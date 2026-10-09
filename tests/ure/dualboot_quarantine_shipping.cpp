// SPDX-License-Identifier: Apache-2.0
#include "lifecycle_policy.hpp"
#include <cstdio>
#ifdef URE_HOST_POLICY_FIXTURE
#error This compile control must use the shipping fixed-path policy.
#endif
int main() {
    if(::setenv("URE_DUALBOOT_QUARANTINE","/tmp/untrusted-policy-override",1)!=0)return 1;
    if(std::strcmp(ure::dualboot_quarantine_detail::path(),"/tmp/uke-dualboot")!=0 || ure::dualboot_quarantine_detail::owner()!=0)return 1;
    std::puts("PASS shipping quarantine uses the fixed root-owned location; no marker or device was accessed."); return 0;
}
