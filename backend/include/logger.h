#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

namespace porcelain_monitor {
namespace logging {

inline std::shared_ptr<spdlog::logger> init_logger(const std::string& name = "pm",
                                                     const std::string& log_path = "/app/logs/porcelain_monitor.log",
                                                     size_t max_size = 1048576 * 50,
                                                     size_t max_files = 5) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::debug);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_path, max_size, max_files);
    file_sink->set_level(spdlog::level::info);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [thread %t] %v");

    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    return logger;
}

inline std::shared_ptr<spdlog::logger> get() {
    auto logger = spdlog::get("pm");
    if (!logger) {
        return init_logger();
    }
    return logger;
}

}
}
