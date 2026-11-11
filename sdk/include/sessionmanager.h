#pragma once
#include "common.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>

namespace ai_chat_sdk
{
    class DataManager;

    // SessionManager 职责：管理会话与消息的生命周期，并按 userId 做数据隔离
    // 每个公共方法都接收 userId，用于校验会话归属，保证"用户只能访问自己的会话"
    // m_sessions 是已加载会话的热缓存；会话列表与归属校验的真相在 DataManager(SQLite)
    class SessionManager
    {
    public:
        // 构造时只持有数据库引用，不再全量恢复（多用户场景下无法预知要加载谁）
        explicit SessionManager(DataManager &db);

        // 创建新会话：先落库，成功才写入内存；返回会话ID，失败返回空串
        std::string createSession(const std::string &userId, const std::string &modelName);
        // 通过 userId + 会话ID 获取会话；归属不符或不存在返回 nullptr
        std::shared_ptr<Session> getSession(const std::string &userId, const std::string &sessionId);
        // 添加消息到会话：校验归属后先落库，成功才写入内存；失败返回 false
        bool addMessage(const std::string &userId, const std::string &sessionId, const Message &message);
        // 获取会话历史消息列表；失败或归属不符返回空 vector
        std::vector<Message> getSessionHistory(const std::string &userId, const std::string &sessionId);
        // 获取某用户的会话ID列表，按 updateTime 降序（直接查库，保证完整）
        std::vector<std::string> getSessionList(const std::string &userId) const;
        // 删除会话：校验归属后先删库（级联删消息），成功才删内存；失败返回 false
        bool deleteSession(const std::string &userId, const std::string &sessionId);
        // 更新会话的 updateTime 为当前时间戳，同时落库
        void updateSessionTimestamp(const std::string &userId, const std::string &sessionId);
        // 清空某用户的所有会话（内存缓存 + 数据库）
        void clearAllSessions(const std::string &userId);
        // 获取某用户的会话总数（查库）
        size_t getSessionCount(const std::string &userId) const;
    private:
        // 生成唯一会话ID
        std::string generateSessionId();
        // 生成唯一消息ID
        std::string generateMessageId();
        // 从内存缓存删除某会话（不触碰数据库）
        void eraseFromCache(const std::string &sessionId);
    private:
        DataManager &m_db;
        std::unordered_map<std::string, std::shared_ptr<Session>> m_sessions;
        mutable std::mutex m_mutex;
        // 两个 static 原子计数器，跨 SessionManager 实例也不会撞号
        static std::atomic<int64_t> s_messageCounter;
        static std::atomic<int64_t> s_sessionCounter;
    };
}
