#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <nlohmann/json.hpp>
#include "common.h"

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

enum class IpcType {
    LOCAL,
    PIPE,
    SOCKET,
    SHARED_MEM,
    MESSAGE_QUEUE
};

struct IpcMessage {
    std::string module_name;
    std::string command;
    json payload;
    int64_t request_id;
    std::chrono::system_clock::time_point timestamp;
};

using IpcCallback = std::function<void(const IpcMessage&)>;

class IIpcChannel {
public:
    virtual ~IIpcChannel() = default;

    virtual bool initialize(const std::string& endpoint) = 0;
    virtual bool connect(const std::string& endpoint) = 0;
    virtual bool send(const IpcMessage& msg) = 0;
    virtual bool receive(IpcMessage& msg, int timeout_ms = -1) = 0;
    virtual void set_message_callback(IpcCallback cb) = 0;
    virtual bool is_connected() const = 0;
    virtual void close() = 0;
    virtual IpcType type() const = 0;
    virtual std::string endpoint() const = 0;
};

class LocalIpcChannel : public IIpcChannel {
public:
    LocalIpcChannel();
    ~LocalIpcChannel() override;

    bool initialize(const std::string& endpoint) override;
    bool connect(const std::string& endpoint) override;
    bool send(const IpcMessage& msg) override;
    bool receive(IpcMessage& msg, int timeout_ms = -1) override;
    void set_message_callback(IpcCallback cb) override;
    bool is_connected() const override;
    void close() override;
    IpcType type() const override { return IpcType::LOCAL; }
    std::string endpoint() const override { return endpoint_; }

    void set_peer(LocalIpcChannel* peer) { peer_ = peer; }

private:
    std::string endpoint_;
    std::vector<IpcMessage> inbox_;
    std::mutex inbox_mutex_;
    std::condition_variable inbox_cv_;
    LocalIpcChannel* peer_ = nullptr;
    IpcCallback callback_;
    bool connected_ = false;
};

class IpcChannelFactory {
public:
    static std::unique_ptr<IIpcChannel> create(IpcType type);
    static std::unique_ptr<IIpcChannel> create_local_pair(
        std::unique_ptr<LocalIpcChannel>& server,
        std::unique_ptr<LocalIpcChannel>& client,
        const std::string& endpoint = "local");
};

}
}
