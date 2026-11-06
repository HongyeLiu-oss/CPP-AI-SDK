#pragma once

#include "common.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <sqlite3.h>

namespace ai_chat_sdk
{
    class DataManager
    {
    public:
        // 构造时打开/创建数据库文件；失败时 _db 为 nullptr
        explicit DataManager(const std::string &dbName);
        ~DataManager();

        // --- Session 相关 ---
        bool insertSession(const Session &session);
        std::shared_ptr<Session> getSession(const std::string &sessionId) const;
        void updateSessionTimestamp(const std::string &sessionId, std::time_t timestamp);
        bool deleteSession(const std::string &sessionId);
        std::vector<std::string> getAllSessionIds() const;
        std::vector<std::shared_ptr<Session>> getAllSessions() const;
        bool clearAllSessions();
        size_t getSessionCount() const;

        // --- Message 相关 ---
        bool insertMessage(const std::string &sessionId, const Message &message);
        std::vector<Message> getMessagesBySessionId(const std::string &sessionId) const;

    private:
        bool initDatabase();
        bool executeSQL(const std::string &sql);

    private:
        sqlite3 *m_db;
        std::string m_dbName;
        mutable std::mutex m_mutex;
    };
}// namespace ai_chat_sdk