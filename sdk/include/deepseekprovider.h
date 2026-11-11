#pragma once
#include "llmprovider.h"

namespace ai_chat_sdk
{
    class DeepSeekProvider : public LLMProvider
    {
    public:
        // 初始化模型
        bool init(const ApiModelConfig &config) override;
        // 检测模型是否有效
        bool isAvailable() const override;
        // 获取模型描述
        std::string getModelDescription() const override;
        // 发送消息-全量返回
        std::string sendMessage(const std::vector<Message> &messages, 
            const GenerationParams &params) override;
        // 发送消息-流式返回
        std::string sendMessageStream(const std::vector<Message> &messages, 
            const GenerationParams &params, 
            std::function<void(const std::string &, StreamStatus)> callback) override;
    };
} // namespace ai_chat_sdk
