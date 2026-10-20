#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include "../include/glmprovider.h"
#include "../include/util/log.h"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/writer.h>
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
        requestBody["stream"] = true;

        // 6. 序列化
        Json::StreamWriterBuilder writeBuilder;
        writeBuilder["indentation"] = "";
        writeBuilder["emitUTF8"] = true;
        std::string requestBodyStr = Json::writeString(writeBuilder, requestBody);
        LOG_INFO("GLMProvider sendMessage request body : {}", requestBodyStr);

        // 7. 使用cpphttp-lib构建客户端
        httplib::Client client(m_BaseURL);
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(600, 0);

        // 设置请求头
        httplib::Headers headers;
        headers.insert({"Content-Type", "application/json"});
        headers.insert({"Authorization", "Bearer " + m_APIKey});
        headers.insert({"Accept", "text/event-stream"});
        headers.insert({"User-Agent",    "GLMProvider/1.0"});

        // 8. 定义流式变量
        std::string buffer;                 // 流式响应缓冲区
        bool getError = false;              // 标记响应是否成功
        std::string fullResponse;           // 积累完整响应内容

        // 9. 创建请求对象
        httplib::Request req;
        req.method = "POST";
        req.headers = headers;
        req.body = requestBodyStr;
        req.path = (m_PathPrefix + "/chat/completions").c_str();

        // 设置响应处理器-让客户端收到响应之后知道如何处理
        req.response_handler = [&](const httplib::Response &res)
        {
            if (res.status != 200)
            {
                getError = true;
                LOG_ERROR("GLMProvider sendMessageStream HTTP error, status: {}, body: {}", res.status, res.body);
                callback("", StreamStatus::Done);
                return false;
            }
            return true;
        };

        // 10. 设置数据接收处理器，用于解析流式响应的每个块
        req.content_receiver = [&](const char* data, size_t len, size_t offset, size_t totalLength)
        {
            // 验证响应头是否错误，如果错误就不需要再接受数据
            // 如果 response_handler 已经判定 HTTP 状态码非 200，这里立即终止接收
            // 注意：不再重复调用 callback("", Done)，因为 response_handler 中已经通知过上层
            // 此处的 return false 仅用于告诉 httplib 停止后续的 content_receiver 回调
            if (getError == true)
                return false;

            // 累加数据到缓冲区
            buffer.append(data, len);
            LOG_TRACE("GLMProvider sendMessageStream buffer: {}", buffer);

            // 处理所有的流式响应的数据块，数据块之间以\n\n分隔
            size_t pos = 0;
            while ((pos = buffer.find("\n\n")) != std::string::npos)
            {
                // 截取当前找到的数据块
                std::string chunk = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                // 处理空行和注释
                if (chunk.empty() || chunk[0] == ':')
                    continue;

                // 获取模型返回的有效数据
                if (chunk.compare(0, 6, "data: ") == 0)
                {
                    std::string modelData = chunk.substr(6); // 从下标6截取到字符串末尾
                    // 检测是否为结束标记 data: [DONE]
                    if (modelData == "[DONE]")
                    {
                        callback("", StreamStatus::Done);
                        return true;
                    }

                    // 反序列化
                    Json::CharReaderBuilder readBuilder;
                    Json::Value modelDataJson;
                    std::string parseError;
                    std::istringstream modelDataStream(modelData);
                    // 注意：去掉 ! 号，解析成功才进入下面的取值逻辑
                    if (Json::parseFromStream(readBuilder, modelDataStream, &modelDataJson, &parseError))
                    {
                        // 流式响应 choices[0] 里是 delta（不是 message）
                        // GLM-5.3 流式 chunk 结构：
                        //   delta.content            = 最终答案增量
                        //   delta.reasoning_content  = 思考过程增量
                        if (modelDataJson.isMember("choices") && 
                        modelDataJson["choices"].isArray() &&
                        !modelDataJson["choices"].empty() &&
                        modelDataJson["choices"][0].isMember("delta"))
                        {
                            const Json::Value &delta = modelDataJson["choices"][0]["delta"];
                            // 取 content 增量，非空则回调上层（每次 chunk 都推进打字机效果）
                            if (delta.isMember("content") && delta["content"].isString())
                            {
                                std::string content = delta["content"].asString();
                                if (!content.empty())
                                {
                                    fullResponse += content;
                                    callback(content, StreamStatus::InProgress);
                                }
                            }
                            // reasoning_content 是思考过程增量，只做日志记录
                            if (delta.isMember("reasoning_content") && delta["reasoning_content"].isString())
                            {
                                std::string reasoning = delta["reasoning_content"].asString();
                                if (!reasoning.empty())
                                {
                                    LOG_TRACE("GLMProvider reasoning delta: {}", reasoning);
                                }
                            }
                        }
                    }
                    else
                    {
                        LOG_ERROR("GLMProvider sendMessageStream parseFromStream failed, error: {}", parseError);
                    }
                }
            }
            return true;
        };

        // 11. 发送请求（同步阻塞）
        // send() 会阻塞直到整个 HTTP 交互完成（包括所有 SSE chunk 都接收完毕）。
        // 期间 httplib 会在合适时机回调 response_handler 和 content_receiver。
        // 注意：必须先在 req 上挂好两个回调，再调用 send()，否则回调会错过触发时机。
        auto result = client.send(req);

        // !result 表示 HTTP 请求在底层就失败了，连 Response 对象都没拿到。
        // 典型场景：DNS 解析失败、TCP 连接超时、TLS 握手失败、网络闪断、read_timeout 到期。
        // 这种情况下 response_handler 和 content_receiver 可能一次都没被调用过，
        // 所以必须在这里补一个 Done 通知，避免上层永远卡在等待状态。
        if (!result)
        {
            std::ostringstream oss;
            oss << result.error();  // httplib::Error 没有 .c_str()，需通过 operator<< 转可读字符串
            LOG_ERROR("GLMProvider sendMessageStream HTTP request failed: error={}, url={}{}/chat/completions", oss.str(), m_BaseURL, m_PathPrefix);
            callback("", StreamStatus::Done);  
            return "";
        }

        // 请求正常结束（无论走了 [DONE] 还是服务器主动断开），返回累计的完整响应
        return fullResponse;
    }

} // namespace ai_chat_sdk
