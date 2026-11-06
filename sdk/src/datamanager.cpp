#include "../include/datamanager.h"
#include "../include/util/log.h"

namespace ai_chat_sdk
{
    DataManager::DataManager(const std::string &dbName)
        : m_db(nullptr)
        , m_dbName(dbName)
    {
        // sqlite3_open：文件存在就打开，不存在就自动创建一个空的 .db 文件
        if (sqlite3_open(m_dbName.c_str(), &m_db) != SQLITE_OK)
        {
            LOG_ERROR("Failed to open database: {}", sqlite3_errmsg(m_db));
            if (m_db)
            {
                sqlite3_close(m_db);
                m_db = nullptr;
            }
            return;
        }
        LOG_INFO("Database opened: {}", m_dbName);

        // SQLite 默认关闭外键约束，必须每次打开 DB 后显式开启，
        // 否则 ON DELETE CASCADE 会被静默忽略
        // 注意：PRAGMA foreign_keys 必须在没有活动事务时执行
        bool fkOk = false;
        char *execErr = nullptr;
        if (sqlite3_exec(m_db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &execErr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to enable foreign keys: {}", execErr ? execErr : "unknown");
            if (execErr) sqlite3_free(execErr);
        }
        else
        {
            // 用 PRAGMA foreign_keys; 查询当前值，验证是否真的开启了（返回 1 表示开启）
            const char *query = "PRAGMA foreign_keys;";
            sqlite3_stmt *verifyStmt = nullptr;
            if (sqlite3_prepare_v2(m_db, query, -1, &verifyStmt, nullptr) == SQLITE_OK)
            {
                if (sqlite3_step(verifyStmt) == SQLITE_ROW)
                {
                    int val = sqlite3_column_int(verifyStmt, 0);
                    fkOk = (val == 1);
                    if (fkOk)
                        LOG_INFO("Foreign keys enabled (PRAGMA foreign_keys = 1)");
                    else
                        LOG_ERROR("Foreign keys query returned {}, expected 1", val);
                }
                sqlite3_finalize(verifyStmt);
            }
            else
            {
                LOG_ERROR("Failed to prepare PRAGMA foreign_keys query: {}", sqlite3_errmsg(m_db));
            }
        }

        if (!fkOk)
        {
            LOG_ERROR("Foreign keys could not be enabled, aborting database init");
            sqlite3_close(m_db);
            m_db = nullptr;
            return;
        }

        // 建表（幂等：表已存在则跳过）
        if (!initDatabase())
        {
            sqlite3_close(m_db);
            m_db = nullptr;
            LOG_ERROR("Database init failed");
        }
    }

    DataManager::~DataManager()
    {
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
            LOG_INFO("Database closed: {}", m_dbName);
        }
    }

    // ==================== 数据库初始化 ====================

    bool DataManager::initDatabase()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // R"( ... )" 是 C++11 raw string literal：里面的双引号、反斜杠、换行都不用转义
        // CREATE TABLE IF NOT EXISTS：表已存在就跳过，不会报错也不会覆盖
        const std::string createSessionsTable = R"(
            CREATE TABLE IF NOT EXISTS sessions (
                sessionId  TEXT PRIMARY KEY,
                modelName  TEXT NOT NULL,
                createTime INTEGER NOT NULL,
                updateTime INTEGER NOT NULL
            );
        )";

        const std::string createMessagesTable = R"(
            CREATE TABLE IF NOT EXISTS messages (
                messageId  TEXT PRIMARY KEY,
                sessionId  TEXT NOT NULL,
                role       TEXT NOT NULL,
                content    TEXT NOT NULL,
                timestamp  INTEGER NOT NULL,
                FOREIGN KEY (sessionId) REFERENCES sessions(sessionId) ON DELETE CASCADE
            );
        )";

        // 给 messages.sessionId 建索引：加速"查某会话的所有消息"这个高频操作
        // 没有索引时 SQLite 要扫整张 messages 表（全表扫描）
        const std::string createIndex = R"(
            CREATE INDEX IF NOT EXISTS idx_messages_sessionId ON messages(sessionId);
        )";

        if (!executeSQL(createSessionsTable))  return false;
        if (!executeSQL(createMessagesTable)) return false;
        if (!executeSQL(createIndex))         return false;

        LOG_INFO("Database initialized");
        return true;
    }

    // executeSQL 只用于 DDL（CREATE TABLE / CREATE INDEX）
    // 用 sqlite3_exec 一次搞定，内部自动 prepare + step + finalize
    bool DataManager::executeSQL(const std::string &sql)
    {
        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            LOG_ERROR("SQL exec failed: {}", errMsg);
            sqlite3_free(errMsg);  // sqlite3_exec 分配的错误字符串必须手动释放
            return false;
        }
        return true;
    }

    // ==================== prepared statement 四步流程 ====================
    // 1. sqlite3_prepare_v2  → 把 SQL 模板（? 占位符）编译成 stmt 对象
    // 2. sqlite3_bind_xxx    → 把真实参数绑到第 N 个 ? 上（序号从 1 开始！）
    // 3. sqlite3_step        → 执行。SELECT 返回 SQLITE_ROW（继续读）或 SQLITE_DONE（读完）
    //                           INSERT/UPDATE/DELETE 直接返回 SQLITE_DONE
    // 4. sqlite3_finalize    → 释放 stmt。每一个 prepare 必须对应一个 finalize！
    //
    // 常用 sqlite3_bind / sqlite3_column：
    //   bind_text(stmt, 序号, 字符串, -1, SQLITE_TRANSIENT)   — 绑字符串
    //   bind_int64(stmt, 序号, 整数)                          — 绑 64 位整数
    //   column_text(stmt, 列号)  → const unsigned char*       — 读字符串列（必须立刻转 std::string）
    //   column_int64(stmt, 列号) → sqlite3_int64              — 读整数列

    // ==================== Session ====================

    bool DataManager::insertSession(const Session &session)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string sql =
            "INSERT INTO sessions (sessionId, modelName, createTime, updateTime) "
            "VALUES (?, ?, ?, ?);";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return false;
        }

        // bind_text 第 5 个参数 SQLITE_TRANSIENT：
        //   我传的是局部变量 .c_str()，用完会失效；让 SQLite 自己拷贝一份存起来
        //   如果确定字符串会一直活着，可以用 SQLITE_STATIC 省一次拷贝
        sqlite3_bind_text (stmt, 1, session.sessionId.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 2, session.modelName.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(session.createTime));
        sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(session.updateTime));

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);  // 无论 step 成功失败，finalize 必须调

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to insert session: {}", sqlite3_errmsg(m_db));
            return false;
        }
        LOG_INFO("Session inserted: {}", session.sessionId);
        return true;
    }

    // 根据 sessionId 查会话 + 顺带加载它的所有消息
    // 为什么不调 getMessagesBySessionId？
    //   因为两个方法各自加锁，std::mutex 不支持递归，嵌套加锁会死锁！
    //   所以把查 messages 的逻辑直接内联在这里，全程只拿一把锁
    std::shared_ptr<Session> DataManager::getSession(const std::string &sessionId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // --- 第一步：查 sessions 表拿会话元信息 ---
        const std::string sessionSql =
            "SELECT sessionId, modelName, createTime, updateTime FROM sessions "
            "WHERE sessionId = ?;";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sessionSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return nullptr;
        }
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return nullptr;  // sessionId 不存在
        }

        // sqlite3_column_text 返回 const unsigned char*，转成 const char* 再喂给 std::string
        // 注意：这个指针在 sqlite3_step 下一次调用后就失效了，必须立刻用 std::string 拷贝
        std::string sid  (reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        std::string model(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
        auto createTime  = static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
        auto updateTime  = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));

        sqlite3_finalize(stmt);

        auto session = std::make_shared<Session>(model);
        session->sessionId  = sid;
        session->createTime = createTime;
        session->updateTime = updateTime;

        // --- 第二步：查 messages 表拿该会话的所有历史消息（内联，不加第二把锁）---
        const std::string msgSql =
            "SELECT messageId, role, content, timestamp FROM messages "
            "WHERE sessionId = ? ORDER BY timestamp ASC;";

        if (sqlite3_prepare_v2(m_db, msgSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return session;  // 会话信息已经拿到了，消息加载失败不影响返回
        }
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            std::string msgId (reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
            std::string role  (reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
            std::string content(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)));
            auto timestamp = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));

            Message msg(role, content);
            msg.messageId = msgId;
            msg.timestamp = timestamp;
            session->messages.push_back(msg);
        }
        sqlite3_finalize(stmt);

        return session;
    }

    void DataManager::updateSessionTimestamp(const std::string &sessionId, std::time_t timestamp)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string sql = "UPDATE sessions SET updateTime = ? WHERE sessionId = ?;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return;
        }
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(timestamp));
        sqlite3_bind_text (stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);     // 忽略返回值（session 可能已被删）
        sqlite3_finalize(stmt);
    }

    // 删除会话。因为 messages 表设了 ON DELETE CASCADE，
    // SQLite 会自动把 sessionId 等于被删会话的所有 messages 也删掉
    bool DataManager::deleteSession(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string sql = "DELETE FROM sessions WHERE sessionId = ?;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return false;
        }
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to delete session: {}", sqlite3_errmsg(m_db));
            return false;
        }
        LOG_INFO("Session deleted: {}", sessionId);
        return true;
    }

    // 获取所有会话 ID，按 updateTime 降序（最近活跃的排前面）
    std::vector<std::string> DataManager::getAllSessionIds() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> ids;

        const std::string sql = "SELECT sessionId FROM sessions ORDER BY updateTime DESC;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return ids;
        }

        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            ids.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
        return ids;
    }

    // 获取所有会话的完整信息（不加载消息）
    // 不加载消息是为了避免 N+1 查询：如果每个 session 都查一次 messages，
    // 有 100 个会话就执行 101 次 SQL。消息按需用 getSession(sessionId) 单独查
    std::vector<std::shared_ptr<Session>> DataManager::getAllSessions() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::shared_ptr<Session>> sessions;

        const std::string sql =
            "SELECT sessionId, modelName, createTime, updateTime "
            "FROM sessions ORDER BY updateTime DESC;";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return sessions;
        }

        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            // sqlite3_column_text 返回的指针在 sqlite3_step 下一次调用后失效
            // 必须立刻拷贝到 std::string
            std::string sid  (reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
            std::string model(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
            auto createTime = static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
            auto updateTime = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));

            auto session = std::make_shared<Session>(model);
            session->sessionId  = sid;
            session->createTime = createTime;
            session->updateTime = updateTime;
            sessions.push_back(session);
        }
        sqlite3_finalize(stmt);
        return sessions;
    }

    size_t DataManager::getSessionCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string sql = "SELECT COUNT(*) FROM sessions;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return 0;
        }

        size_t count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return count;
    }

    bool DataManager::clearAllSessions()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string sql = "DELETE FROM sessions;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return false;
        }
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to clear sessions: {}", sqlite3_errmsg(m_db));
            return false;
        }
        LOG_INFO("All sessions cleared");
        return true;
    }

    // ==================== Message ====================

    // 往某个会话里插一条消息，同时顺便更新该会话的 updateTime
    // 两次操作放在同一个事务里，要么全成要么全不成
    bool DataManager::insertMessage(const std::string &sessionId, const Message &message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 开启事务。BEGIN TRANSACTION 保证接下来的 INSERT + UPDATE 要么一起提交要么一起回滚
        if (sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to begin transaction: {}", sqlite3_errmsg(m_db));
            return false;
        }

        // ========== 第一步：INSERT messages ==========
        const std::string sql =
            "INSERT INTO messages (messageId, sessionId, role, content, timestamp) "
            "VALUES (?, ?, ?, ?, ?);";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare INSERT SQL: {}", sqlite3_errmsg(m_db));
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        sqlite3_bind_text (stmt, 1, message.messageId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 2, sessionId.c_str(),        -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 3, message.role.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 4, message.content.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(message.timestamp));

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = nullptr;
        if (rc != SQLITE_DONE)
        {
            // 外键约束在事务里也生效：如果 sessionId 在 sessions 表里不存在，
            // 这里会返回 SQLITE_CONSTRAINT，ROLLBACK 后整个事务作废
            LOG_ERROR("Failed to insert message: {}", sqlite3_errmsg(m_db));
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        // ========== 第二步：UPDATE sessions.updateTime ==========
        const std::string updateSql =
            "UPDATE sessions SET updateTime = ? WHERE sessionId = ?;";

        sqlite3_stmt *updateStmt = nullptr;
        if (sqlite3_prepare_v2(m_db, updateSql.c_str(), -1, &updateStmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare UPDATE SQL: {}", sqlite3_errmsg(m_db));
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        sqlite3_bind_int64(updateStmt, 1, static_cast<int64_t>(std::time(nullptr)));
        sqlite3_bind_text (updateStmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(updateStmt);
        sqlite3_finalize(updateStmt);
        updateStmt = nullptr;

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to update session timestamp: {}", sqlite3_errmsg(m_db));
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        // 检查 UPDATE 实际影响了几行。sqlite3_changes 返回最近一次 INSERT/UPDATE/DELETE 影响的行数
        // 如果返回 0，说明 sessionId 根本不存在于 sessions 表里——虽然 INSERT 那步外键约束会拦住，
        // 但多一层检查确保万无一失（比如外键约束被误关、或者 sessions 表被并发删除）
        int changes = sqlite3_changes(m_db);
        if (changes == 0)
        {
            LOG_ERROR("UPDATE sessions affected 0 rows, session may not exist: {}", sessionId);
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        // ========== 第三步：COMMIT ==========
        rc = sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
        {
            LOG_ERROR("Failed to commit transaction: {}", sqlite3_errmsg(m_db));
            // COMMIT 失败时 ROLLBACK 尝试撤销已执行的变更
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        return true;
    }

    // 查某个会话的所有历史消息，按 timestamp 升序（先发的排前面）
    std::vector<Message> DataManager::getMessagesBySessionId(const std::string &sessionId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<Message> messages;

        const std::string sql =
            "SELECT messageId, role, content, timestamp FROM messages "
            "WHERE sessionId = ? ORDER BY timestamp ASC;";

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SQL: {}", sqlite3_errmsg(m_db));
            return messages;
        }
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            std::string msgId (reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
            std::string role  (reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
            std::string content(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)));
            auto timestamp = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));

            Message msg(role, content);
            msg.messageId = msgId;
            msg.timestamp = timestamp;
            messages.push_back(msg);
        }
        sqlite3_finalize(stmt);
        return messages;
    }
}// namespace ai_chat_sdk
