#include "../include/sessionmanager.h"
#include "../include/util/log.h"
#include <sstream>
#include <iomanip>

namespace ai_chat_sdk
{
    // static 成员必须在类外分配存储，这是 C++ 规则
    std::atomic<int64_t> SessionManager::s_messageCounter{0};
    std::atomic<int64_t> SessionManager::s_sessionCounter{0};

    std::string SessionManager::generateMessageId()
    {
        s_messageCounter.fetch_add(1);
        std::time_t now = std::time(nullptr);
        std::ostringstream oss;
        oss << "msg_" << now << "_"
            << std::setfill('0') << std::setw(8) << s_messageCounter;  // oss = msg_1700000000_00000042
        return oss.str();
    }

    std::string SessionManager::generateSessionId()
    {
        s_sessionCounter.fetch_add(1);
        std::time_t now = std::time(nullptr);
        std::ostringstream oss;
        oss << "session_" << now << "_"
            << std::setfill('0') << std::setw(8) << s_sessionCounter;
        return oss.str();
    }

    std::string SessionManager::createSession(const std::string &modelName)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::string sessionId = generateSessionId();
        auto session = std::make_shared<Session>(modelName);
        session->sessionId = sessionId;
        std::time_t now = std::time(nullptr);
        session->createTime = now;
        session->updateTime = now;

        m_sessions.emplace(sessionId, session);
        LOG_INFO("Session created: {}, model: {}", sessionId, modelName);
        return sessionId;
    }

    std::shared_ptr<Session> SessionManager::getSession(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            return nullptr;
        }
        return it->second;
    }

    bool SessionManager::addMessage(const std::string &sessionId, const Message &message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            LOG_ERROR("Session not found when adding message: {}", sessionId);
            return false;
        }

        // 拷贝一份再填字段，messageId 和 timestamp 由 SessionManager 统一生成
        Message msg(message.role, message.content);
        msg.messageId = generateMessageId();
        msg.timestamp = std::time(nullptr);

        it->second->messages.push_back(msg);
        it->second->updateTime = msg.timestamp;

        LOG_INFO("Message added to session {}, role: {}", sessionId, msg.role);
        return true;
    }

    std::vector<Message> SessionManager::getSessionHistory(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            LOG_ERROR("Session not found when getting history: {}", sessionId);
            return {};
        }
        return it->second->messages;
    }

    std::vector<std::string> SessionManager::getSessionList() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> sessionIds;
        sessionIds.reserve(m_sessions.size());
        for (const auto &it : m_sessions)
        {
            sessionIds.push_back(it.first);
        }
        return sessionIds;
    }

    bool SessionManager::deleteSession(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            LOG_ERROR("Session not found when deleting: {}", sessionId);
            return false;
        }
        m_sessions.erase(it);
        LOG_INFO("Session deleted: {}", sessionId);
        return true;
    }

    void SessionManager::updateSessionTimestamp(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            return;
        }
        it->second->updateTime = std::time(nullptr);
    }

    void SessionManager::clearAllSessions()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sessions.clear();
        LOG_INFO("All sessions cleared");
    }

    size_t SessionManager::getSessionCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sessions.size();
    }
}// namespace ai_chat_sdk