// SPDX-License-Identifier: Apache-2.0
#include "mock.hpp"
#include "Weaver1.h"
#include "ure-weaver-policy.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void require(bool value, const char* reason) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); }
}
using android::vold::Weaver;
static std::array<uint8_t, 64> key{};
static bool verify(Weaver& w, uint32_t slot, size_t size, uint64_t* retry) {
    std::vector<uint8_t> output{1, 2, 3};
    const bool success = w.WeaverVerify(slot, key.data(), size, &output, retry);
    require(success ? output == std::vector<uint8_t>(32, 0x5a) : output.empty(), "partial or stale secret was published");
    return success;
}
static void reset(bool aidl) {
    fixture = {};
    fixture.aidl = aidl;
    fixture.hidl = !aidl;
}
int main() {
    fixture = {};
    { Weaver w; uint32_t output = 99; uint64_t retry = 99;
      require(!static_cast<bool>(w) && !w.GetKeySize(&output) && !verify(w, 0, key.size(), &retry), "absent service accepted");
      require(fixture.binderChecks == 1 && fixture.hidlChecks == 1 && fixture.configCalls == 0 && fixture.readCalls == 0,
              "missing service attempted crypto or repeated readiness checks"); }
    for (bool aidl : {false, true}) {
        reset(aidl);
        { Weaver w; uint64_t retry = 99; uint32_t size = 0;
          require(static_cast<bool>(w) && w.GetKeySize(&size) && size == 16 && verify(w, 15, 16, &retry) && retry == 0,
                  "valid exact-size last-slot request rejected");
          require(fixture.configCalls == 1 && fixture.readCalls == 1, "config cache or exactly-one read changed"); }
        for (int field = 0; field < 3; ++field) for (int64_t bad : {-1LL, 0LL, 4097LL, 0x7fffffffLL, 0xffffffffLL}) {
            reset(aidl);
            if (field == 0) fixture.slots = bad;
            if (field == 1) fixture.keySize = bad;
            if (field == 2) fixture.valueSize = bad;
            Weaver w; uint64_t retry = 99;
            require(!verify(w, 0, key.size(), &retry) && fixture.readCalls == 0, "invalid config reached hardware");
        }
        for (int64_t bad : {65, 128}) {
            reset(aidl); fixture.keySize = bad;
            Weaver w; uint64_t retry = 0;
            uint32_t admittedSize = 0;
            require(!w.GetKeySize(&admittedSize), "oversized key config was cached");
            require(!verify(w, 0, key.size(), &retry) && fixture.readCalls == 0, "oversized key config reached hardware");
        }
        reset(aidl);
        { Weaver w; uint64_t retry = 99;
          require(!verify(w, 16, 16, &retry) && fixture.readCalls == 0, "slot bound bypassed");
          require(!verify(w, UINT32_MAX, 16, &retry) && fixture.readCalls == 0, "slot overflow reached hardware");
          require(!verify(w, 0, 15, &retry) && fixture.readCalls == 0, "short key reached hardware");
          std::vector<uint8_t> output{3};
          require(!w.WeaverVerify(0, nullptr, 16, &output, &retry) && output.empty() && fixture.readCalls == 0,
                  "null key reached hardware");
          require(!w.WeaverVerify(0, key.data(), 16, nullptr, &retry) && fixture.readCalls == 0,
                  "null payload reached hardware");
          require(!w.WeaverVerify(0, key.data(), 16, &output, nullptr) && fixture.readCalls == 0,
                  "missing retry output reached hardware");
          require(!w.GetSlots(nullptr) && !w.GetKeySize(nullptr) && !w.GetValueSize(nullptr), "null config output accepted"); }
        for (int status : {1, 2, 3, 99}) for (int64_t delay : {0LL, 42000LL}) {
            reset(aidl); fixture.status = status; fixture.timeout = delay;
            Weaver w; uint64_t retry = 99;
            require(!verify(w, 0, 16, &retry) && retry == static_cast<uint64_t>(delay) && fixture.readCalls == 1,
                    "throttle or failure caused lost delay, secret publication or retry");
        }
        for (size_t size : {0U, 1U, 31U, 33U, 4097U}) {
            reset(aidl); fixture.replySize = size;
            Weaver w; uint64_t retry = 0;
            require(!verify(w, 0, 16, &retry) && fixture.readCalls == 1, "invalid reply size accepted");
        }
        reset(aidl); fixture.timeout = 1000;
        { Weaver w; uint64_t retry = 0;
          require(!verify(w, 0, 16, &retry) && retry == 1000 && fixture.readCalls == 1, "success with delay accepted"); }
        reset(aidl);
        { Weaver w; uint64_t retry = 0; uint32_t size = 0;
          require(w.GetKeySize(&size), "valid config refused"); fixture.transport = false;
          require(!verify(w, 0, 16, &retry) && fixture.readCalls == 1, "failed transaction accepted"); }
    }
    for (int64_t invalid : {-1LL, 0x7fffffffLL, 0xffffffffLL, 0x100000000LL}) {
        reset(true); fixture.timeout = invalid;
        Weaver w; uint64_t retry = 0;
        require(!verify(w, 0, 16, &retry), "invalid signed AIDL delay accepted");
    }
    reset(false); fixture.timeout = UINT32_MAX; fixture.status = 3;
    { Weaver w; uint64_t retry = 0;
      require(!verify(w, 0, 16, &retry) && retry == UINT32_MAX, "HIDL delay truncated"); }
    for (bool duplicate : {false, true}) {
        reset(false); fixture.callback = duplicate; fixture.duplicate = duplicate;
        { Weaver w; uint64_t retry = 0;
          require(!verify(w, 0, 16, &retry) && fixture.readCalls == 0, "missing or duplicate config callback accepted"); }
        reset(false);
        { Weaver w; uint64_t retry = 0; uint32_t size = 0;
          require(w.GetKeySize(&size), "valid HIDL config refused");
          fixture.callback = duplicate; fixture.duplicate = duplicate;
          require(!verify(w, 0, 16, &retry) && fixture.readCalls == 1, "missing or duplicate read callback accepted"); }
    }
    std::puts("Actual Weaver production source: absent services, bounded configs/key/slots, single reads, AIDL/HIDL timeout and malformed-reply controls passed.");
}
