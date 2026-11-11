#include "../include/sessionmanager.h"
#include "../include/datamanager.h"
#include "../include/util/log.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ai_chat_sdk
{
    // static 成员必须在类外分配存储，这是 C++ 规则
    std::atomic<int64_t> SessionManager::s_messageCounter{0};
    std::atomic<int64_t> SessionManager::s_sessionCounter{0};

    SessionManager::SessionManager(DataManager &db)
        : m_db(db)
    {
        // 启动时把数据库里的历史会话恢复回内存。
        // 列表接口需要每条会话的消息数和首条用户消息，所以这里连消息一起加载；
        // 如果会话量级很大，应改成分页加载或按需懒加载。
        const std::vector<std::string> sessionIds = m_db.getAllSessionIds();
        for (const auto &sessionId : sessionIds)
        {
            auto session = m_db.getSession(sessionId);
            if (session)
            {
                m_sessions.emplace(sessionId, std::move(session));
            }
        }
        LOG_INFO("SessionManager restored {} sessions from database", m_sessions.size());
    }

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

        auto session = std::make_shared<Session>(modelName);
        session->sessionId = generateSessionId();
        std::time_t now = std::time(nullptr);
        session->createTime = now;
        session->updateTime = now;

        // 先落库：写库失败就不注册，保证内存和数据库不会出现单边数据
        if (!m_db.insertSession(*session))
        {
            LOG_ERROR("Failed to persist session: {}", session->sessionId);
            return "";
        }

        const std::string sessionId = session->sessionId;
        m_sessions.emplace(sessionId, std::move(session));
        LOG_INFO("Session created: {}, model: {}", sessionId, modelName);
        return sessionId;
    }

    std::shared_ptr<Session> SessionManager::getSession(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it != m_sessions.end())
        {
            return it->second;
        }

        // 内存未命中：回数据库加载，并回填内存缓存
        auto session = m_db.getSession(sessionId);
        if (!session)
        {
            return nullptr;
        }
        return m_sessions.emplace(sessionId, std::move(session)).first->second;
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

        // 先落库：失败则内存保持不变，避免内存与数据库不一致
        if (!m_db.insertMessage(sessionId, msg))
        {
            LOG_ERROR("Failed to persist message for session: {}", sessionId);
            return false;
        }

        it->second->messages.push_back(msg);
        it->second->updateTime = msg.timestamp;
        LOG_INFO("Message added to session {}, role: {}", sessionId, msg.role);
        return true;
    }

    std::vector<Message> SessionManager::getSessionHistory(const std::string &sessionId)
    {
        // getSession 内部自己加锁，这里不能同时持锁调用，否则重复加锁会死锁
        auto session = getSession(sessionId);
        if (!session)
        {
            LOG_ERROR("Session not found when getting history: {}", sessionId);
            return {};
        }

        // 重新加锁拷贝：getSession 返回后，其他线程可能还在往 messages 里追加
        std::lock_guard<std::mutex> lock(m_mutex);
        return session->messages;
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

        // 产品要求：会话列表按最近更新时间降序，最近聊过的排在最前
        std::sort(sessionIds.begin(), sessionIds.end(),
            [this](const std::string &a, const std::string &b) {
                return m_sessions.at(a)->updateTime > m_sessions.at(b)->updateTime;
            });
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

        // 先删库（外键级联删除该会话的所有消息），成功后再删内存
        if (!m_db.deleteSession(sessionId))
        {
            LOG_ERROR("Failed to delete session from database: {}", sessionId);
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
            LOG_ERROR("Session not found when updating timestamp: {}", sessionId);
            return;
        }

        std::time_t now = std::time(nullptr);
        it->second->updateTime = now;
        m_db.updateSessionTimestamp(sessionId, now);
    }

    void SessionManager::clearAllSessions()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 先清库，成功后再清内存
        if (!m_db.clearAllSessions())
        {
            LOG_ERROR("Failed to clear sessions in database");
            return;
        }

        m_sessions.clear();
        LOG_INFO("All sessions cleared");
    }

    size_t SessionManager::getSessionCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sessions.size();
    }
}// namespace ai_chat_sdk
