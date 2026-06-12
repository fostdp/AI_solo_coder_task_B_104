#pragma once

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <chrono>
#include "lockfree_queue.h"

namespace porcelain_monitor {
namespace modules {

template <typename InMsg, typename OutMsg>
class ModuleBase {
public:
    using InputQueue = concurrency::SharedQueue<InMsg>;
    using OutputQueue = concurrency::SharedQueue<OutMsg>;

    explicit ModuleBase(const std::string& name)
        : name_(name) {}

    virtual ~ModuleBase() { stop(); }

    void set_input(InputQueue q) { input_ = std::move(q); }
    void set_output(OutputQueue q) { output_ = std::move(q); }

    InputQueue input() const { return input_; }
    OutputQueue output() const { return output_; }

    void start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this]() { run_loop(); });
    }

    void stop() {
        running_.store(false);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    const std::string& name() const { return name_; }

    uint64_t processed_count() const { return processed_.load(); }

protected:
    virtual void process(const InMsg& msg, OutputQueue out) = 0;

    virtual void on_start() {}
    virtual void on_stop() {}

    void run_loop() {
        on_start();
        while (running_.load()) {
            InMsg msg;
            bool got = input_ && input_->try_pop(msg);
            if (got) {
                try {
                    process(msg, output_);
                    processed_.fetch_add(1, std::memory_order_relaxed);
                } catch (const std::exception& e) {
                    std::cerr << "[" << name_ << "] Error: " << e.what() << std::endl;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        on_stop();
    }

    std::string name_;
    InputQueue input_;
    OutputQueue output_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> processed_{0};
    std::thread thread_;
};

class SourceModuleBase {
public:
    virtual ~SourceModuleBase() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
};

class SinkModuleBase {
public:
    virtual ~SinkModuleBase() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
};

}
}
