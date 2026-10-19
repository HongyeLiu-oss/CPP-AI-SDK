#pragma once
#include "llmprovider.h"

namespace ai_chat_sdk
{
    class GLMProvider : public LLMProvider
    {
    public:
        // 初始化模型
        bool init(const std::map<std::string, std::string> &config) override;
        // 检测模型是否有效
        bool isAvailable() const override;
        // 获取模型名称
        std::string getModelName() const override;
        // 获取模型描述
        std::string getModelDescription() const override;
        // 发送消息-全量返回
        std::string sendMessage(const std::vector<Message> &messages, 
            const std::map<std::string, std::string> &params) override;
        // 发送消息-流式返回
        std::string sendMessageStream(const std::vector<Message> &messages, 
            const std::map<std::string, std::string> &params, 
            std::function<void(const std::string &, StreamStatus)> callback) override;
    };
} // namespace ai_chat_sdk
