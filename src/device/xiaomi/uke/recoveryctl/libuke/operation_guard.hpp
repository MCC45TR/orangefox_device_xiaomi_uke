// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "operation_lease.hpp"

namespace ure {
Value operation_target(int fd, const std::string& role);
Value operation_targets(const Value& identity);
OperationBinding operation_binding(const std::string& operation, const Value& plan,
    const fs::path& journal, const Value& targets);

// Every compound operation passes its retained token explicitly. Nested helpers
// never acquire another domain or infer ownership from a PID, thread or mutex.
class ManagedOperation {
    std::unique_ptr<OperationLease> owned_;
    OperationLease* lease_ = nullptr;
public:
    ManagedOperation(const OperationBinding& binding, bool recovery = false,
        OperationLease* parent = nullptr);
    OperationLease& token() const;
    void begin(const std::string& phase);
    Value finish(Value result, bool verified, bool cleanup_complete,
        const std::string& terminal = {});
};
} // namespace ure
