#pragma once

#include <string>
#include <ctime>
#include <vector>

namespace ai_chat_sdk
{
    // 消息结构
    struct Message
    {
        std::string messageId;        // 消息ID
        std::string role;             // 角色
        std::string content;          // 内容
        std::time_t timestamp;        // 消息发送时间戳

        // 构造函数
        Message(const std::string &role_, const std::string &content_)
        :role(role_)
        ,content(content_)
        {}
    };

    // 模型的公共配置信息
    struct ModelConfig
    {
        std::string modelName;        // 模型名称
        double temperature;           // 温度参数： 控制输出的随机性，值越小越确定，值越大越随机
        int maxTokens;                // 最大输出token数
    };

    // 通过API方式接入云端模型
    struct ApiModelConfig : public ModelConfig
    {
        std::string apiKey;           // API密钥
    };

    // LLM 信息
    struct ModelInfo
    {
        std::string modelName;        // 模型名称
        std::string description;      // 模型描述
        std::string provider;         // 模型提供方
        std::string endpoint;         // 模型base url 或 endpoint
        bool isAvailable = false;     // 模型是否可用

        // 构造函数
        ModelInfo(const std::string &modelName_, const std::string &description_, const std::string &provider_, const std::string &endpoint_)
        :modelName(modelName_)
        ,description(description_)
        ,provider(provider_)
        ,endpoint(endpoint_)
        {}
    };



    // 通过Ollama方式接入本地模型
    struct OllamaModelConfig : public ModelConfig
    {
        std::string ollamaUrl;        // Ollama地址
    };

    // 会话信息
    struct Session
    {
        std::string sessionId;         // 会话ID
        std::string modelName;         // 模型名称
        std::vector<Message> messages; // 会话中的消息列表
        std::time_t createTime;        // 会话创建时间戳
        std::time_t updateTime;        // 会话更新时间戳

        // 构造函数
        Session(const std::string &modelName_)
        :modelName(modelName_)
        {}
    };
}