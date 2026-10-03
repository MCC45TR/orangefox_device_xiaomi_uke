// SPDX-License-Identifier: Apache-2.0
// Host-only platform stand-ins. Tests compile the actual project density hooks
// and reviewed upstream reload/coordinate functions, not a second algorithm.
#pragma once
#include "uke.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "rapidxml.hpp"
using namespace rapidxml;
using std::string;

class DataManager {
public:
    inline static std::map<std::string,std::string> values;
    inline static const std::map<std::string,std::string> constants{{"center_y","1600"},{"screen_original_h","3200"}};
    inline static int flushes=0,settings_reads=0;
    static int SetValue(const std::string& name,const std::string& value,int = 0) {
        if(constants.contains(name))return -1;
        values[name]=value; return 0;
    }
    static int SetValue(const std::string& name,int value,int = 0) { return SetValue(name,std::to_string(value)); }
    static int GetValue(const std::string& name,std::string& result) {
        const auto key=name.size()>2 && name.front()=='%' && name.back()=='%' ? name.substr(1,name.size()-2) : name;
        const auto fixed=constants.find(key); if(fixed!=constants.end()) { result=fixed->second; return 0; }
        const auto item=values.find(key); if(item==values.end())return -1; result=item->second; return 0;
    }
    static int GetValue(const std::string& name,int& result) {
        std::string value; if(GetValue(name,value)!=0)return -1;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        return parsed.ec==std::errc() && parsed.ptr==value.data()+value.size() ? 0 : -1;
    }
    static int GetIntValue(const std::string& name) { int result=0; GetValue(name,result); return result; }
    static std::string GetStrValue(const std::string& name) { std::string result; GetValue(name,result); return result; }
    static std::string GetSettingsStoragePath() { return "/fixture-settings"; }
    static void Flush() { ++flushes; }
    static void ReadSettingsFile() { ++settings_reads; }
};
class PageSet {
public:
    int LoadVariables(xml_node<>*);
};
struct ListItem {
    std::string variableValue,variableName,displayName,unparsedName;
    bool selected=false,mConditions=true;
    void* action=nullptr;
};
class GUIScrollList {
public:
    int firstDisplayedItem=0;
    void NotifyVarChange(const std::string&,const std::string&) {}
    void SetPageFocus(int) {}
    void SetVisibleListLocation(size_t index) { firstDisplayedItem=static_cast<int>(index); }
};
inline bool UpdateConditions(bool visible,const std::string&) { return visible; }
class GUIListBox:public GUIScrollList {
public:
    std::string mVariable,currentValue;
    std::vector<ListItem> mListItems;
    std::vector<size_t> mVisibleItems;
    bool requireReload=false,isCheckList=false,isTextParsed=false;
    int mUpdate=0;
    bool isConditionTrue() const { return true; }
    void CreateEncryptUsersList() {}
    void ReadFileToList(const char*) {}
    int NotifyVarChange(const std::string&,const std::string&);
    void SetPageFocus(int);
};
void initialize_actual_list_item(xml_node<>*,const std::string&,const std::string&,ListItem&);
inline std::string value(const std::string& name) { return DataManager::GetStrValue(name); }
void ure_gui_density(float&,float&,int,int);
bool ure_gui_variable(const std::string&,std::string&);
bool ure_gui_keep_variable(const std::string&);
std::string gui_parse_text(std::string);
extern "C" void set_scale_values(float,float);
extern "C" int scale_theme_x(int);
extern "C" int scale_theme_y(int);
extern "C" int scale_theme_min(int);
extern "C" float get_scale_w();
extern "C" float get_scale_h();
inline int gr_fb_width() { return scale_theme_x(DataManager::GetIntValue("ure_canvas_width")); }
inline int gr_fb_height() { return scale_theme_y(DataManager::GetIntValue("ure_canvas_height")); }
std::string LoadAttrString(xml_node<>*,const char*,const char* = "");
int LoadAttrInt(xml_node<>*,const char*,int = 0);
int LoadAttrIntScaleX(xml_node<>*,const char*,int = 0);
int LoadAttrIntScaleY(xml_node<>*,const char*,int = 0);

struct MockResources {
    std::string FindString(const std::string& name) const { return name; }
    std::string FindString(const std::string&,const std::string& fallback) const { return fallback; }
};
class PageManager {
public:
    inline static std::atomic<bool> mReloadTheme{false};
    inline static int reloads=0,failures=0,renders=0,mounts=0;
    inline static int main_page_side_effects=0;
    inline static std::string mStartPage="main";
    inline static std::string current_page;
    inline static std::vector<std::string> paths;
    static int RunReload();
    static void RequestReload();
    static void RequestUreReload();
    static int ReloadPackage(const std::string&,const std::string& path) {
        ++reloads; paths.push_back(path);
        if(mStartPage!="ure_display")++main_page_side_effects;
        if(!ure_gui_keep_variable("ure_root"))DataManager::SetValue("ure_root","/default-fixture-root");
        DataManager::SetValue("pass_open","0"); // Actual stock variables reset this during package loading.
        float width=2136.0F/1080.0F,height=1.0F;
        ure_gui_density(width,height,2136,3200); set_scale_values(width,height);
        // Simulate a failure after theme density was already recomputed.
        if(failures>0) { --failures; return 1; } return 0;
    }
    static void LoadLanguage(const std::string&) {}
    static const MockResources* GetResources() { static const MockResources resources; return &resources; }
};
struct MockPartitionManager {
    int Mount_By_Path(const char*,int) { ++PageManager::mounts; return 0; }
};
inline MockPartitionManager PartitionManager;
inline void gui_forceRender() { ++PageManager::renders; }
inline void gui_changePage(const std::string& page) { PageManager::current_page=page; }
