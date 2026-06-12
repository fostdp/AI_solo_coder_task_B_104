#include "modules/fem_stress/fem_stress_module.h"
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
    log_info("FEM Stress process starting...");

    FemStressModule module;

    json config;
    config["fem_config"]["grid_resolution"] = 50;
    config["fem_config"]["youngs_modulus_gpa"] = 70.0;
    config["fem_config"]["poissons_ratio"] = 0.22;
    config["fem_config"]["use_adaptive_mesh"] = true;
    config["fem_config"]["max_refinement_level"] = 3;

    if (!module.initialize(config)) {
        log_error("Failed to initialize FEM Stress module");
        return 1;
    }

    log_info("FEM Stress module initialized, waiting for requests...");

    std::atomic<bool> running{true};
    auto ipc = IpcChannelFactory::create(IpcType::LOCAL);
    ipc->initialize("fem_stress_server");

    ipc->set_message_callback([&](const IpcMessage& msg) {
        if (msg.command == "shutdown") {
            running = false;
            return;
        }

        if (msg.command == "analyze") {
            ModuleResult result;
            module.execute_sync(msg.payload, result);

            IpcMessage response;
            response.module_name = "fem_stress";
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
    log_info("FEM Stress process shutdown complete");
    return 0;
}
