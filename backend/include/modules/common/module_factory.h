#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include "imodule.h"

namespace porcelain_monitor {
namespace modules {

class ModuleFactory {
public:
    static ModuleFactory& instance();

    using CreatorFunc = std::function<std::unique_ptr<IModule>()>;

    bool register_module(const std::string& name, CreatorFunc creator);

    std::unique_ptr<IModule> create(const std::string& name);

    template <typename T>
    bool register_module_type(const std::string& name) {
        return register_module(name, []() { return std::make_unique<T>(); });
    }

    std::vector<std::string> get_registered_modules() const;

    bool is_registered(const std::string& name) const;

private:
    ModuleFactory() = default;
    std::unordered_map<std::string, CreatorFunc> creators_;
};

#define REGISTER_MODULE(Class, Name) \
    namespace { \
        static bool _registered_##Class = \
            ModuleFactory::instance().register_module_type<Class>(Name); \
    }

}
}
