#pragma once
#include "llmprovider.h"
#include "common.h"
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <functional>

namespace ai_chat_sdk
{
    class LLMManager
    {
    public:
        // 添加并初始化一个模型（注册 + init 原子化，init 失败则不注册）
        bool addModel(const std::string &modelName, std::shared_ptr<LLMProvider> provider,
            const std::map<std::string, std::string> &config);
        // 获取所有可用模型
        std::vector<ModelInfo> getAvailableModels() const;
        // 检查模型是否可用
        bool isModelAvailable(const std::string &modelName) const;
        // 发送消息给指定模型
        std::string sendMessage(const std::string &modelName, const std::vector<Message> &messages, 
            const std::map<std::string, std::string> &requestParams);
        // 发送消息流给指定模型
        std::string sendMessageStream(const std::string &modelName, const std::vector<Message> &messages, 
            const std::map<std::string, std::string> &requestParams, 
            std::function<void(const std::string &, StreamStatus)> callback);
    private:
        // 模型名称到提供者的映射
        std::map<std::string, std::shared_ptr<LLMProvider>> m_providers;
    };
}// namespace ai_chat_sdk
