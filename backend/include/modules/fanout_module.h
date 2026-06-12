#pragma once

#include <vector>
#include "lockfree_queue.h"
#include "module_base.h"

namespace porcelain_monitor {
namespace modules {

template <typename T>
class FanOutModule : public ModuleBase<T, T> {
public:
    using Base = ModuleBase<T, T>;
    using QueuePtr = concurrency::SharedQueue<T>;

    explicit FanOutModule(const std::string& name = "FanOut")
        : Base(name) {}

    void add_output(QueuePtr q) { extra_outputs_.push_back(std::move(q)); }

protected:
    void process(const T& msg, typename Base::OutputQueue primary_out) override {
        if (primary_out) {
            primary_out->push(msg);
        }
        for (auto& q : extra_outputs_) {
            if (q) {
                T copy = msg;
                q->push(std::move(copy));
            }
        }
    }

private:
    std::vector<QueuePtr> extra_outputs_;
};

}
}
