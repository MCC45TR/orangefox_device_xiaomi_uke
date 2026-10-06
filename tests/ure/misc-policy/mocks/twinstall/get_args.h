#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <bootloader_message/bootloader_message.h>
extern std::string stage;
class args { public: static std::vector<std::string> get_args(const int* argc,char*** const argv); };
