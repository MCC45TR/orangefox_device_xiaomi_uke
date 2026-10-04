// SPDX-License-Identifier: Apache-2.0
// Compile the production installer unchanged except for the test entry name.
#define main ure_installer_main
#include "../../src/device/xiaomi/uke/recoveryctl/installer.cpp"
#undef main
