// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"
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
class PageManager { public: static void RequestUreReload() {} };
class GUIAction { public: int uremanager(std::string command); };
inline void gr_external_enable(bool) {}
inline bool gr_external_select(int,int,int) { return false; }
inline std::string gr_external_modes() { return {}; }
namespace uke_display { inline bool parse_selection(const std::string&,const std::string&,int&,int&,int&) { return false; } }
