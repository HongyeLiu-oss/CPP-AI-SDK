#pragma once

#include <memory>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <mutex>

namespace log_module
{
    class Logger
    {
        public:
            static void init(const std::string &logger_name, const std::string &log_file, spdlog::level::level_enum level = spdlog::level::info);
            static std::shared_ptr<spdlog::logger> getLogger();
        private:
            Logger(const Logger &other) = delete;
            Logger& operator=(const Logger &other) = delete;
        private:
            static std::shared_ptr<spdlog::logger> m_logger;
            static std::mutex m_mutex;
    };

    // 使用 SPDLOG_LOGGER_CALL 把 __FILE__/__LINE__ 打包成 source_loc 传入
    // 文件名与行号由 Logger::init() 中的 set_pattern 配置 [%s:%#] 负责显示
    #define LOG_TRACE(format, ...) do { auto _l = log_module::Logger::getLogger(); if (_l) SPDLOG_LOGGER_CALL(_l, spdlog::level::trace, format, ##__VA_ARGS__); } while(0)
    #define LOG_DBG(format, ...)   do { auto _l = log_module::Logger::getLogger(); if (_l) SPDLOG_LOGGER_CALL(_l, spdlog::level::debug, format, ##__VA_ARGS__); } while(0)
    #define LOG_INFO(format, ...)  do { auto _l = log_module::Logger::getLogger(); if (_l) SPDLOG_LOGGER_CALL(_l, spdlog::level::info, format, ##__VA_ARGS__); } while(0)
    #define LOG_WARN(format, ...)  do { auto _l = log_module::Logger::getLogger(); if (_l) SPDLOG_LOGGER_CALL(_l, spdlog::level::warn, format, ##__VA_ARGS__); } while(0)
    #define LOG_ERROR(format, ...) do { auto _l = log_module::Logger::getLogger(); if (_l) SPDLOG_LOGGER_CALL(_l, spdlog::level::err, format, ##__VA_ARGS__); } while(0)
    #define LOG_CRIT(format, ...)  do { auto _l = log_module::Logger::getLogger(); if (_l) SPDLOG_LOGGER_CALL(_l, spdlog::level::critical, format, ##__VA_ARGS__); } while(0)
}