#include "../include/llmmanager.h"
#include "../include/util/log.h"
#include "../include/common.h"

namespace ai_chat_sdk
{
    bool LLMManager::addModel(const std::shared_ptr<LLMProvider> &provider, const ApiModelConfig &config)
    {
        // 检查模型名是否为空
        if (config.modelName.empty())
        {
            LOG_ERROR("Model name is empty");
            return false;
        }
        // 检查 provider 是否为空
        if (!provider)
        {
            LOG_ERROR("Provider is null for model: {}", config.modelName);
            return false;
        }
        // 检查模型是否已存在
        if (m_providers.find(config.modelName) != m_providers.end())
        {
            LOG_ERROR("Model already registered: {}", config.modelName);
            return false;
        }
        // 初始化 provider；init 失败则不注册
        if (!provider->init(config))
        {
            LOG_ERROR("Model initialization failed: {}", config.modelName);
            return false;
        }
        // init 成功才存入，实现原子化注册
        m_providers.emplace(config.modelName, provider);
        LOG_INFO("Model added: {}", config.modelName);
        return true;
    }

    std::vector<ModelInfo> LLMManager::getAvailableModels() const
    {
        std::vector<ModelInfo> availableModels;
        for (const auto &it : m_providers)
        {
            if (it.second->isAvailable())
            {
                availableModels.emplace_back(it.second->getModelName());
                auto &info = availableModels.back();
                info.description = it.second->getModelDescription();
                info.isAvailable = true;
            }
        }
        return availableModels;
    }

    bool LLMManager::isModelAvailable(const std::string &modelName) const
    {
        auto it = m_providers.find(modelName);
        return it != m_providers.end() && it->second->isAvailable();
    }

    std::string LLMManager::sendMessage(const std::string &modelName, const std::vector<Message> &messages, 
        const GenerationParams &params)
    {
        auto it = m_providers.find(modelName);
        if (it == m_providers.end())
        {
            LOG_ERROR("Model not registered: {}", modelName);
            return "";
        }
        if (!it->second->isAvailable())
        {
            LOG_ERROR("Model not available: {}", modelName);
            return "";
        }
        return it->second->sendMessage(messages, params);
    }

    std::string LLMManager::sendMessageStream(const std::string &modelName, const std::vector<Message> &messages, 
        const GenerationParams &params, 
        std::function<void(const std::string &, StreamStatus)> callback)
    {
        auto it = m_providers.find(modelName);
        if (it == m_providers.end())
        {
            LOG_ERROR("Model not registered: {}", modelName);
            return "";
        }
        if (!it->second->isAvailable())
        {
            LOG_ERROR("Model not available: {}", modelName);
            return "";
        }
        return it->second->sendMessageStream(messages, params, callback);
    }
}// namespace ai_chat_sdk
