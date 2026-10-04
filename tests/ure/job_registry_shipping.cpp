// SPDX-License-Identifier: Apache-2.0
// Compile the shipping header without exceptions; inspect policy without I/O.
#include "lifecycle_policy.hpp"
bool compile_shipping_lifecycle_guard() {
    ure::LegacyLifecycleGuard parent("unmount"); ure::LegacyLifecycleGuard nested("unmount",&parent);
    return parent.active() && nested.active() && ure::stage_legacy_reboot();
}
int main() {
    static_assert(!ure::device_operation_coordinator_accepted());
    if(::setenv("URE_GUI_JOB_REGISTRY","/untrusted/environment/override",1)!=0)return 1;
    return std::strcmp(ure::job_registry_detail::runtime_path(),"/tmp/ure-job-registry")==0 ? 0 : 1;
}
