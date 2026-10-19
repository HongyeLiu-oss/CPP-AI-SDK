#pragma once
#include <map>
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
            virtual bool init(const std::map<std::string, std::string> &config) = 0;
            // 检测模型是否有效
            virtual bool isAvailable() const = 0;
            // 获取模型名称
            virtual std::string getModelName() const = 0;
            // 获取模型描述
            virtual std::string getModelDescription() const = 0;
            // 发送消息-全量返回
            virtual std::string sendMessage(const std::vector<Message> &messages, 
                const std::map<std::string, std::string> &params) = 0;
            // 发送消息-流式返回
            virtual std::string sendMessageStream(const std::vector<Message> &messages, 
                const std::map<std::string, std::string> &params, 
                std::function<void(const std::string &, StreamStatus)> callback) = 0;
        protected:
            bool m_is_available = false;     // 模型是否有效
            std::string m_APIKey;            // API密钥
            std::string m_BaseURL;           // 模型API基础URL（scheme://host[:port]）
            std::string m_PathPrefix;        // API路径前缀（如 /api/paas/v4），httplib Client 不支持在 base_url 中带路径
    };
}
