#include "../sdk/include/deepseekprovider.h"
#include "../sdk/src/util/log.cpp"
#include <cstdlib>
#include <gtest/gtest.h>

TEST(DeepSeekProviderTest, sendMessage)
{
    std::shared_ptr<ai_chat_sdk::DeepSeekProvider> provider = std::make_shared<ai_chat_sdk::DeepSeekProvider>();
    ASSERT_TRUE(provider);

    std::map<std::string, std::string> modelParams;
    modelParams["api_key"] = std::getenv("DEEPSEEK_API_KEY");
    modelParams["base_url"] = std::getenv("DEEPSEEK_BASE_URL");
    
    provider->init(modelParams);
    ASSERT_TRUE(provider->isAvailable());

    std::map<std::string, std::string> requestParams = { {"temperature", "0.7"}, {"max_tokens", "1024"} };
    std::vector<ai_chat_sdk::Message> messages;
    messages.push_back({"user", "你是什么模型?"});

    std::string response = provider->sendMessage(messages, requestParams);
    ASSERT_FALSE(response.empty());
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
