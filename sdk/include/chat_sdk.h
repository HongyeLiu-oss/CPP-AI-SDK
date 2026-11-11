#pragma once

#include "common.h"
#include "datamanager.h"
#include "llmmanager.h"
#include "sessionmanager.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ai_chat_sdk
{
    // ChatSDK：对外门面
    // 把 LLMManager（模型路由）、SessionManager（会话）、DataManager（持久化）编排在一起，
    // 调用方只用这一个类，不需要关心 SQL 和具体 provider。
    class ChatSDK
    {
    public:
        // dbName：SQLite 文件路径，文件不存在会自动创建
        explicit ChatSDK(const std::string &dbName = "chat.db");

        // 注册并初始化一批模型；任一模型失败返回 false（已成功的模型仍然可用）
        bool initModels(const std::vector<ApiModelConfig> &configs);
        // 获取所有可用模型
        std::vector<ModelInfo> getAvailableModels() const;

        // 创建会话；模型不可用时返回空串
        std::string createSession(const std::string &modelName);
        // 获取会话；不存在返回 nullptr
        std::shared_ptr<Session> getSession(const std::string &sessionId);
        // 获取会话列表，按最近更新时间降序
        std::vector<std::string> getSessionList() const;
        // 获取会话历史消息
        std::vector<Message> getSessionHistory(const std::string &sessionId);
        // 删除会话（连带删除该会话的全部消息）
        bool deleteSession(const std::string &sessionId);

        // 发送消息，返回模型完整回复；会话不存在或模型失败返回空串
        std::string sendMessage(const std::string &sessionId, const std::string &content);
        // 发送消息-流式：回复通过 callback 增量推送，返回值同样是完整回复
        std::string sendMessageStream(const std::string &sessionId, const std::string &content,
            std::function<void(const std::string &, StreamStatus)> callback);

    private:
        // 取某个模型配置的生成参数，没有配置时返回默认值
        GenerationParams paramsFor(const std::string &modelName) const;

    private:
        // 声明顺序决定析构的逆序：SessionManager 先析构，DataManager 最后关闭连接
        DataManager m_dataManager;
        SessionManager m_sessionManager;
        LLMManager m_llmManager;
        std::map<std::string, GenerationParams> m_generationParams;
    };
}// namespace ai_chat_sdk
