#pragma once
#include <fixture-external.hpp>
namespace android::base {
inline std::string GetProperty(const std::string& key,const std::string& fallback) {
    return key=="ro.boot.slot_suffix" ? fixture::slot_suffix : fallback;
}
}
