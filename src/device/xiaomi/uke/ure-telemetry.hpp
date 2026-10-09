// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Uke observation only: no health HAL, firmware activation or storage admission.
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace ure::telemetry {
enum class State { unavailable, observed, malformed, denied, ambiguous, budget };
enum class Charge { unknown, charging, discharging, not_charging, full };
struct Battery {
    State state = State::unavailable;
    State status_state = State::unavailable;
    int capacity = -1;
    Charge status = Charge::unknown;
    bool available() const { return state == State::observed; }
    bool charging() const { return available() && status_state == State::observed && status == Charge::charging; }
};
struct Temperature {
    State state = State::unavailable;
    int millidegrees = 0;
    bool available() const { return state == State::observed; }
};

inline bool decimal(std::string_view text, int low, int high, int& number) {
    if (text.empty()) return false;
    std::size_t start = text.front() == '-' ? 1 : 0;
    if (start == text.size()) return false;
    for (std::size_t i = start; i < text.size(); ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && number >= low && number <= high;
}
inline bool capacity_value(std::string_view text, int& value) {
    return !text.empty() && text.front() != '-' && decimal(text, 0, 100, value);
}
inline std::string battery_text(const Battery& sample, bool suffix) {
    if (!sample.available()) return "--";
    return std::to_string(sample.capacity) + (suffix ? (sample.charging() ? "%+" : "% ") : "");
}
inline std::string temperature_text(const Temperature& sample) {
    // thermal_zone/temp is signed millidegrees Celsius; no magnitude heuristic.
    return sample.available() ? std::to_string(sample.millidegrees / 1000) : "--";
}
inline int fill_height(int height, int capacity) {
    if (height <= 0) return 0;
    return static_cast<int>(static_cast<std::int64_t>(height) * std::clamp(capacity, 0, 100) / 100);
}

namespace detail {
class Fd {
    int value_;
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return value_; }
    int release() { const int value = value_; value_ = -1; return value; }
};
struct Attribute { State state = State::unavailable; std::string text; };
inline State io_state() {
    if (errno == EACCES || errno == EPERM) return State::denied;
    if (errno == ELOOP || errno == EXDEV) return State::malformed;
    return State::unavailable;
}
inline bool component(std::string_view name) {
    if (name.empty() || name.size() > 63 || name == "." || name == "..") return false;
    for (const char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
inline int beneath(int root, const char* path, int flags) {
    struct open_how how{};
    how.flags = static_cast<std::uint64_t>(flags | O_CLOEXEC);
    // Class directory aliases may point into devices/, but never outside /sys.
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;
    return static_cast<int>(::syscall(SYS_openat2, root, path, &how, sizeof(how)));
}
class Reader {
    Fd root_;
    unsigned reads_ = 0;
public:
    explicit Reader(const char* root) : root_(::open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) {}
    int directory(const std::string& path) { return beneath(root_.get(), path.c_str(), O_RDONLY | O_DIRECTORY); }
    Attribute attribute(int directory_fd, const char* name) {
        if (++reads_ > 256) return {State::budget, {}};
        Fd file(beneath(directory_fd, name, O_RDONLY | O_NONBLOCK | O_NOFOLLOW));
        if (file.get() < 0) return {io_state(), {}};
        struct stat info{};
        if (::fstat(file.get(), &info) != 0) return {io_state(), {}};
        if (!S_ISREG(info.st_mode)) return {State::malformed, {}};
        std::array<char, 65> bytes{};
        std::size_t used = 0;
        unsigned attempts = 0;
        while (attempts++ < 8) {
            const auto count = ::read(file.get(), bytes.data() + used, bytes.size() - used);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) return {io_state(), {}};
            if (count == 0) {
                // These standard sysfs show attributes terminate one record
                // with a newline. Refuse a partial/truncated record.
                if (!used || bytes[used - 1] != '\n') return {State::malformed, {}};
                --used;
                if (!used) return {State::malformed, {}};
                for (std::size_t i = 0; i < used; ++i)
                    if (bytes[i] < 32 || bytes[i] > 126) return {State::malformed, {}};
                return {State::observed, std::string(bytes.data(), used)};
            }
            used += static_cast<std::size_t>(count);
            if (used > 64) return {State::budget, {}};
        }
        return {State::budget, {}};
    }
    State entries(const char* path, std::vector<std::string>& names) {
        Fd directory_fd(directory(path));
        if (directory_fd.get() < 0) return io_state();
        DIR* directory_stream = ::fdopendir(directory_fd.get());
        if (!directory_stream) return io_state();
        static_cast<void>(directory_fd.release());
        State state = State::observed;
        unsigned count = 0;
        for (;;) {
            errno = 0;
            const auto* entry = ::readdir(directory_stream);
            if (!entry) { if (errno) state = io_state(); break; }
            const std::string name(entry->d_name);
            if (name == "." || name == "..") continue;
            if (++count > 128) { state = State::budget; break; }
            if (component(name)) names.push_back(name);
        }
        ::closedir(directory_stream);
        return state;
    }
};
inline bool cpu_type(std::string_view type) {
    if (type == "cpu_therm") return true;
    if (type.size() <= 6 || type.substr(0, 6) != "cpuss-") return false;
    for (const char c : type.substr(6)) if (c < '0' || c > '9') return false;
    return true;
}
} // namespace detail

inline Battery observe_battery(const char* root = "/sys") {
    detail::Reader reader(root);
    std::vector<std::string> names;
    const auto listed = reader.entries("class/power_supply", names);
    if (listed != State::observed) return {listed};
    Battery sample;
    bool found = false;
    for (const auto& name : names) {
        detail::Fd candidate(reader.directory("class/power_supply/" + name));
        if (candidate.get() < 0) continue;
        const auto type = reader.attribute(candidate.get(), "type");
        if (type.state == State::budget) return {State::budget};
        if (type.state != State::observed || type.text != "Battery") continue;
        if (found) return {State::ambiguous};
        found = true;
        const auto capacity = reader.attribute(candidate.get(), "capacity");
        sample.state = capacity.state;
        if (capacity.state == State::observed) {
            if (capacity_value(capacity.text, sample.capacity)) sample.state = State::observed;
            else { sample.state = State::malformed; sample.capacity = -1; }
        }
        const auto status = reader.attribute(candidate.get(), "status");
        sample.status_state = status.state;
        if (status.state == State::observed) {
            if (status.text == "Charging") sample.status = Charge::charging;
            else if (status.text == "Discharging") sample.status = Charge::discharging;
            else if (status.text == "Not charging") sample.status = Charge::not_charging;
            else if (status.text == "Full") sample.status = Charge::full;
            else if (status.text != "Unknown") sample.status_state = State::malformed;
        }
    }
    return sample;
}

inline Temperature observe_cpu(const char* root = "/sys") {
    detail::Reader reader(root);
    std::vector<std::string> names;
    const auto listed = reader.entries("class/thermal", names);
    if (listed != State::observed) return {listed};
    Temperature cpu, fallback;
    bool have_cpu = false, have_fallback = false;
    State fallback_error = State::observed;
    for (const auto& name : names) {
        if (name.size() <= 12 || name.compare(0, 12, "thermal_zone") != 0) continue;
        if (!std::all_of(name.begin() + 12, name.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        detail::Fd candidate(reader.directory("class/thermal/" + name));
        if (candidate.get() < 0) continue;
        const auto type = reader.attribute(candidate.get(), "type");
        if (type.state == State::budget) return {State::budget};
        if (type.state != State::observed || !detail::cpu_type(type.text)) continue;
        const auto value = reader.attribute(candidate.get(), "temp");
        Temperature current{value.state};
        if (value.state == State::observed && !decimal(value.text, -100000, 250000, current.millidegrees)) current.state = State::malformed;
        if (type.text == "cpu_therm") {
            if (have_cpu) return {State::ambiguous};
            have_cpu = true; cpu = current;
        } else {
            if (!current.available()) fallback_error = current.state;
            if (!have_fallback || current.millidegrees > fallback.millidegrees) fallback = current;
            have_fallback = true;
        }
    }
    if (have_cpu) return cpu;
    if (fallback_error != State::observed) return {fallback_error};
    return have_fallback ? fallback : Temperature{};
}

class CpuCache {
    std::mutex mutex_;
    bool initialized_ = false;
    std::chrono::steady_clock::time_point refreshed_{};
    Temperature sample_;
public:
    Temperature get(const char* root = "/sys", std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || now < refreshed_ || now - refreshed_ >= std::chrono::seconds(5)) {
            sample_ = observe_cpu(root); refreshed_ = now; initialized_ = true;
        }
        return sample_;
    }
};
inline Temperature cpu_snapshot() { static CpuCache cache; return cache.get(); }
} // namespace ure::telemetry
