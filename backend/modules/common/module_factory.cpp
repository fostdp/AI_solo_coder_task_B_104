#include "modules/common/module_factory.h"

namespace porcelain_monitor {
namespace modules {

ModuleFactory& ModuleFactory::instance() {
    static ModuleFactory instance;
    return instance;
}

bool ModuleFactory::register_module(const std::string& name, CreatorFunc creator) {
    if (!creator) return false;
    creators_[name] = std::move(creator);
    return true;
}

std::unique_ptr<IModule> ModuleFactory::create(const std::string& name) {
    auto it = creators_.find(name);
    if (it == creators_.end()) return nullptr;
    return it->second();
}

std::vector<std::string> ModuleFactory::get_registered_modules() const {
    std::vector<std::string> names;
    names.reserve(creators_.size());
    for (const auto& pair : creators_) {
        names.push_back(pair.first);
    }
    return names;
}

bool ModuleFactory::is_registered(const std::string& name) const {
    return creators_.find(name) != creators_.end();
}

}
}
