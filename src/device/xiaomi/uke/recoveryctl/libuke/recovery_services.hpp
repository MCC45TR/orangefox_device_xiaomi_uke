// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"
#include <array>
#include <optional>
#include <span>
#include <mutex>

namespace ure {
Value recovery_services_status(const Root& system);

// Providers supply calibrated SI acceleration in the reviewed natural-display
// coordinate system. A GUI preference never establishes provider readiness.
struct SensorFrame {
    std::uint64_t provider_generation=0,sequence=0,sample_ms=0;
    std::optional<double> lux;
    std::optional<std::array<double,3>> acceleration;
};
struct AutomaticDisplayDecision {
    std::optional<int> brightness_percent,rotation_degrees;
    const char* reason="sensor-unavailable";
};
class AutomaticDisplayController {
    std::uint64_t generation_=0,last_sequence_=0,last_sample_=0,candidate_since_=0,last_brightness_=0,manual_until_=0;
    bool brightness_=false,rotation_=false,have_lux_=false;
    double filtered_lux_=0;
    int candidate_=-1,applied_rotation_=-1,applied_brightness_=-1;
public:
    void configure(bool brightness,bool rotation,std::uint64_t reviewed_provider_generation);
    void manual_override(std::uint64_t now_ms);
    AutomaticDisplayDecision update(const SensorFrame&,std::uint64_t now_ms);
};

enum class EncryptedSystem { AndroidFbe,LinuxLuks };
struct UnlockReadiness {
    bool accepted=false,read_only_access=false;
    std::uint64_t provider_generation=0;
};
struct UnlockOutcome {
    bool authenticated=false,read_only_access=false;
    unsigned retry_after_seconds=0;
};
// Device adapters must retain verified target/firmware identities and their
// own operation leases. They may not format, export keys or remount metadata.
// There is no property, environment or JSON provider-registration shortcut.
class DecryptionProvider {
public:
    virtual ~DecryptionProvider()=default;
    virtual EncryptedSystem system() const noexcept=0;
    virtual UnlockReadiness readiness() const=0;
    virtual UnlockOutcome unlock(std::span<const unsigned char>,unsigned android_user)=0;
    virtual bool close() noexcept=0;
    virtual void quarantine() noexcept=0;
};
class DecryptionSession {
    std::mutex mutex_;
    std::shared_ptr<DecryptionProvider> provider_;
    std::uint64_t retry_until_=0;
    bool open_=false,quarantined_=false;
public:
    explicit DecryptionSession(std::shared_ptr<DecryptionProvider>);
    ~DecryptionSession();
    DecryptionSession(const DecryptionSession&)=delete;
    DecryptionSession& operator=(const DecryptionSession&)=delete;
    UnlockOutcome unlock_from_fd(int secret_fd,unsigned android_user,std::uint64_t now_seconds);
    bool close() noexcept;
};
} // namespace ure
