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

    // SessionManager 职责：管理会话与消息的生命周期
    // 内存中的 m_sessions 是热点缓存，所有写操作同步落到 DataManager（SQLite）
    // 构造时会把数据库里的历史会话恢复回内存，所以进程重启后会话不丢
    class SessionManager
    {
    public:
        // 构造时从数据库恢复全部历史会话（含消息）
        explicit SessionManager(DataManager &db);

        // 创建新会话：先落库，成功才写入内存；modelName：模型名称，返回会话ID；失败返回空串
        std::string createSession(const std::string &modelName);
        // 通过会话ID获取会话；内存没有再回数据库加载，两边都没有返回 nullptr
        std::shared_ptr<Session> getSession(const std::string &sessionId);
        // 添加消息到会话：先落库，成功才写入内存；会话不存在或落库失败返回 false
        bool addMessage(const std::string &sessionId, const Message &message);
        // 获取会话历史消息列表；失败返回空 vector
        std::vector<Message> getSessionHistory(const std::string &sessionId);
        // 获取所有会话ID列表，按 updateTime 降序（最近聊过的在前）
        std::vector<std::string> getSessionList() const;
        // 删除会话：先删库（级联删消息），成功才删内存；会话不存在返回 false
        bool deleteSession(const std::string &sessionId);
        // 更新会话的 updateTime 为当前时间戳，同时落库
        void updateSessionTimestamp(const std::string &sessionId);
        // 清空所有会话（内存 + 数据库）
        void clearAllSessions();
        // 获取当前会话总数
        size_t getSessionCount() const;
    private:
        // 生成唯一会话ID
        std::string generateSessionId();
        // 生成唯一消息ID
        std::string generateMessageId();
    private:
        DataManager &m_db;
        std::unordered_map<std::string, std::shared_ptr<Session>> m_sessions;
        mutable std::mutex m_mutex;
        // 两个 static 原子计数器，跨 SessionManager 实例也不会撞号
        static std::atomic<int64_t> s_messageCounter;
        static std::atomic<int64_t> s_sessionCounter;
    };
}