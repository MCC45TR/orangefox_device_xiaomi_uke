// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"

namespace ure {

// The caller retains and revalidates every target descriptor. This binding is
// the immutable operation identity, not evidence of firmware acceptance.
struct OperationBinding {
    std::string operation;
    std::string operation_id;
    std::string plan_sha256;
    Value targets;
    Value journal;
};
enum class LeaseAdmission { NewOperation, RecoverSameOperation, RecoverOrNewSameOperation };

// One process-wide domain shared by every cooperating management operation.
// Host-only: configuration must precede use and cannot switch domains later.
// Android has no accepted persistent owner backend and rejects configuration.
// URE_OPERATION_COORDINATOR is an equivalent host-only startup setting.
void configure_operation_coordinator(const fs::path& path);
Value operation_lease_status();

class OperationLease {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit OperationLease(std::unique_ptr<Impl> impl);
public:
    static OperationLease acquire(const OperationBinding& binding,
        LeaseAdmission admission = LeaseAdmission::NewOperation);
    ~OperationLease();
    OperationLease(OperationLease&&) noexcept;
    OperationLease& operator=(OperationLease&&) noexcept;
    OperationLease(const OperationLease&) = delete;
    OperationLease& operator=(const OperationLease&) = delete;

    // Pass this exact token to nested helpers; there is no PID/thread borrowing.
    void require_active() const;
    void require_binding(const OperationBinding& binding) const;
    const OperationBinding& binding() const;
    bool has_retained_intent() const;

    // Publish retained intent before any target effect. Default destruction
    // releases the kernel flock but preserves any published unresolved owner.
    void checkpoint(const std::string& phase);
    // The caller supplies independent byte/metadata and child/mount oracles.
    // Require verified:true, cleanup_complete:true and a supported terminal
    // state. No exception handler automatically clears an unresolved owner.
    void release_verified(const Value& terminal_result);
};

class OwnerControlLease {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit OwnerControlLease(std::unique_ptr<Impl> impl);
public:
    static OwnerControlLease acquire(const OperationBinding& binding);
    ~OwnerControlLease();
    OwnerControlLease(OwnerControlLease&&) noexcept;
    OwnerControlLease& operator=(OwnerControlLease&&) noexcept;
    OwnerControlLease(const OwnerControlLease&) = delete;
    OwnerControlLease& operator=(const OwnerControlLease&) = delete;
    void require_active() const;
    void require_binding(const OperationBinding& binding) const;
};

class LifecycleLease {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit LifecycleLease(std::unique_ptr<Impl> impl);
public:
    // Hold through the complete unmount/reboot effect; a boolean busy check
    // would permit a new operation between its check and the effect.
    static LifecycleLease acquire(const std::string& action);
    ~LifecycleLease();
    LifecycleLease(LifecycleLease&&) noexcept;
    LifecycleLease& operator=(LifecycleLease&&) noexcept;
    LifecycleLease(const LifecycleLease&) = delete;
    LifecycleLease& operator=(const LifecycleLease&) = delete;
    void require_active() const;
};

} // namespace ure
