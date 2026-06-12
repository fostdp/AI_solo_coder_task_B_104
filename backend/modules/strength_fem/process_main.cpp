#include "modules/strength_fem/strength_module.h"
#include "modules/common/ipc_channel.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <atomic>

#if defined(HAS_SPDLOG)
#include <spdlog/spdlog.h>
#else
#define LOG_INFO(msg)  do { std::cout << "[INFO] " << msg << std::endl; } while(0)
#define LOG_ERROR(msg) do { std::cerr << "[ERROR] " << msg << std::endl; } while(0)
#endif

using namespace porcelain_monitor;
using namespace porcelain_monitor::modules;
using json = nlohmann::json;

static inline void log_info(const std::string& msg) {
#if defined(HAS_SPDLOG)
    spdlog::info(msg);
#else
    LOG_INFO(msg);
#endif
}
static inline void log_error(const std::string& msg) {
#if defined(HAS_SPDLOG)
    spdlog::error(msg);
#else
    LOG_ERROR(msg);
#endif
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    log_info("Strength FEM process starting...");

    StrengthFemModule module;

    json config;
    config["bending_config"]["support_span_mm"] = 40.0;
    config["bending_config"]["loading_span_mm"] = 20.0;
    config["bending_config"]["specimen_thickness_mm"] = 3.0;
    config["bending_config"]["specimen_width_mm"] = 10.0;
    config["bending_config"]["porcelain_strength_mpa"] = 120.0;
    config["bending_config"]["repair_interface_strength_ratio"] = 0.85;

    if (!module.initialize(config)) {
        log_error("Failed to initialize Strength FEM module");
        return 1;
    }

    log_info("Strength FEM module initialized, waiting for requests...");

    std::atomic<bool> running{true};
    auto ipc = IpcChannelFactory::create(IpcType::LOCAL);
    ipc->initialize("strength_fem_server");

    ipc->set_message_callback([&](const IpcMessage& msg) {
        if (msg.command == "shutdown") {
            running = false;
            return;
        }

        if (msg.command == "simulate") {
            ModuleResult result;
            module.execute_sync(msg.payload, result);

            IpcMessage response;
            response.module_name = "strength_fem";
            response.command = "result";
            response.payload = result.data;
            response.request_id = msg.request_id;
            ipc->send(response);
        }
    });

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    module.shutdown();
    log_info("Strength FEM process shutdown complete");
    return 0;
}
