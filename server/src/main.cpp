#include "chatserver.h"
#include "util/log.h"

#include <gflags/gflags.h>

#include <cstdlib>
#include <vector>

DEFINE_int32(port, 8083, "HTTP listen port");
DEFINE_string(db_path, "chat.db", "SQLite database file path");
DEFINE_string(static_dir, "static", "directory containing the web page");
DEFINE_string(deepseek_model, "deepseek-flash", "DeepSeek model id");
DEFINE_string(glm_model, "glm-5.3-flash", "Zhipu GLM model id");

int main(int argc, char **argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    log_module::Logger::init("chat_server", "stdout", spdlog::level::info);

    ai_chat_sdk::ChatSDK sdk(FLAGS_db_path);

    // API Key 只从环境变量读，避免出现在命令行历史和进程列表里
    std::vector<ai_chat_sdk::ApiModelConfig> configs;
    if (const char *apiKey = std::getenv("DEEPSEEK_API_KEY"))
    {
        ai_chat_sdk::ApiModelConfig config;
        config.provider = "deepseek";
        config.modelName = FLAGS_deepseek_model;
        config.apiKey = apiKey;
        config.baseUrl = "https://api.deepseek.com";
        configs.push_back(config);
    }
    if (const char *apiKey = std::getenv("ZHIPU_API_KEY"))
    {
        ai_chat_sdk::ApiModelConfig config;
        config.provider = "glm";
        config.modelName = FLAGS_glm_model;
        config.apiKey = apiKey;
        config.baseUrl = "https://open.bigmodel.cn";
        configs.push_back(config);
    }

    if (configs.empty())
    {
        LOG_WARN("No API key found, set DEEPSEEK_API_KEY or ZHIPU_API_KEY before starting");
    }
    else if (!sdk.initModels(configs))
    {
        LOG_WARN("Some models failed to initialize, check the log above");
    }

    chat_server::ChatServer server(sdk, FLAGS_port, FLAGS_static_dir);
    return server.run() ? 0 : 1;
}
