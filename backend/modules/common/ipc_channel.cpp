#include "modules/common/ipc_channel.h"
#include <chrono>

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

LocalIpcChannel::LocalIpcChannel() = default;

LocalIpcChannel::~LocalIpcChannel() {
    close();
}

bool LocalIpcChannel::initialize(const std::string& endpoint) {
    endpoint_ = endpoint;
    connected_ = true;
    return true;
}

bool LocalIpcChannel::connect(const std::string& endpoint) {
    endpoint_ = endpoint;
    connected_ = true;
    return true;
}

bool LocalIpcChannel::send(const IpcMessage& msg) {
    if (!connected_ || !peer_) return false;

    {
        std::lock_guard<std::mutex> lock(peer_->inbox_mutex_);
        peer_->inbox_.push_back(msg);
    }
    peer_->inbox_cv_.notify_one();

    if (peer_->callback_) {
        peer_->callback_(msg);
    }

    return true;
}

bool LocalIpcChannel::receive(IpcMessage& msg, int timeout_ms) {
    std::unique_lock<std::mutex> lock(inbox_mutex_);

    if (timeout_ms < 0) {
        inbox_cv_.wait(lock, [this] { return !inbox_.empty(); });
    } else {
        bool received = inbox_cv_.wait_for(
            lock, std::chrono::milliseconds(timeout_ms),
            [this] { return !inbox_.empty(); }
        );
        if (!received) return false;
    }

    if (inbox_.empty()) return false;
    msg = std::move(inbox_.front());
    inbox_.erase(inbox_.begin());
    return true;
}

void LocalIpcChannel::set_message_callback(IpcCallback cb) {
    callback_ = std::move(cb);
}

bool LocalIpcChannel::is_connected() const {
    return connected_ && peer_ != nullptr;
}

void LocalIpcChannel::close() {
    connected_ = false;
    peer_ = nullptr;
    {
        std::lock_guard<std::mutex> lock(inbox_mutex_);
        inbox_.clear();
    }
    inbox_cv_.notify_all();
}

std::unique_ptr<IIpcChannel> IpcChannelFactory::create(IpcType type) {
    switch (type) {
        case IpcType::LOCAL:
            return std::make_unique<LocalIpcChannel>();
        case IpcType::PIPE:
        case IpcType::SOCKET:
        case IpcType::SHARED_MEM:
        case IpcType::MESSAGE_QUEUE:
        default:
            return std::make_unique<LocalIpcChannel>();
    }
}

std::unique_ptr<IIpcChannel> IpcChannelFactory::create_local_pair(
    std::unique_ptr<LocalIpcChannel>& server,
    std::unique_ptr<LocalIpcChannel>& client,
    const std::string& endpoint)
{
    server = std::make_unique<LocalIpcChannel>();
    client = std::make_unique<LocalIpcChannel>();
    server->set_peer(client.get());
    client->set_peer(server.get());
    server->initialize(endpoint + "_server");
    client->connect(endpoint + "_client");
    return nullptr;
}

}
}
