#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace porcelain_monitor {
namespace concurrency {

template <typename T>
class LockFreeQueue {
public:
    LockFreeQueue() {
        Node* dummy = new Node();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        while (Node* old_head = head_.load(std::memory_order_relaxed)) {
            head_.store(old_head->next, std::memory_order_relaxed);
            delete old_head;
        }
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    void push(T value) {
        Node* new_node = new Node(std::move(value));
        Node* old_tail = tail_.exchange(new_node, std::memory_order_acq_rel);
        old_tail->next.store(new_node, std::memory_order_release);
    }

    bool try_pop(T& out) {
        Node* old_head = head_.load(std::memory_order_acquire);
        while (true) {
            Node* next = old_head->next.load(std::memory_order_acquire);
            if (!next) {
                return false;
            }
            if (head_.compare_exchange_weak(old_head, next,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                out = std::move(next->value);
                delete old_head;
                return true;
            }
        }
    }

    bool empty() const {
        Node* head = head_.load(std::memory_order_acquire);
        return head->next.load(std::memory_order_acquire) == nullptr;
    }

private:
    struct Node {
        std::atomic<Node*> next{nullptr};
        std::optional<T> value;

        Node() = default;
        explicit Node(T&& v) : value(std::move(v)) {}
    };

    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};

template <typename T>
using SharedQueue = std::shared_ptr<LockFreeQueue<T>>;

template <typename T>
SharedQueue<T> make_queue() {
    return std::make_shared<LockFreeQueue<T>>();
}

}
}
