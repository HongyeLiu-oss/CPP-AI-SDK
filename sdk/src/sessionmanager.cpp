#include "../include/sessionmanager.h"
#include "../include/datamanager.h"
#include "../include/util/log.h"
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

    void SessionManager::eraseFromCache(const std::string &sessionId)
    {
        // 只在已持锁的上下文中调用，不重复加锁
        m_sessions.erase(sessionId);
    }

    std::string SessionManager::createSession(const std::string &userId, const std::string &modelName)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto session = std::make_shared<Session>(modelName);
        session->sessionId = generateSessionId();
        session->userId    = userId;
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
        LOG_INFO("Session created: {}, user: {}, model: {}", sessionId, userId, modelName);
        return sessionId;
    }

    std::shared_ptr<Session> SessionManager::getSession(const std::string &userId, const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 内存命中时也要校验归属：缓存里可能混有多个用户的会话
        auto it = m_sessions.find(sessionId);
        if (it != m_sessions.end())
        {
            if (it->second->userId != userId)
            {
                LOG_ERROR("Session {} does not belong to user {}", sessionId, userId);
                return nullptr;
            }
            return it->second;
        }

        // 内存未命中：回数据库加载（数据库侧已带 userId 过滤），并回填缓存
        auto session = m_db.getSession(userId, sessionId);
        if (!session)
        {
            return nullptr;
        }
        return m_sessions.emplace(sessionId, std::move(session)).first->second;
    }

    bool SessionManager::addMessage(const std::string &userId, const std::string &sessionId, const Message &message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end() || it->second->userId != userId)
        {
            LOG_ERROR("Session not found or not owned by user {}: {}", userId, sessionId);
            return false;
        }

        // 拷贝一份再填字段，messageId 和 timestamp 由 SessionManager 统一生成
        Message msg(message.role, message.content);
        msg.messageId = generateMessageId();
        msg.timestamp = std::time(nullptr);

        // 先落库（insertMessage 内部也会校验归属），失败则内存保持不变
        if (!m_db.insertMessage(userId, sessionId, msg))
        {
            LOG_ERROR("Failed to persist message for session: {}", sessionId);
            return false;
        }

        it->second->messages.push_back(msg);
        it->second->updateTime = msg.timestamp;
        LOG_INFO("Message added to session {}, user: {}, role: {}", sessionId, userId, msg.role);
        return true;
    }

    std::vector<Message> SessionManager::getSessionHistory(const std::string &userId, const std::string &sessionId)
    {
        // getSession 内部自己加锁，这里不能同时持锁调用，否则重复加锁会死锁
        auto session = getSession(userId, sessionId);
        if (!session)
        {
            LOG_ERROR("Session not found when getting history: {}", sessionId);
            return {};
        }

        // 重新加锁拷贝：getSession 返回后，其他线程可能还在往 messages 里追加
        std::lock_guard<std::mutex> lock(m_mutex);
        return session->messages;
    }

    std::vector<std::string> SessionManager::getSessionList(const std::string &userId) const
    {
        // 会话列表直接查库，保证返回的是该用户的完整、按更新时间排序的结果，
        // 而不是内存缓存里恰好加载过的那一部分
        return m_db.getAllSessionIds(userId);
    }

    bool SessionManager::deleteSession(const std::string &userId, const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 归属校验：即使内存里没有，也先确认该会话确实属于该用户（避免删到别人的会话）
        auto it = m_sessions.find(sessionId);
        if (it != m_sessions.end() && it->second->userId != userId)
        {
            LOG_ERROR("Session {} does not belong to user {}", sessionId, userId);
            return false;
        }

        // 先删库（外键级联删除该会话的所有消息），数据库侧会再校验一次归属
        if (!m_db.deleteSession(userId, sessionId))
        {
            LOG_ERROR("Failed to delete session from database: {}", sessionId);
            return false;
        }

        eraseFromCache(sessionId);
        LOG_INFO("Session deleted: {}, user: {}", sessionId, userId);
        return true;
    }

    void SessionManager::updateSessionTimestamp(const std::string &userId, const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end() || it->second->userId != userId)
        {
            LOG_ERROR("Session not found when updating timestamp: {}", sessionId);
            return;
        }

        std::time_t now = std::time(nullptr);
        it->second->updateTime = now;
        m_db.updateSessionTimestamp(userId, sessionId, now);
    }

    void SessionManager::clearAllSessions(const std::string &userId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 先清库，成功后再清内存缓存里属于该用户的会话
        if (!m_db.clearAllSessions(userId))
        {
            LOG_ERROR("Failed to clear sessions in database for user: {}", userId);
            return;
        }

        for (auto it = m_sessions.begin(); it != m_sessions.end();)
        {
            if (it->second->userId == userId)
            {
                it = m_sessions.erase(it);
            }
            else
            {
                ++it;
            }
        }
        LOG_INFO("All sessions cleared for user: {}", userId);
    }

    size_t SessionManager::getSessionCount(const std::string &userId) const
    {
        return m_db.getSessionCount(userId);
    }
}// namespace ai_chat_sdk
