#pragma once

#include "common.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <ctime>
#include <sqlite3.h>

namespace ai_chat_sdk
{
    // DataManager 职责：把用户、会话、消息、登录态持久化到 SQLite 文件
    // 所有公共方法线程安全（内部持有一把互斥锁）
    //
    // 注意：
    //   1. 构造函数可能失败（打开失败 / 外键开启失败 / 建表失败），
    //      失败后 m_db 为 nullptr，用 isOpen() 判断；此后所有操作都会快速返回失败
    //   2. 自动迁移历史 schema（下划线列名 -> 驼峰列名 -> 用户体系），通过 PRAGMA user_version 控制
    class DataManager
    {
    public:
        // 构造时打开/创建数据库文件，并完成外键开启 + 建表 + schema 迁移
        explicit DataManager(const std::string &dbName);
        ~DataManager();

        // 数据库是否已成功打开并初始化完成
        bool isOpen() const;

        // --- User 相关 ---

        // 插入新用户；username 已存在时返回 false（依赖 UNIQUE 约束）
        bool insertUser(const User &user);
        // 按用户名查用户；不存在返回 nullptr
        std::shared_ptr<User> getUserByUsername(const std::string &username) const;

        // --- AuthToken 相关 ---

        // 记录一条登录 token 及其过期时间
        bool insertAuthToken(const std::string &token, const std::string &userId, std::time_t expireTime);
        // 按 token 查所属用户 ID；token 不存在或已过期返回空串
        std::string getUserIdByToken(const std::string &token) const;
        // 删除某条 token（登出用）；token 不存在返回 false
        bool deleteAuthToken(const std::string &token);

        // --- Session 相关 ---

        // 插入一条新会话（Session.userId 必须已设置）；sessionId 已存在时返回 false
        bool insertSession(const Session &session);
        // 根据 userId + sessionId 查会话，同时加载全部历史消息；不存在返回 nullptr
        std::shared_ptr<Session> getSession(const std::string &userId, const std::string &sessionId) const;
        // 更新某个会话的 updateTime；会话不存在时只记录日志，不返回错误
        void updateSessionTimestamp(const std::string &userId, const std::string &sessionId, std::time_t timestamp);
        // 删除会话；依赖 ON DELETE CASCADE 自动删掉该会话的全部消息
        // 会话不存在或不属于该用户时返回 false
        bool deleteSession(const std::string &userId, const std::string &sessionId);
        // 获取某用户的所有会话 ID，按 updateTime 降序
        std::vector<std::string> getAllSessionIds(const std::string &userId) const;
        // 获取某用户的所有会话元信息，按 updateTime 降序
        // 注意：返回的 Session::messages 为空（避免 N+1 查询），需要消息请用 getSession()
        std::vector<std::shared_ptr<Session>> getAllSessions(const std::string &userId) const;
        // 清空某用户的所有会话（级联清空所有消息）
        bool clearAllSessions(const std::string &userId);
        // 某用户的会话总数。返回 0 时可能表示"空表"也可能表示"查询失败"，失败会打错误日志
        size_t getSessionCount(const std::string &userId) const;

        // --- Message 相关 ---

        // 插入一条消息并同步更新所属会话的 updateTime，两步在同一事务内完成
        // 会话不存在或不属于该用户时返回 false
        bool insertMessage(const std::string &userId, const std::string &sessionId, const Message &message);
        // 获取某个会话的全部历史消息，按 timestamp 升序；会话不属于该用户时返回空
        std::vector<Message> getMessagesBySessionId(const std::string &userId, const std::string &sessionId) const;

    private:
        // 建表 + schema 迁移 + 建索引（构造期调用一次）
        bool initDatabase();
        // 按 PRAGMA user_version 逐步迁移历史 schema，并记录版本
        bool migrateSchema();
        // 执行不带参数的 DDL / PRAGMA 语句
        bool executeSQL(const std::string &sql);
        // 查询某张表是否存在指定列（用于 schema 迁移）
        bool columnExists(const std::string &table, const std::string &column) const;
        // 读取 PRAGMA user_version，失败返回 -1
        int readUserVersion() const;
        // 回滚当前事务，失败时记录日志（连接可能卡在事务中）
        bool rollback();
        // 安全读取文本列：NULL 返回空串，避免 std::string(nullptr) 未定义行为
        static std::string columnToString(sqlite3_stmt *stmt, int idx);

    private:
        sqlite3 *m_db;
        std::string m_dbName;
        mutable std::mutex m_mutex;
    };
}// namespace ai_chat_sdk
