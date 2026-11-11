#include "../include/datamanager.h"
#include "../include/util/log.h"
#include <ctime>

namespace ai_chat_sdk
{
    namespace
    {
        // 当前数据库 schema 版本，与 PRAGMA user_version 对应
        // 1 = 驼峰列名（sessionId / modelName / createTime / updateTime / messageId）
        const int kSchemaVersion = 1;
        // 遇到 SQLITE_BUSY 时的最长等待时间（毫秒），避免多连接并发直接失败
        const int kBusyTimeoutMs = 5000;
    }

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

        // 并发访问同一文件时，写锁冲突会返回 SQLITE_BUSY；设置等待时间让它自动重试
        sqlite3_busy_timeout(m_db, kBusyTimeoutMs);

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
            // 执行成功不代表真的生效，必须用 PRAGMA foreign_keys; 查询确认返回值为 1
            sqlite3_stmt *verifyStmt = nullptr;
            if (sqlite3_prepare_v2(m_db, "PRAGMA foreign_keys;", -1, &verifyStmt, nullptr) == SQLITE_OK)
            {
                int rc = sqlite3_step(verifyStmt);
                if (rc == SQLITE_ROW)
                {
                    int val = sqlite3_column_int(verifyStmt, 0);
                    fkOk = (val == 1);
                    if (fkOk)
                        LOG_INFO("Foreign keys enabled (PRAGMA foreign_keys = 1)");
                    else
                        LOG_ERROR("Foreign keys query returned {}, expected 1", val);
                }
                else
                {
                    LOG_ERROR("Failed to read PRAGMA foreign_keys: {}", sqlite3_errmsg(m_db));
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

        // 建表 + schema 迁移 + 建索引
        if (!initDatabase())
        {
            LOG_ERROR("Database init failed, closing connection");
            sqlite3_close(m_db);
            m_db = nullptr;
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

    bool DataManager::isOpen() const
    {
        return m_db != nullptr;
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

        if (!executeSQL(createSessionsTable))  return false;
        if (!executeSQL(createMessagesTable)) return false;

        // CREATE TABLE IF NOT EXISTS 不会修正已存在的旧表结构，
        // 因此必须在建索引之前把历史的下划线列名迁移成驼峰
        if (!migrateSchema()) return false;

        // 给 messages.sessionId 建索引：加速"查某会话的所有消息"这个高频操作
        // 必须放在迁移之后，否则旧表还没有 sessionId 列
        const std::string createIndex = R"(
            CREATE INDEX IF NOT EXISTS idx_messages_sessionId ON messages(sessionId);
        )";
        if (!executeSQL(createIndex)) return false;

        LOG_INFO("Database initialized");
        return true;
    }

    // 把 v0（下划线列名）迁移到 v1（驼峰列名），保留已有数据
    // 调用方必须已持有 m_mutex
    bool DataManager::migrateSchema()
    {
        int version = readUserVersion();
        if (version < 0)
        {
            LOG_ERROR("Failed to read schema version");
            return false;
        }
        if (version >= kSchemaVersion)
        {
            return true;
        }

        struct RenameRule
        {
            const char *table;
            const char *oldColumn;
            const char *newColumn;
        };
        const RenameRule rules[] = {
            {"sessions", "session_id", "sessionId"},
            {"sessions", "model_name", "modelName"},
            {"sessions", "create_time", "createTime"},
            {"sessions", "update_time", "updateTime"},
            {"messages", "message_id", "messageId"},
            {"messages", "session_id", "sessionId"},
        };

        for (const auto &rule : rules)
        {
            // 只有"旧列存在且新列不存在"才需要迁移
            if (!columnExists(rule.table, rule.oldColumn) ||
                columnExists(rule.table, rule.newColumn))
            {
                continue;
            }

            const std::string sql = "ALTER TABLE " + std::string(rule.table) +
                                    " RENAME COLUMN " + rule.oldColumn +
                                    " TO " + rule.newColumn + ";";
            if (!executeSQL(sql))
            {
                LOG_ERROR("Failed to migrate column {}.{} -> {}",
                          rule.table, rule.oldColumn, rule.newColumn);
                return false;
            }
            LOG_INFO("Migrated column {}.{} -> {}", rule.table, rule.oldColumn, rule.newColumn);
        }

        // 旧版本可能遗留的索引名，删掉避免和新的 idx_messages_sessionId 重复
        if (!executeSQL("DROP INDEX IF EXISTS idx_messages_session_id;"))
        {
            return false;
        }

        // 记录版本号，下次启动直接跳过迁移
        if (!executeSQL("PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";"))
        {
            LOG_ERROR("Failed to set schema version");
            return false;
        }

        LOG_INFO("Schema migrated to version {}", kSchemaVersion);
        return true;
    }

    bool DataManager::columnExists(const std::string &table, const std::string &column) const
    {
        // PRAGMA table_info 的第 2 列（索引 1）是列名
        const std::string sql = "PRAGMA table_info(" + table + ");";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare PRAGMA table_info: {}", sqlite3_errmsg(m_db));
            return false;
        }

        bool found = false;
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            if (column == columnToString(stmt, 1))
            {
                found = true;
                break;
            }
        }
        sqlite3_finalize(stmt);

        if (rc != SQLITE_ROW && rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to read table info for {}: {}", table, sqlite3_errmsg(m_db));
        }
        return found;
    }

    int DataManager::readUserVersion() const
    {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare PRAGMA user_version: {}", sqlite3_errmsg(m_db));
            return -1;
        }

        int version = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            version = sqlite3_column_int(stmt, 0);
        }
        else
        {
            LOG_ERROR("Failed to read PRAGMA user_version: {}", sqlite3_errmsg(m_db));
        }
        sqlite3_finalize(stmt);
        return version;
    }

    // executeSQL 只用于 DDL / PRAGMA（CREATE TABLE / CREATE INDEX / PRAGMA 等）
    // 用 sqlite3_exec 一次搞定，内部自动 prepare + step + finalize
    bool DataManager::executeSQL(const std::string &sql)
    {
        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            LOG_ERROR("SQL exec failed: {}", errMsg ? errMsg : "unknown");
            if (errMsg) sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    bool DataManager::rollback()
    {
        if (sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            // ROLLBACK 失败会让连接卡在事务中，之后所有写操作都会报
            // "cannot start a transaction within a transaction"
            LOG_ERROR("Failed to rollback transaction: {}", sqlite3_errmsg(m_db));
            return false;
        }
        return true;
    }

    std::string DataManager::columnToString(sqlite3_stmt *stmt, int idx)
    {
        const unsigned char *text = sqlite3_column_text(stmt, idx);
        if (!text)
        {
            // 列为 NULL 时 sqlite3_column_text 返回 nullptr，
            // 直接喂给 std::string 是未定义行为。正常数据被 NOT NULL 约束保证，
            // 但外部工具可能绕过约束写入，这里返回空串兜底
            return std::string();
        }
        return std::string(reinterpret_cast<const char *>(text));
    }

    // ==================== prepared statement 四步流程 ====================
    // 1. sqlite3_prepare_v2  → 把 SQL 模板（? 占位符）编译成 stmt 对象
    // 2. sqlite3_bind_xxx    → 把真实参数绑到第 N 个 ? 上（序号从 1 开始）
    // 3. sqlite3_step        → 执行。SELECT 返回 SQLITE_ROW / SQLITE_DONE，
    //                          写操作返回 SQLITE_DONE
    // 4. sqlite3_finalize    → 释放 stmt，每一个 prepare 必须对应一个 finalize

    // ==================== Session ====================

    bool DataManager::insertSession(const Session &session)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot insert session: {}", m_dbName);
            return false;
        }

        const std::string sql =
            "INSERT INTO sessions (sessionId, modelName, createTime, updateTime) "
            "VALUES (?, ?, ?, ?);";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare INSERT session: {}", sqlite3_errmsg(m_db));
            return false;
        }

        // SQLITE_TRANSIENT：让 SQLite 自己拷贝字符串，不依赖外部内存生命周期
        if (sqlite3_bind_text(stmt, 1, session.sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 2, session.modelName.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(session.createTime)) != SQLITE_OK ||
            sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(session.updateTime)) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind INSERT session params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            return false;
        }

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to insert session {}: {}", session.sessionId, sqlite3_errmsg(m_db));
            return false;
        }
        LOG_INFO("Session inserted: {}", session.sessionId);
        return true;
    }

    // 根据 sessionId 查会话 + 顺带加载它的所有消息
    // 这里内联查询 messages 而不是调 getMessagesBySessionId：
    //   std::mutex 不支持递归，嵌套加锁会死锁
    std::shared_ptr<Session> DataManager::getSession(const std::string &sessionId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot get session: {}", m_dbName);
            return nullptr;
        }

        // --- 第一步：查 sessions 表拿会话元信息 ---
        const std::string sessionSql =
            "SELECT sessionId, modelName, createTime, updateTime FROM sessions "
            "WHERE sessionId = ?;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sessionSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SELECT session: {}", sqlite3_errmsg(m_db));
            return nullptr;
        }
        if (sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind SELECT session params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            return nullptr;
        }

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW)
        {
            // 区分"查无此行"和"查询出错"，避免把 SQL 错误当成会话不存在
            if (rc != SQLITE_DONE)
            {
                LOG_ERROR("Failed to query session {}: {}", sessionId, sqlite3_errmsg(m_db));
            }
            sqlite3_finalize(stmt);
            return nullptr;
        }

        // sqlite3_column_text 的指针在下一次 step 后失效，必须立刻拷贝到 std::string
        std::string sid   = columnToString(stmt, 0);
        std::string model = columnToString(stmt, 1);
        auto createTime = static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
        auto updateTime = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
        sqlite3_finalize(stmt);

        auto session = std::make_shared<Session>(model);
        session->sessionId  = sid;
        session->createTime = createTime;
        session->updateTime = updateTime;

        // --- 第二步：查 messages 表拿该会话的所有历史消息（不加第二把锁）---
        const std::string msgSql =
            "SELECT messageId, role, content, timestamp FROM messages "
            "WHERE sessionId = ? ORDER BY timestamp ASC;";

        if (sqlite3_prepare_v2(m_db, msgSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SELECT messages: {}", sqlite3_errmsg(m_db));
            return session;  // 会话信息已拿到，消息加载失败不影响返回
        }
        if (sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind SELECT messages params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            return session;
        }

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            Message msg(columnToString(stmt, 1), columnToString(stmt, 2));
            msg.messageId = columnToString(stmt, 0);
            msg.timestamp = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
            session->messages.push_back(msg);
        }
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to read messages of session {}: {}", sessionId, sqlite3_errmsg(m_db));
        }

        return session;
    }

    void DataManager::updateSessionTimestamp(const std::string &sessionId, std::time_t timestamp)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot update session timestamp: {}", m_dbName);
            return;
        }

        const std::string sql = "UPDATE sessions SET updateTime = ? WHERE sessionId = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare UPDATE session: {}", sqlite3_errmsg(m_db));
            return;
        }
        if (sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(timestamp)) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind UPDATE session params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            return;
        }

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to update session {}: {}", sessionId, sqlite3_errmsg(m_db));
            return;
        }
        if (sqlite3_changes(m_db) == 0)
        {
            LOG_ERROR("Session not found when updating timestamp: {}", sessionId);
        }
    }

    // 删除会话。messages 表设了 ON DELETE CASCADE，
    // SQLite 会自动删掉该会话的全部消息，不需要手动删两次
    bool DataManager::deleteSession(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot delete session: {}", m_dbName);
            return false;
        }

        const std::string sql = "DELETE FROM sessions WHERE sessionId = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare DELETE session: {}", sqlite3_errmsg(m_db));
            return false;
        }
        if (sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind DELETE session params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            return false;
        }

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to delete session {}: {}", sessionId, sqlite3_errmsg(m_db));
            return false;
        }
        if (sqlite3_changes(m_db) == 0)
        {
            // DELETE 返回 SQLITE_DONE 但没删到任何行，说明会话不存在
            LOG_ERROR("Session not found when deleting: {}", sessionId);
            return false;
        }

        LOG_INFO("Session deleted: {}", sessionId);
        return true;
    }

    std::vector<std::string> DataManager::getAllSessionIds() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> ids;

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot list sessions: {}", m_dbName);
            return ids;
        }

        const std::string sql = "SELECT sessionId FROM sessions ORDER BY updateTime DESC;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SELECT session ids: {}", sqlite3_errmsg(m_db));
            return ids;
        }

        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            ids.push_back(columnToString(stmt, 0));
        }
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to read session ids: {}", sqlite3_errmsg(m_db));
        }
        return ids;
    }

    // 获取所有会话的元信息（不加载消息，避免 N+1 查询）
    std::vector<std::shared_ptr<Session>> DataManager::getAllSessions() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::shared_ptr<Session>> sessions;

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot list sessions: {}", m_dbName);
            return sessions;
        }

        const std::string sql =
            "SELECT sessionId, modelName, createTime, updateTime "
            "FROM sessions ORDER BY updateTime DESC;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SELECT sessions: {}", sqlite3_errmsg(m_db));
            return sessions;
        }

        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            auto session = std::make_shared<Session>(columnToString(stmt, 1));
            session->sessionId  = columnToString(stmt, 0);
            session->createTime = static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
            session->updateTime = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
            sessions.push_back(session);
        }
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to read sessions: {}", sqlite3_errmsg(m_db));
        }
        return sessions;
    }

    size_t DataManager::getSessionCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot count sessions: {}", m_dbName);
            return 0;
        }

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM sessions;", -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare COUNT session: {}", sqlite3_errmsg(m_db));
            return 0;
        }

        size_t count = 0;
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
        else
        {
            LOG_ERROR("Failed to count sessions: {}", sqlite3_errmsg(m_db));
        }
        sqlite3_finalize(stmt);
        return count;
    }

    bool DataManager::clearAllSessions()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot clear sessions: {}", m_dbName);
            return false;
        }

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "DELETE FROM sessions;", -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare DELETE all sessions: {}", sqlite3_errmsg(m_db));
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

    // 插入消息 + 更新会话 updateTime，放在同一事务里保证原子性
    bool DataManager::insertMessage(const std::string &sessionId, const Message &message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot insert message: {}", m_dbName);
            return false;
        }

        if (sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to begin transaction: {}", sqlite3_errmsg(m_db));
            return false;
        }

        // ---------- 第一步：INSERT messages ----------
        const std::string sql =
            "INSERT INTO messages (messageId, sessionId, role, content, timestamp) "
            "VALUES (?, ?, ?, ?, ?);";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare INSERT message: {}", sqlite3_errmsg(m_db));
            rollback();
            return false;
        }

        if (sqlite3_bind_text(stmt, 1, message.messageId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 3, message.role.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(stmt, 4, message.content.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(message.timestamp)) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind INSERT message params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            rollback();
            return false;
        }

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            // 外键约束生效时，sessionId 不存在会返回 SQLITE_CONSTRAINT
            LOG_ERROR("Failed to insert message {}: {}", message.messageId, sqlite3_errmsg(m_db));
            rollback();
            return false;
        }

        // ---------- 第二步：UPDATE sessions.updateTime ----------
        // 用消息自身的时间戳，保证导入历史消息时会话的排序时间仍然正确
        const std::string updateSql =
            "UPDATE sessions SET updateTime = ? WHERE sessionId = ?;";

        sqlite3_stmt *updateStmt = nullptr;
        if (sqlite3_prepare_v2(m_db, updateSql.c_str(), -1, &updateStmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare UPDATE session: {}", sqlite3_errmsg(m_db));
            rollback();
            return false;
        }

        if (sqlite3_bind_int64(updateStmt, 1, static_cast<int64_t>(message.timestamp)) != SQLITE_OK ||
            sqlite3_bind_text(updateStmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind UPDATE session params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(updateStmt);
            rollback();
            return false;
        }

        rc = sqlite3_step(updateStmt);
        sqlite3_finalize(updateStmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to update session {} timestamp: {}", sessionId, sqlite3_errmsg(m_db));
            rollback();
            return false;
        }

        // UPDATE 返回 SQLITE_DONE 但影响 0 行，说明会话不存在
        if (sqlite3_changes(m_db) == 0)
        {
            LOG_ERROR("Session not found when inserting message: {}", sessionId);
            rollback();
            return false;
        }

        // ---------- 第三步：COMMIT ----------
        if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to commit transaction: {}", sqlite3_errmsg(m_db));
            rollback();
            return false;
        }

        return true;
    }

    std::vector<Message> DataManager::getMessagesBySessionId(const std::string &sessionId) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<Message> messages;

        if (!m_db)
        {
            LOG_ERROR("Database is not open, cannot read messages: {}", m_dbName);
            return messages;
        }

        const std::string sql =
            "SELECT messageId, role, content, timestamp FROM messages "
            "WHERE sessionId = ? ORDER BY timestamp ASC;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            LOG_ERROR("Failed to prepare SELECT messages: {}", sqlite3_errmsg(m_db));
            return messages;
        }
        if (sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        {
            LOG_ERROR("Failed to bind SELECT messages params: {}", sqlite3_errmsg(m_db));
            sqlite3_finalize(stmt);
            return messages;
        }

        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            Message msg(columnToString(stmt, 1), columnToString(stmt, 2));
            msg.messageId = columnToString(stmt, 0);
            msg.timestamp = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
            messages.push_back(msg);
        }
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE)
        {
            LOG_ERROR("Failed to read messages of session {}: {}", sessionId, sqlite3_errmsg(m_db));
        }
        return messages;
    }
}// namespace ai_chat_sdk
