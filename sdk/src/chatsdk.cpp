#include "../include/chat_sdk.h"
#include "../include/deepseekprovider.h"
#include "../include/glmprovider.h"
#include "../include/util/log.h"

namespace ai_chat_sdk
{
    namespace
    {
        // 根据配置里的 provider 标识创建具体 provider。
        // 调用方（ChatServer）只认字符串，不需要 include 任何 provider 头文件。
        std::shared_ptr<LLMProvider> createProvider(const std::string &provider)
        {
            if (provider == "deepseek")
            {
                return std::make_shared<DeepSeekProvider>();
            }
            if (provider == "glm")
            {
                return std::make_shared<GLMProvider>();
            }
            return nullptr;
        }
    }

    ChatSDK::ChatSDK(const std::string &dbName)
        : m_dataManager(dbName)
        , m_sessionManager(m_dataManager)
        , m_authManager(m_dataManager)
    {
        if (!m_dataManager.isOpen())
        {
            LOG_ERROR("ChatSDK started without database, persistence is unavailable");
        }
    }

    bool ChatSDK::initModels(const std::vector<ApiModelConfig> &configs)
    {
        bool allOk = true;
        for (const auto &config : configs)
        {
            auto provider = createProvider(config.provider);
            if (!provider)
            {
                LOG_ERROR("Unknown provider: {}, model: {}", config.provider, config.modelName);
                allOk = false;
                continue;
            }

            if (!m_llmManager.addModel(provider, config))
            {
                allOk = false;
                continue;
            }

            // 记住该模型的生成参数，发送消息时使用；未配置的模型走默认值
            m_generationParams.emplace(config.modelName, config);
        }
        return allOk;
    }

    std::vector<ModelInfo> ChatSDK::getAvailableModels() const
    {
        return m_llmManager.getAvailableModels();
    }

    // ==================== 用户认证 ====================

    bool ChatSDK::registerUser(const std::string &username, const std::string &password)
    {
        return m_authManager.registerUser(username, password);
    }

    std::string ChatSDK::loginUser(const std::string &username, const std::string &password)
    {
        return m_authManager.loginUser(username, password);
    }

    bool ChatSDK::logout(const std::string &token)
    {
        return m_authManager.logout(token);
    }

    std::string ChatSDK::getUserIdByToken(const std::string &token) const
    {
        return m_authManager.getUserIdByToken(token);
    }

    // ==================== 会话操作 ====================

    std::string ChatSDK::createSession(const std::string &userId, const std::string &modelName)
    {
        if (!m_llmManager.isModelAvailable(modelName))
        {
            LOG_ERROR("Model not available when creating session: {}", modelName);
            return "";
        }
        return m_sessionManager.createSession(userId, modelName);
    }

    std::shared_ptr<Session> ChatSDK::getSession(const std::string &userId, const std::string &sessionId)
    {
        return m_sessionManager.getSession(userId, sessionId);
    }

    std::vector<std::string> ChatSDK::getSessionList(const std::string &userId) const
    {
        return m_sessionManager.getSessionList(userId);
    }

    std::vector<Message> ChatSDK::getSessionHistory(const std::string &userId, const std::string &sessionId)
    {
        return m_sessionManager.getSessionHistory(userId, sessionId);
    }

    bool ChatSDK::deleteSession(const std::string &userId, const std::string &sessionId)
    {
        return m_sessionManager.deleteSession(userId, sessionId);
    }

    std::string ChatSDK::sendMessage(const std::string &userId, const std::string &sessionId, const std::string &content)
    {
        auto session = m_sessionManager.getSession(userId, sessionId);
        if (!session)
        {
            LOG_ERROR("Session not found when sending message: {}", sessionId);
            return "";
        }
        if (content.empty())
        {
            LOG_ERROR("Message content is empty, session: {}", sessionId);
            return "";
        }

        // 上下文 = 历史消息 + 本次用户消息。
        // 本次消息此时还未落库，模型调用失败就不会在历史里留下孤立消息。
        std::vector<Message> context = m_sessionManager.getSessionHistory(userId, sessionId);
        context.emplace_back("user", content);

        const std::string reply =
            m_llmManager.sendMessage(session->modelName, context, paramsFor(session->modelName));
        if (reply.empty())
        {
            LOG_ERROR("Model returned empty response, session: {}, model: {}",
                      sessionId, session->modelName);
            return "";
        }

        // 成功后把用户消息和模型回复一起写入，保证历史成对出现
        if (!m_sessionManager.addMessage(userId, sessionId, Message("user", content)) ||
            !m_sessionManager.addMessage(userId, sessionId, Message("assistant", reply)))
        {
            LOG_ERROR("Failed to persist conversation for session: {}", sessionId);
            return "";
        }

        LOG_INFO("Message processed, session: {}, reply size: {}", sessionId, reply.size());
        return reply;
    }

    std::string ChatSDK::sendMessageStream(const std::string &userId, const std::string &sessionId, const std::string &content,
        std::function<void(const std::string &, StreamStatus)> callback)
    {
        auto session = m_sessionManager.getSession(userId, sessionId);
        if (!session)
        {
            LOG_ERROR("Session not found when sending message: {}", sessionId);
            return "";
        }
        if (content.empty())
        {
            LOG_ERROR("Message content is empty, session: {}", sessionId);
            return "";
        }
        if (!callback)
        {
            LOG_ERROR("Stream callback is empty, session: {}", sessionId);
            return "";
        }

        std::vector<Message> context = m_sessionManager.getSessionHistory(userId, sessionId);
        context.emplace_back("user", content);

        const std::string reply = m_llmManager.sendMessageStream(
            session->modelName, context, paramsFor(session->modelName), callback);
        if (reply.empty())
        {
            LOG_ERROR("Model returned empty response, session: {}, model: {}",
                      sessionId, session->modelName);
            return "";
        }

        // 流式推送完之后再整体落库，避免把半截回复写进历史
        if (!m_sessionManager.addMessage(userId, sessionId, Message("user", content)) ||
            !m_sessionManager.addMessage(userId, sessionId, Message("assistant", reply)))
        {
            LOG_ERROR("Failed to persist conversation for session: {}", sessionId);
            return "";
        }

        LOG_INFO("Stream message processed, session: {}, reply size: {}", sessionId, reply.size());
        return reply;
    }

    GenerationParams ChatSDK::paramsFor(const std::string &modelName) const
    {
        auto it = m_generationParams.find(modelName);
        if (it == m_generationParams.end())
        {
            return GenerationParams{};
        }
        return it->second;
    }
}// namespace ai_chat_sdk
