// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"
#include "localization_fixture.hpp"
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
#include <poll.h>
inline std::map<std::string,std::string> management_variables;
inline std::mutex management_variable_mutex;
inline const auto management_ui_thread=std::this_thread::get_id();
inline std::atomic<unsigned> management_foreign_reads{0},management_foreign_writes{0};
class DataManager {
public:
    static int GetValue(const std::string& key,std::string& out) {
        if(std::this_thread::get_id()!=management_ui_thread)++management_foreign_reads;
        std::lock_guard<std::mutex> guard(management_variable_mutex); const auto found=management_variables.find(key);
        if(found==management_variables.end())return -1;
        out=found->second; return 0;
    }
    static int GetValue(const std::string& key,int& out) { std::string text; const auto status=GetValue(key,text); if(!status)out=std::stoi(text); return status; }
    static void SetValue(const std::string& key,const std::string& value) {
        if(std::this_thread::get_id()!=management_ui_thread)++management_foreign_writes;
        std::lock_guard<std::mutex> guard(management_variable_mutex); management_variables[key]=value;
    }
    static void SetValue(const std::string& key,int value) { SetValue(key,std::to_string(value)); }
};
class PageManager { public: inline static int reloads=0; static void RequestUreReload() { ++reloads; } };
class GUIAction { public: int uremanager(std::string command); };
void ure_gui_poll_jobs();
void ure_gui_shutdown_jobs();
// The real adapter accepts asynchronous work; fixtures explicitly collect it.
// Never hide the distinction between queue admission and backend completion.
inline int run_management(GUIAction& action,const std::string& command) {
    std::string previous; DataManager::GetValue("ure_job_id",previous);
    const auto admitted=action.uremanager(command); if(admitted)return admitted;
    std::string current; DataManager::GetValue("ure_job_id",current);
    if(current.empty() || current==previous)return admitted;
    const auto deadline=ure::monotonic_ms()+180000;
    while(ure::monotonic_ms()<deadline) {
        static_cast<void>(action.uremanager("job-collect"));
        std::string active,pending; DataManager::GetValue("ure_job_active",active); DataManager::GetValue("ure_job_result_pending",pending);
        if(active=="0" && pending!="1") { int code=1; DataManager::GetValue("ure_job_exit_code",code); return code; }
        ::poll(nullptr,0,2);
    }
    throw std::runtime_error("Actual management callback did not finish its bounded private fixture");
}
inline void gr_external_enable(bool) {}
inline std::array<int,4> mirror_request{};
inline int mirror_requests=0;
inline bool gr_external_configure(int w,int h,int hz,int scale) { mirror_request={w,h,hz,scale}; ++mirror_requests; return true; }
inline std::string gr_external_modes() { return {}; }
