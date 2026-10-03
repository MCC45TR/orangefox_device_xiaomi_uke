// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"
#include "../../src/device/xiaomi/uke/display-mirror.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>
#include <thread>
inline std::map<std::string,std::string> management_variables;
inline std::mutex management_variable_mutex;
class DataManager {
public:
    static int GetValue(const std::string& key,std::string& out) {
        std::lock_guard<std::mutex> guard(management_variable_mutex); const auto found=management_variables.find(key);
        if(found==management_variables.end())return -1;
        out=found->second; return 0;
    }
    static int GetValue(const std::string& key,int& out) { std::string text; const auto status=GetValue(key,text); if(!status)out=std::stoi(text); return status; }
    static void SetValue(const std::string& key,const std::string& value) { std::lock_guard<std::mutex> guard(management_variable_mutex); management_variables[key]=value; }
    static void SetValue(const std::string& key,int value) { SetValue(key,std::to_string(value)); }
};
class PageManager { public: inline static int reloads=0; static void RequestUreReload() { ++reloads; } };
class GUIAction { public: int uremanager(std::string command); };
inline void gr_external_enable(bool) {}
inline std::array<int,4> mirror_request{};
inline int mirror_requests=0;
inline bool gr_external_configure(int w,int h,int hz,int scale) { mirror_request={w,h,hz,scale}; ++mirror_requests; return true; }
inline std::string gr_external_modes() { return {}; }
