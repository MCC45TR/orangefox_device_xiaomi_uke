// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace ure {
// One owned worker coalesces GUI batches. It never detaches, and a normal
// shutdown drains the final request before joining. Storage failures are
// reported by the callback; scheduling is not a durability acknowledgement.
class PreferenceWriter {
public:
    explicit PreferenceWriter(std::function<void()> save) : save_(std::move(save)), worker_([this] { run(); }) {}
    PreferenceWriter(const PreferenceWriter&) = delete;
    PreferenceWriter& operator=(const PreferenceWriter&) = delete;
    ~PreferenceWriter() {
        { std::lock_guard<std::mutex> guard(lock_); stopping_=true; changed_.notify_one(); }
        worker_.join();
    }
    void request() {
        std::lock_guard<std::mutex> guard(lock_);
        if(stopping_)return;
        const auto now=Clock::now();
        if(!pending_)first_=now;
        last_=now; pending_=true; changed_.notify_one();
    }
private:
    using Clock=std::chrono::steady_clock;
    void run() {
        std::unique_lock<std::mutex> guard(lock_);
        for(;;) {
            changed_.wait(guard,[this] { return pending_ || stopping_; });
            if(!pending_ && stopping_)return;
            const auto deadline=std::min(first_+std::chrono::seconds(2),last_+std::chrono::milliseconds(400));
            if(!stopping_ && Clock::now()<deadline) { changed_.wait_until(guard,deadline); continue; }
            pending_=false;
            guard.unlock();
            try { save_(); } catch(...) { /* A callback failure must not terminate the GUI. */ }
            guard.lock();
        }
    }
    std::function<void()> save_;
    std::mutex lock_;
    std::condition_variable changed_;
    bool pending_=false,stopping_=false;
    Clock::time_point first_{},last_{};
    std::thread worker_;
};
} // namespace ure
