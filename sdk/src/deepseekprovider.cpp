#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../include/deepseekprovider.h"
#include "../include/util/log.h"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <sstream>

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
        
        // 初始化成功（key 掩码：只显示前4后4位，防止日志泄露）
        std::string maskedKey = (m_APIKey.size() > 8)
            ? m_APIKey.substr(0, 4) + "****" + m_APIKey.substr(m_APIKey.size() - 4)
            : "***";
        LOG_INFO("DeepSeekProvider init success, api_key: {}, base_url: {}", maskedKey, m_BaseURL);
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
        // 1. 检查模型是否可用
        if (m_is_available == false)
        {
            LOG_ERROR("DeepSeekProvider sendMessage failed, model is not available");
            return "";
        }

        // 2. 构造请求参数
        double temperature = 0.7;
        int max_tokens = 1024;
        if (params.find("temperature") != params.end())
            temperature = std::stod(params.at("temperature"));
        if (params.find("max_tokens") != params.end())
            max_tokens = std::stoi(params.at("max_tokens"));

        // 3. 构造历史消息
        Json::Value messageArray(Json::arrayValue);
        for (auto &message: messages)
        {
            Json::Value messageObj(Json::objectValue);
            messageObj["role"] = message.role;
            messageObj["content"] = message.content;
            messageArray.append(messageObj);
        }

        // 4. 构造请求体
        Json::Value requestBody(Json::objectValue);
        requestBody["model"] = getModelName();
        requestBody["messages"] = messageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = max_tokens;
        requestBody["stream"] = false;

        // 5. 序列化
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string requestBodyStr = Json::writeString(builder, requestBody);
        LOG_INFO("DeepSeekProvider sendMessage request body: {}", requestBodyStr);
        
        // 6. 使用cpp-httplib库构造HTTP客户端
        httplib::Client client(m_BaseURL);
        client.set_connection_timeout(30, 0);     // 连接超时时间30秒
        client.set_read_timeout(60, 0);           // 读取超时时间60秒

        // 设置请求头
        httplib::Headers headers;
        headers.insert({"Authorization", "Bearer " + m_APIKey});
        headers.insert({"Content-Type",  "application/json"});
        headers.insert({"Accept",        "application/json"});
        headers.insert({"User-Agent",    "DeepSeekProvider/1.0"});

        // 7. 发送POST请求
        httplib::Result result = client.Post(
            "/chat/completions", headers, requestBodyStr, "application/json");
        // 检查响应是否为空
        if (!result)
        {
            std::ostringstream oss;
            oss << result.error();
            LOG_ERROR("DeepSeekProvider sendMessage failed, error: {}", oss.str());
            return "";
        }
        // 检查响应状态码
        if (result->status != 200)
        {
            LOG_ERROR("DeepSeekProvider HTTP {}: {}", result->status, result->body);
            return "";
        }
        // 打印状态码和body
        LOG_INFO("DeepSeekProvider sendMessage success, status: {}, body: {}",
                 result->status, result->body);
        return result->body;
    
    }

    // 发送消息-期望模型流式返回
    std::string DeepSeekProvider::sendMessageStream(const std::vector<Message> &messages, 
        const std::map<std::string, std::string> &params, 
        std::function<void(const std::string &, bool)> callback)
    {
        return "";
    }
} // namespace ai_chat_sdk