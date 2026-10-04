// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"

namespace ure {
Value rescue_resource_policy(const Value& request);
Value rescue_resource_capabilities();
void rescue_process_limits(const Value& policy);

// Internal backend, selected by the compiled factory. No plan, environment
// variable or CLI argument can supply a backend, controller path or substitute
// enforcement. Host lifetime fixtures wrap the factory only at link time.
class RescueResources {
public:
    virtual ~RescueResources() = default;
    virtual void attach_worker(pid_t pid)=0;
    virtual void close_in_child() noexcept=0;
    virtual Value observation() const=0;
    virtual void kill_all() noexcept=0;
    virtual bool finish() noexcept=0;
};
extern "C" RescueResources* ure_create_rescue_resources(const Value* policy,const char* operation_id);
}
