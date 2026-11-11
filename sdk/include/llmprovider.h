#pragma once
#include <string>
#include <vector>
#include <functional>
#include "common.h"

namespace ai_chat_sdk
{
    enum class StreamStatus { InProgress, Done };
    class LLMProvider
    {
        public:
            // 初始化模型
            virtual bool init(const ApiModelConfig &config) = 0;
            // 检测模型是否有效
            virtual bool isAvailable() const = 0;
            // 获取模型名称（由 init 时传入的配置决定，全流程唯一来源）
            std::string getModelName() const
            {
                return m_modelName;
            }
            // 获取模型描述
            virtual std::string getModelDescription() const = 0;
            // 发送消息-全量返回
            virtual std::string sendMessage(const std::vector<Message> &messages, 
                const GenerationParams &params) = 0;
            // 发送消息-流式返回
            virtual std::string sendMessageStream(const std::vector<Message> &messages, 
                const GenerationParams &params, 
                std::function<void(const std::string &, StreamStatus)> callback) = 0;
        protected:
            bool m_isAvailable = false;      // 模型是否有效
            std::string m_modelName;         // 模型名称（注册 key、列表展示、请求体 model 字段共用）
            std::string m_apiKey;            // API密钥
            std::string m_baseUrl;           // 模型API基础URL（scheme://host[:port]）
            std::string m_pathPrefix;        // API路径前缀（如 /api/paas/v4），httplib Client 不支持在 baseUrl 中带路径
    };
}
