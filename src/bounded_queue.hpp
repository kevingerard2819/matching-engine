#pragma once
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace matching {
// Preallocated ring. close() wakes both sides; consumers drain the accepted prefix.
template <typename T> class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : slots_(capacity) {
        if (!capacity) throw std::invalid_argument("queue capacity must be positive");
    }
    bool try_push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || size_ == slots_.size()) return false;
        insert(std::move(value));
        return true;
    }
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        writable_.wait(lock, [&] { return closed_ || size_ < slots_.size(); });
        if (closed_) return false;
        insert(std::move(value));
        return true;
    }
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        readable_.wait(lock, [&] { return closed_ || size_; });
        if (!size_) return std::nullopt;
        auto value = std::move(slots_[head_]);
        slots_[head_].reset();
        head_ = (head_ + 1) % slots_.size();
        --size_;
        writable_.notify_one();
        return value;
    }
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        readable_.notify_all();
        writable_.notify_all();
    }
private:
    void insert(T value) {
        slots_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % slots_.size();
        ++size_;
        readable_.notify_one();
    }
    std::vector<std::optional<T>> slots_;
    std::size_t head_ = 0, tail_ = 0, size_ = 0;
    bool closed_ = false;
    std::mutex mutex_;
    std::condition_variable readable_, writable_;
};
} // namespace matching
