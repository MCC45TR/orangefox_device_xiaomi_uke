#pragma once
#include <cstddef>
#include <string>
namespace android::base {
inline std::string HexString(const void* data,std::size_t size) {
    const auto* bytes=static_cast<const unsigned char*>(data); const char digits[]="0123456789abcdef"; std::string result;
    result.reserve(size*2); for(std::size_t i=0;i<size;i++) { result+=digits[bytes[i]>>4]; result+=digits[bytes[i]&15]; } return result;
}
}
