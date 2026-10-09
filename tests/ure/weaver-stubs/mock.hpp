// SPDX-License-Identifier: Apache-2.0
// Host-only inert service boundary; production Weaver1.cpp is compiled intact.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <functional>
#include <string>
struct AIBinder {};
namespace ndk {
struct ScopedAStatus {
    bool good = true;
    bool isOk() const { return good; }
};
class SpAIBinder {
    AIBinder* value_;
public:
    explicit SpAIBinder(AIBinder* value) : value_(value) {}
    AIBinder* get() const { return value_; }
};
}
struct Fixture {
    bool aidl = false, hidl = false, transport = true, callback = true, duplicate = false;
    int64_t slots = 16, keySize = 16, valueSize = 32, timeout = 0;
    int status = 0, configStatus = 0;
    unsigned binderChecks = 0, hidlChecks = 0, configCalls = 0, readCalls = 0;
    size_t replySize = 32;
};
inline Fixture fixture;
inline AIBinder fakeBinder;
inline AIBinder* AServiceManager_checkService(const char*) {
    ++fixture.binderChecks;
    return fixture.aidl ? &fakeBinder : nullptr;
}
namespace android {
template <class T> using sp = std::shared_ptr<T>;
namespace hardware { namespace weaver { namespace V1_0 {
enum class WeaverStatus : uint32_t { OK, FAILED };
enum class WeaverReadStatus : uint32_t { OK, FAILED, INCORRECT_KEY, THROTTLE };
struct WeaverConfig { uint32_t slots = 0, keySize = 0, valueSize = 0; };
struct WeaverReadResponse { uint32_t timeout = 0; std::vector<uint8_t> value; };
class IWeaver {
public:
    static sp<IWeaver> tryGetService() {
        ++fixture.hidlChecks;
        return fixture.hidl ? std::make_shared<IWeaver>() : nullptr;
    }
    ndk::ScopedAStatus getConfig(const std::function<void(WeaverStatus, WeaverConfig)>& callback) {
        ++fixture.configCalls;
        WeaverConfig c{static_cast<uint32_t>(fixture.slots), static_cast<uint32_t>(fixture.keySize), static_cast<uint32_t>(fixture.valueSize)};
        if (fixture.callback) callback(static_cast<WeaverStatus>(fixture.configStatus), c);
        if (fixture.callback && fixture.duplicate) callback(static_cast<WeaverStatus>(fixture.configStatus), c);
        return {fixture.transport};
    }
    ndk::ScopedAStatus read(uint32_t, const std::vector<uint8_t>& key,
            const std::function<void(WeaverReadStatus, WeaverReadResponse)>& callback) {
        ++fixture.readCalls;
        if (key.size() != static_cast<uint64_t>(fixture.keySize)) std::abort();
        WeaverReadResponse r{static_cast<uint32_t>(fixture.timeout), std::vector<uint8_t>(fixture.replySize, 0x5a)};
        if (fixture.callback) callback(static_cast<WeaverReadStatus>(fixture.status), r);
        if (fixture.callback && fixture.duplicate) callback(static_cast<WeaverReadStatus>(fixture.status), r);
        return {fixture.transport};
    }
};
} } }
}
namespace aidl { namespace android { namespace hardware { namespace weaver {
struct WeaverConfig { int32_t slots = 0, keySize = 0, valueSize = 0; };
enum class WeaverReadStatus : int32_t { OK, FAILED, INCORRECT_KEY, THROTTLE };
struct WeaverReadResponse { int64_t timeout = 0; std::vector<uint8_t> value; WeaverReadStatus status = WeaverReadStatus::FAILED; };
class IWeaver {
public:
    static constexpr const char* descriptor = "android.hardware.weaver.IWeaver";
    static std::shared_ptr<IWeaver> fromBinder(const ndk::SpAIBinder& binder) {
        return binder.get() ? std::make_shared<IWeaver>() : nullptr;
    }
    ndk::ScopedAStatus getConfig(WeaverConfig* output) {
        ++fixture.configCalls;
        *output = {static_cast<int32_t>(fixture.slots), static_cast<int32_t>(fixture.keySize), static_cast<int32_t>(fixture.valueSize)};
        return {fixture.transport};
    }
    ndk::ScopedAStatus read(int32_t, const std::vector<uint8_t>& key, WeaverReadResponse* output) {
        ++fixture.readCalls;
        if (key.size() != static_cast<uint64_t>(fixture.keySize)) std::abort();
        *output = {fixture.timeout, std::vector<uint8_t>(fixture.replySize, 0x5a), static_cast<WeaverReadStatus>(fixture.status)};
        return {fixture.transport};
    }
};
} } } }
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(T) T(const T&) = delete; T& operator=(const T&) = delete
#endif
