#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
namespace android::base {
inline std::string StringPrintf(const char* format,...) {
    va_list args; va_start(args,format); va_list copy; va_copy(copy,args);
    const int n=std::vsnprintf(nullptr,0,format,copy); va_end(copy);
    if(n<0) { va_end(args); return {}; }
    std::vector<char> bytes(static_cast<std::size_t>(n)+1); std::vsnprintf(bytes.data(),bytes.size(),format,args); va_end(args);
    return std::string(bytes.data(),static_cast<std::size_t>(n));
}
}
