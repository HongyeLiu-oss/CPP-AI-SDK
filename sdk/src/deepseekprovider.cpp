#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <jsoncpp/json/reader.h>
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

        // 去掉结尾的斜杠（httplib 0.10.x 对带尾斜杠的 base_url 解析有 bug，会导致 Connection 失败）
        while (!m_BaseURL.empty() && m_BaseURL.back() == '/')
            m_BaseURL.pop_back();

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
        // std::stod 和std::stoi 在解析失败时会抛异常，而不是返回错误码，需要进行错误处理
        try
        {
            if (params.find("temperature") != params.end())
                temperature = std::stod(params.at("temperature"));
            if (params.find("max_tokens") != params.end())
                max_tokens = std::stoi(params.at("max_tokens"));
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("DeepSeekProvider invalid params: {}", e.what());
            return "";
        }

        // 3. 检查消息列表是否为空
        // 如果messages是空的,messageArray就是[],发给 DeepSeek 会返回 400 Bad Request （API 要求至少有一条消息）。
        // 与其浪费一次网络请求等服务器报错，不如 在本地提前拦截 ，更快、更省资源、日志也更清晰
        if (messages.empty())
        {
            LOG_ERROR("DeepSeekProvider sendMessage failed, messages list is empty");
            return "";
        }

        // 4. 构造历史消息
        Json::Value messageArray(Json::arrayValue);
        for (auto &message: messages)
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
        std::string requestBodyStr = Json::writeString(writeBuilder, requestBody);
        LOG_INFO("DeepSeekProvider sendMessage request body: {}", requestBodyStr);
        
        // 7. 使用cpp-httplib库构造HTTP客户端
        httplib::Client client(m_BaseURL);
        client.set_connection_timeout(30, 0);     // 连接超时时间30秒
        client.set_read_timeout(60, 0);           // 读取超时时间60秒

        // 设置请求头
        httplib::Headers headers;
        headers.insert({"Authorization", "Bearer " + m_APIKey});
        headers.insert({"Content-Type",  "application/json"});
        headers.insert({"Accept",        "application/json"});
        headers.insert({"User-Agent",    "DeepSeekProvider/1.0"});

        // 8. 发送POST请求
        httplib::Result result = client.Post(
            "/chat/completions", headers, requestBodyStr, "application/json");
        // 检查响应是否为空
        if (!result)
        {
            std::ostringstream oss;
            oss << result.error();
            LOG_ERROR("DeepSeekProvider HTTP request failed: {}", oss.str());
            return "";
        }
        // 检查响应状态码
        if (result->status != 200)
        {
            LOG_ERROR("DeepSeekProvider HTTP {}: {}", result->status, result->body);
            return "";
        }
        // 打印状态码和body
        LOG_INFO("DeepSeekProvider HTTP response received, status: {}, body: {}",
         result->status, result->body);

        // 9. 解析响应体
        Json::Value responseBody;
        Json::CharReaderBuilder readBuilder;
        std::string parseError;
        std::istringstream responseStream(result->body);
        if (!Json::parseFromStream(readBuilder, responseStream, &responseBody, &parseError))
        {
            LOG_ERROR("DeepSeekProvider failed to parse response body: {}", parseError);
            return "";
        }

        // 安全取值：先判断字段是否存在
        if (responseBody.isMember("choices") 
        && responseBody["choices"].isArray()
        && responseBody["choices"].size() > 0
        && responseBody["choices"][0].isMember("message")
        && responseBody["choices"][0]["message"].isMember("content")
        && responseBody["choices"][0]["message"]["content"].isString())
        {   // .asString() 对非字符串类型 不会崩 ，但会默默返回空串。问题是：你无法区分"模型真的回复了空字符串"和"content 字段类型不对"——前者应该返回空串，后者应该报错。
            std::string content = responseBody["choices"][0]["message"]["content"].asString();
            LOG_INFO("DeepSeekProvider response text : {}", content);
            return content;
        }
        
        // 10. 响应格式不符合预期（JSON 解析成功，但结构里没有 choices/message/content）
        LOG_ERROR("DeepSeekProvider unexpected response format, body: {}", result->body);
        return "";
    }

    // 发送消息-期望模型流式返回
    std::string DeepSeekProvider::sendMessageStream(const std::vector<Message> &messages, 
        const std::map<std::string, std::string> &params, 
        std::function<void(const std::string &, StreamStatus)> callback)
    {
        // 1. 检查模型是否可用
        if (!isAvailable())
        {
            LOG_ERROR("DeepSeekProvider sendMessageStream failed, model is not available");
            return "";
        }

        // 2. 构造请求参数
        double temperature = 0.7;
        int max_tokens = 1024;
        // std::stod 和std::stoi 在解析失败时会抛异常，而不是返回错误码，需要进行错误处理
        try
        {
            if (params.find("temperature") != params.end())
                temperature = std::stod(params.at("temperature"));
            if (params.find("max_tokens") != params.end())
                max_tokens = std::stoi(params.at("max_tokens"));
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("DeepSeekProvider invalid params: {}", e.what());
            return "";
        }

        // 3. 检查消息列表是否为空
        // 如果messages是空的,messageArray就是[],发给 DeepSeek 会返回 400 Bad Request （API 要求至少有一条消息）。
        // 与其浪费一次网络请求等服务器报错，不如 在本地提前拦截 ，更快、更省资源、日志也更清晰
        if (messages.empty())
        {
            LOG_ERROR("DeepSeekProvider sendMessage failed, messages list is empty");
            return "";
        }

        // 4. 构造历史消息
        Json::Value messageArray(Json::arrayValue);
        for (auto &message: messages)
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
        std::string requestBodyStr = Json::writeString(writeBuilder, requestBody);
        LOG_INFO("DeepSeekProvider sendMessage request body: {}", requestBodyStr);
        
        // 7. 使用cpp-httplib库构造HTTP客户端
        httplib::Client client(m_BaseURL);
        client.set_connection_timeout(30, 0);     // 连接超时时间30秒
        client.set_read_timeout(300, 0);           // 流式响应需要更长的时间
        
        // 设置请求头
        httplib::Headers headers;
        headers.insert({"Authorization", "Bearer " + m_APIKey});
        headers.insert({"Content-Type",  "application/json"});
        headers.insert({"Accept",        "text/event-stream"});
        headers.insert({"User-Agent",    "DeepSeekProvider/1.0"});

        // 8. 定义流式变量
        std::string buffer;                 // 流式响应缓冲区
        bool getError = false;              // 标记响应是否成功
        std::string fullResponse;           // 积累完整响应内容

        // 9. 创建请求对象
        httplib::Request req;
        req.method = "POST";
        req.path = "/chat/completions";
        req.headers = headers;
        req.body = requestBodyStr;

        // 设置响应处理器 客户端收到响应之后应该如何处理
        req.response_handler = [&](const httplib::Response &res)
        {
            if (res.status != 200)
            {
                // 处理错误响应
                getError = true;
                LOG_ERROR("DeepSeekProvider sendMessageStream HTTP error, status: {}, body: {}", res.status, res.body);
                callback("", StreamStatus::Done);
                return false;
            }
            return true;
        };

        // 10. 设置数据接收处理器，用于解析流式响应的每个块
        req.content_receiver = [&](const char* data, size_t len, size_t offset, size_t totalLength)
        {
            // 验证响应头是否错误，如果错误就不需要再接受数据
            if (getError == true)
                return false;

            // 累加数据到缓冲区
            buffer.append(data, len);
            LOG_TRACE("DeepSeekProvider sendMessageStream buffer: {}", buffer);
            
            // 处理所有的流式响应的数据块，数据块之间以\n\n分隔
            size_t pos = 0;
            while ((pos = buffer.find("\n\n")) != std::string::npos) // 此时pos指向第一个\n的下标
            {
                // 截取当前找到的数据块
                std::string chunk = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                // 处理空行和注释，以:开头的是注释，需要跳过
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
                    Json::Value modelDataJson;
                    Json::CharReaderBuilder readBuilder;
                    std::string parseError;
                    std::istringstream modelDataStream(modelData); // Json::parseFromStream的接口要求传一个stream对象，而不是std::string，所以需要使用istringstream将字符串转换为流对象
                    if (Json::parseFromStream(readBuilder, modelDataStream, &modelDataJson, &parseError))
                    {
                        // 提取成功的Json数据就保存在modelDataJson中
                        // modelDataJson["choices"][0]["delta"]["content"].asString() = "你"
                        if (modelDataJson.isMember("choices") && 
                            modelDataJson["choices"].isArray() && 
                            !modelDataJson["choices"].empty() &&
                            modelDataJson["choices"][0].isMember("delta") &&
                            modelDataJson["choices"][0]["delta"].isMember("content"))
                            {
                                // 提取content字段，注意：jsoncpp对null值asString()返回空串
                                Json::Value contentVal = modelDataJson["choices"][0]["delta"]["content"];
                                if (!contentVal.isNull() && contentVal.isString())
                                {
                                    std::string content = contentVal.asString();
                                    fullResponse += content;
                                    callback(content, StreamStatus::InProgress);
                                }
                            }
                    }
                    else
                    {
                        LOG_ERROR("DeepSeekProvider sendMessageStream parseFromStream failed, error: {}", parseError);
                    }
                }

            }
            return true;
        };

        // 11. 发送请求（同步阻塞）
        // 注意：必须先在 req 上挂好 response_handler 和 content_receiver 两个回调，
        // 再调用 send()。send() 执行期间 httplib 才会在"收到响应头"和"收到 body 数据块"
        // 这两个时机分别触发上面的回调。如果先 send 再挂回调，请求已经发出去了，
        // 回调还没注册，就错过了所有通知。
        auto result = client.send(req);
        if (!result)
        {
            std::ostringstream oss;
            oss << result.error();
            LOG_ERROR("DeepSeekProvider sendMessageStream HTTP request failed: error={}, url={}", oss.str(), m_BaseURL + "/chat/completions");
            callback("", StreamStatus::Done);  
            return "";
        }
        return fullResponse;
    }
} // namespace ai_chat_sdk