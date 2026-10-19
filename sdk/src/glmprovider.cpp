#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include "../include/glmprovider.h"
#include "../include/util/log.h"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <sstream>

namespace ai_chat_sdk
{
    // GLMProvider类实现
    // 初始化模型
    bool GLMProvider::init(const std::map<std::string, std::string> &config)
    {
        // 初始化api_key
        if (config.find("api_key") == config.end())
        {
            LOG_ERROR("GLMProvider api_key is not found in config");
            return false;
        }
        m_APIKey = config.at("api_key");

        // 初始化BaseURL
        if (config.find("base_url") == config.end())
        {
            LOG_ERROR("GLMProvider base_url is not found in config");
            return false;
        }

        // httplib 0.10.x 的 Client 只能接受 scheme://host[:port]，不能带路径
        m_BaseURL = config.at("base_url");
        // 去尾斜杠
        while (!m_BaseURL.empty() && m_BaseURL.back() == '/')
            m_BaseURL.pop_back();
        
        // 智谱所有模型共用的 API 版本前缀
        m_PathPrefix = "/api/paas/v4";

        // 初始化成功（key 掩码：只显示前4后4位，防止日志泄露）
        std::string maskedKey = (m_APIKey.size() > 8)
            ? m_APIKey.substr(0, 4) + "****" + m_APIKey.substr(m_APIKey.size() - 4)
            : "***";
        LOG_INFO("GLMProvider init success, api_key: {}, base_url: {}", maskedKey, m_BaseURL);
        m_is_available = true;
        return true;
    }

        // 检测模型是否可用
    bool GLMProvider::isAvailable() const
    {
        return m_is_available;
    }

    // 获取模型名称
    std::string GLMProvider::getModelName() const
    {       
        return "glm-5.3-flash";
    }

    // 获取模型的描述信息 
    std::string GLMProvider::getModelDescription() const
    {
        return "GLM-5.3-Flash/GLM-5.3-FlashX 是 GLM-5 系列首个原生多模态模型，以极致低成本架构实现超越 GLM-5.2 的更强智能能力。";
    }

    // 发送消息-期待模型全量返回
    std::string GLMProvider::sendMessage(const std::vector<Message> &messages, const std::map<std::string, std::string> &params)
    {
        // 1. 检查模型是否可用
        if (m_is_available == false)
        {
            LOG_ERROR("GLMProvider sendMessage failed, model is not available");
            return "";
        }

        // 2. 构造请求参数
        double temperature = 1.0;
        int max_tokens = 1024;
        try 
        {
            if (params.find("temperature") != params.end())
            {
                temperature = std::stod(params.at("temperature"));
            }
            if (params.find("max_tokens") != params.end())
            {
                max_tokens = std::stoi(params.at("max_tokens"));
            }
        }
        catch (const std::exception &e) 
        {
            LOG_ERROR("GLMProvider sendMessage failed, invalid parameter: {}", e.what());
            return "";
        }

        // 3. 检查消息列表是否为空
        if (messages.empty())
        {
            LOG_ERROR("GLMProvider sendMessage failed, messages list is empty");
            return "";
        }

        // 4. 构造历史消息
        Json::Value messageArray(Json::arrayValue);
        for (const auto &message: messages)
        {
            Json::Value messageObj(Json::objectValue);
            messageObj["role"] = message.role;
            messageObj["content"] = message.content;
            messageArray.append(messageObj);
        }

        // 5. 构造请求体
        Json::Value requestBody(Json::objectValue);
        requestBody["model"] = getModelName();
        requestBody["messages"] = messageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = max_tokens;
        requestBody["stream"] = false;

        // 6. 序列化
        Json::StreamWriterBuilder writeBuilder;
        writeBuilder["indentation"] = "";
        writeBuilder["emitUTF8"] = true;
        std::string requestBodyStr = Json::writeString(writeBuilder, requestBody);
        LOG_INFO("GLMProvider sendMessage request body: {}", requestBodyStr);

        // 7. 使用cpp-httplib库构造http客户端
        httplib::Client client(m_BaseURL);
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(300, 0);  // GLM-5.3-flash 强制深度思考，响应较慢

        // 设置请求头
        httplib::Headers headers;
        headers.insert({"Authorization", "Bearer " + m_APIKey});
        headers.insert({"Content-Type", "application/json"});
        headers.insert({"Accept", "application/json"});
        headers.insert({"User-Agent", "GLMProvider"});

        // 8. 发送POST请求
        std::string apiPath = m_PathPrefix + "/chat/completions";
        httplib::Result result = client.Post(apiPath.c_str(), headers, requestBodyStr, "application/json");
        
        // 检查响应是否为空
        if (!result)
        {
            std::ostringstream oss;
            oss << result.error();
            LOG_ERROR("GLMProvider HTTP request failed: {}", oss.str());
            return "";
        }

        // 检查响应状态码
        if (result->status != 200)
        {
            LOG_ERROR("GLMProvider HTTP {}: {}", result->status, result->body);
            return "";
        }

        // 打印状态码和body
        LOG_INFO("GLMProvider HTTP response received, status: {}, body: {}",
         result->status, result->body);
        
        // 9. 解析响应体
        Json::Value responseBody;
        Json::CharReaderBuilder readBuilder;
        std::string parseError;
        std::istringstream responseStream(result->body);
        if (!Json::parseFromStream(readBuilder, responseStream, &responseBody, &parseError))
        {
            LOG_ERROR("GLMProvider failed to parse response body: {}", parseError);
            return "";
        }

        // 安全取值，先判断字段是否存在
        // GLM-5.3 系列强制开启思考模式：
        //   message.reasoning_content = 思考过程
        //   message.content           = 最终答案
        // 当 content 为空时说明 max_tokens 太小，思考过程还没跑完
        if (responseBody.isMember("choices") && 
        responseBody["choices"].isArray() &&
        responseBody["choices"].size() > 0 && 
        responseBody["choices"][0].isMember("message"))
        {
            const Json::Value &msg = responseBody["choices"][0]["message"];
            // 优先取 content（最终答案），为空再回退到 reasoning_content（思考过程）
            if (msg.isMember("content") && msg["content"].isString())
            {
                std::string content = msg["content"].asString();
                if (!content.empty())
                {
                    LOG_INFO("GLMProvider response text : {}", content);
                    return content;
                }
            }
            if (msg.isMember("reasoning_content") && msg["reasoning_content"].isString())
            {
                std::string reasoning = msg["reasoning_content"].asString();
                if (!reasoning.empty())
                {
                    LOG_WARN("GLMProvider content is empty, falling back to reasoning_content");
                    LOG_INFO("GLMProvider reasoning text : {}", reasoning);
                    return reasoning;
                }
            }
        }

        // 10. 响应格式不符合预期（JSON 解析成功，但结构里没有 choices/message/content）
        LOG_ERROR("GLMProvider unexpected response format, body: {}", result->body);
        return "";
    }

    // 发送消息-期望模型流式返回
    std::string GLMProvider::sendMessageStream(const std::vector<Message> &messages, 
        const std::map<std::string, std::string> &params, 
        std::function<void(const std::string &, StreamStatus)> callback)
    {
        return "";
    }

} // namespace ai_chat_sdk
