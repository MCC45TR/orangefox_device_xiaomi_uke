// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace fixture { extern std::string misc_path; extern std::string slot_suffix; }
namespace fixture::external {
struct Log { template<class T> Log& operator<<(const T&) { return *this; } };
}
