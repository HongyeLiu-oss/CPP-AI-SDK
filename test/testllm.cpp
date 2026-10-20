#include "../sdk/include/deepseekprovider.h"
#include "../sdk/include/glmprovider.h"
#include "../sdk/include/util/log.h"
#include <cstdlib>
#include <gtest/gtest.h>

// TEST(DeepSeekProviderTest, sendMessage)
// {
//     std::shared_ptr<ai_chat_sdk::DeepSeekProvider> provider = std::make_shared<ai_chat_sdk::DeepSeekProvider>();
//     ASSERT_TRUE(provider);

//     std::map<std::string, std::string> modelParams;
//     modelParams["api_key"] = std::getenv("DEEPSEEK_API_KEY");
//     modelParams["base_url"] = std::getenv("DEEPSEEK_BASE_URL");
    
//     provider->init(modelParams);
//     ASSERT_TRUE(provider->isAvailable());

//     std::map<std::string, std::string> requestParams = { {"temperature", "0.7"}, {"max_tokens", "1024"} };
//     std::vector<ai_chat_sdk::Message> messages;
//     messages.push_back({"user", "你是什么模型?"});

//     std::string response = provider->sendMessage(messages, requestParams);
//     ASSERT_FALSE(response.empty());
// }

// TEST(DeepSeekProviderTest, sendMessageStream)
// {
//     std::shared_ptr<ai_chat_sdk::DeepSeekProvider> provider = std::make_shared<ai_chat_sdk::DeepSeekProvider>();
//     ASSERT_TRUE(provider);

//     std::map<std::string, std::string> modelParams;
//     modelParams["api_key"] = std::getenv("DEEPSEEK_API_KEY");
//     modelParams["base_url"] = std::getenv("DEEPSEEK_BASE_URL");
    
//     provider->init(modelParams);
//     ASSERT_TRUE(provider->isAvailable());

//     std::map<std::string, std::string> requestParams = { {"temperature", "0.7"}, {"max_tokens", "256"} };
//     std::vector<ai_chat_sdk::Message> messages;
//     messages.push_back({"user", "用一句话介绍你自己"});

//     // callback 会写入这三个局部变量（按引用捕获）
//     std::string accumulated;       // 累积所有 InProgress 的 content
//     bool streamDone = false;       // 是否收到 Done 信号
//     int inProgressCount = 0;       // InProgress 被调用次数

//     auto callback = [&](const std::string &content, ai_chat_sdk::StreamStatus status) {
//         if (status == ai_chat_sdk::StreamStatus::InProgress)
//         {
//             accumulated += content;
//             inProgressCount++;
//             std::cout << "[chunk] " << content << std::flush;
//         }
//         else if (status == ai_chat_sdk::StreamStatus::Done)
//         {
//             streamDone = true;
//             std::cout << "\n[Done]\n";
//         }
//     };

//     // sendMessageStream 是同步阻塞的，返回时流已经走完了
//     std::string fullResponse = provider->sendMessageStream(messages, requestParams, callback);

//     // 断言 1：流必须被标记为结束
//     ASSERT_TRUE(streamDone) << "callback 未收到 Done 信号";

//     // 断言 2：至少收到过一次增量
//     ASSERT_GT(inProgressCount, 0) << "callback(InProgress) 一次都没调";

//     // 断言 3：累积内容不为空
//     ASSERT_FALSE(accumulated.empty()) << "callback 累积的内容为空";

//     // 断言 4：返回值等于累积内容
//     EXPECT_EQ(fullResponse, accumulated) << "sendMessageStream 返回值和 callback 累积内容不一致";

//     // 打印最终完整回复
//     std::cout << "\n===== 最终完整回复 =====\n" << fullResponse << "\n========================\n";
// }

// TEST(GLMProviderTest, sendMessage)
// {
//     std::shared_ptr<ai_chat_sdk::GLMProvider> provider = std::make_shared<ai_chat_sdk::GLMProvider>();
//     ASSERT_TRUE(provider);

//     std::map<std::string, std::string> modelParams;
//     modelParams["api_key"] = std::getenv("ZHIPU_API_KEY");
//     modelParams["base_url"] = std::getenv("ZHIPU_BASE_URL");
    
//     provider->init(modelParams);
//     ASSERT_TRUE(provider->isAvailable());

//     std::map<std::string, std::string> requestParams = { {"temperature", "1.0"}, {"max_tokens", "8192"} };
//     std::vector<ai_chat_sdk::Message> messages;
//     messages.push_back({"user", "你是什么模型?"});

//     std::string response = provider->sendMessage(messages, requestParams);
//     ASSERT_FALSE(response.empty());
// }

TEST(GLMProviderTest, sendMessageStream)
{
    std::shared_ptr<ai_chat_sdk::GLMProvider> provider = std::make_shared<ai_chat_sdk::GLMProvider>();
    ASSERT_TRUE(provider);

    std::map<std::string, std::string> modelParams;
    modelParams["api_key"] = std::getenv("ZHIPU_API_KEY");
    modelParams["base_url"] = std::getenv("ZHIPU_BASE_URL");
    
    provider->init(modelParams);
    ASSERT_TRUE(provider->isAvailable());

    std::map<std::string, std::string> requestParams = { {"temperature", "1.0"}, {"max_tokens", "256"} };
    std::vector<ai_chat_sdk::Message> messages;
    messages.push_back({"user", "你是什么模型？"});

    // callback 会写入这三个局部变量（按引用捕获）
    std::string accumulated;       // 累积所有 InProgress 的 content
    bool streamDone = false;       // 是否收到 Done 信号
    int inProgressCount = 0;       // InProgress 被调用次数

    auto callback = [&](const std::string &content, ai_chat_sdk::StreamStatus status) {
        if (status == ai_chat_sdk::StreamStatus::InProgress)
        {
            accumulated += content;
            inProgressCount++;
            std::cout << "[chunk] " << content << std::flush;
        }
        else if (status == ai_chat_sdk::StreamStatus::Done)
        {
            streamDone = true;
            std::cout << "\n[Done]\n";
        }
    };

    // sendMessageStream 是同步阻塞的，返回时流已经走完了
    std::string fullResponse = provider->sendMessageStream(messages, requestParams, callback);

    // 断言 1：流必须被标记为结束
    ASSERT_TRUE(streamDone) << "callback 未收到 Done 信号";

    // 断言 2：至少收到过一次增量
    ASSERT_GT(inProgressCount, 0) << "callback(InProgress) 一次都没调";

    // 断言 3：累积内容不为空
    ASSERT_FALSE(accumulated.empty()) << "callback 累积的内容为空";

    // 断言 4：返回值等于累积内容
    EXPECT_EQ(fullResponse, accumulated) << "sendMessageStream 返回值和 callback 累积内容不一致";

    // 打印最终完整回复
    std::cout << "\n===== 最终完整回复 =====\n" << fullResponse << "\n========================\n";
}

int main(int argc, char *argv[])
{
    // 初始化日志库
    log_module::Logger::init("testllm.log", "stdout", spdlog::level::debug);
    // 初始化测试环境
    ::testing::InitGoogleTest(&argc, argv);
    // 运行所有测试
    LOG_INFO("testllm start");
    return RUN_ALL_TESTS();
}
