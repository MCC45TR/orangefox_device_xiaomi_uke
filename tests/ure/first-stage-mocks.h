// SPDX-License-Identifier: Apache-2.0
// External bootconfig/sysfs seams only. The production selector/resumer is compiled intact.
#pragma once
#include <fcntl.h>
#include <iostream>
#include <string>
#include <android-base/unique_fd.h>

bool IsRecoveryMode();
namespace android::fs_mgr {
void GetBootconfigFromString(const std::string&, const std::string&, std::string*);
}
namespace android::base {
bool WriteStringToFd(const std::string&, int);
}
#define PLOG(severity) std::cerr
