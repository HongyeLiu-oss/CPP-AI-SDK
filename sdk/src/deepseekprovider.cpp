#include "../include/deepseekprovider.h"
#include "../include/util/log.h"

namespace ai_chat_sdk
{
    // DeepSeekProvider类
    bool DeepSeekProvider::init(const std::map<std::string, std::string> &config)
    {
        // 初始化api_key
        if (config.find("api_key") == config.end())
        {
            LOG_ERROR("DeepSeekProvider api_key is not found in config");
            return false;
        }
        m_APIKey = config.at("api_key");

        // 初始化BaseURL
        if (config.find("base_url") == config.end())
        {
            LOG_ERROR("DeepSeekProvider base_url is not found in config");
            return false;
        }
        m_BaseURL = config.at("base_url");
        
        // 检查BaseURL是否以"/"结尾
        if (m_BaseURL.back() != '/')
            m_BaseURL += '/';
        
        // 初始化成功
        LOG_INFO("DeepSeekProvider init success, api_key: %s, base_url: %s", m_APIKey.c_str(), m_BaseURL.c_str());
        m_is_available = true;
        return true;
    }

    // 检测模型是否可用
    bool DeepSeekProvider::isAvailable() const
    {
        return m_is_available;
    }

    // 获取模型名称
    std::string DeepSeekProvider::getModelName() const
    {       
        return "deepseek-flash";
    }

    // 获取模型的描述信息 
    std::string DeepSeekProvider::getModelDescription() const
    {
        return "DeepSeek-Flash 是一款面向高效交互场景的大语言模型，注重响应速度与文本生成质量，支持智能问答、文本创作、内容总结、代码辅助及基础逻辑推理等任务。适用于智能聊天助手、日常办公和 AI 应用开发等场景，能够提供快速、流畅的交互体验。";
    }

    // 发送消息-期望模型全量返回
    std::string DeepSeekProvider::sendMessage(const std::vector<Message> &messages, 
        const std::map<std::string, std::string> &params)
    {
        return "";
    }

    // 发送消息-期望模型流式返回
    std::string DeepSeekProvider::sendMessageStream(const std::vector<Message> &messages, 
        const std::map<std::string, std::string> &params, 
        std::function<void(const std::string &, bool)> callback)
    {
        return "";
    }
} // namespace ai_chat_sdk