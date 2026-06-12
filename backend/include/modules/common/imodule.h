#pragma once

#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include "common.h"

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

enum class ModuleStatus {
    UNINITIALIZED,
    IDLE,
    RUNNING,
    COMPLETED,
    ERROR
};

struct ModuleResult {
    bool success;
    std::string error_message;
    json data;
};

using ResultCallback = std::function<void(const ModuleResult&)>;

class IModule {
public:
    virtual ~IModule() = default;

    virtual bool initialize(const json& config) = 0;
    virtual bool execute(const json& input, ResultCallback callback = nullptr) = 0;
    virtual bool execute_sync(const json& input, ModuleResult& result) = 0;
    virtual ModuleStatus get_status() const = 0;
    virtual std::string get_name() const = 0;
    virtual std::string get_version() const = 0;
    virtual bool cancel() = 0;
    virtual bool shutdown() = 0;

    virtual void set_progress_callback(std::function<void(double, const std::string&)> cb) {}
    virtual bool supports_async() const { return false; }
    virtual bool supports_parallel() const { return false; }
    virtual bool is_external_process() const { return false; }
};

}
}
