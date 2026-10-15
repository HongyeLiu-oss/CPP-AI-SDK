#include "../../include/util/log.h"
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <mutex>

namespace log_module
{
    std::shared_ptr<spdlog::logger> Logger::m_logger = nullptr;
    std::mutex Logger::m_mutex;

    void Logger::init(const std::string &logger_name, const std::string &log_file, spdlog::level::level_enum level)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logger != nullptr)
        {
            // 已经初始化过，直接返回（单次初始化语义）
            return;
        }
        
        // 设置全局自动刷新级别，当日志级别 >= level 时，日志会被立即刷新到文件
        spdlog::flush_on(level);
        // 启用异步日志：将日志信息放入队列，有后台线程负责写入
        // 参数1：队列大小；参数2：后台线程数量
        spdlog::init_thread_pool(32768, 1);

        if ("stdout" == log_file)
        {
            // 创建一个带颜色的输出到控制台的日志器
            m_logger = spdlog::stdout_color_mt(logger_name);
        }
        else
        {
            // 创建文件输出日志器，日志会被写入到指定文件中
            m_logger = spdlog::basic_logger_mt(logger_name, log_file);
        }

        // 格式设置
        // [%H:%M:%S] 时分秒
        // [%n]      日志器名称
        // [%7l]     日志级别，左对齐，宽度为7个字符
        // [%s:%#]   源文件 basename + 行号（短文件名，避免完整路径溢出）
        // %v        日志消息
        m_logger->set_pattern("[%H:%M:%S][%n][%7l][%s:%#] %v");
        m_logger->set_level(level);
    }

    std::shared_ptr<spdlog::logger> Logger::getLogger()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_logger;
    }
}
