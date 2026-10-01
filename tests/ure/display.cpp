// SPDX-License-Identifier: Apache-2.0
#include "display-hooks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal: "+error.code); return; }
    throw std::runtime_error("Expected refusal: "+code);
}
}
int main() {
    std::array<char,40> pattern{}; const std::string text="/tmp/ure-display-tests-XXXXXX"; std::copy(text.begin(),text.end(),pattern.begin());
    const auto* created=::mkdtemp(pattern.data()); if(!created)return 1; const ure::fs::path work(created);
    try {
        for(const auto* invalid:{"","49","101","-75","+75","75.0"," 75","75 ","0750","9999999999","nan"})
            reject([&]{ure::display_scale_parse(invalid);},"invalid-scale");
        reject([&]{ure::display_layout(0,3200,1,1,75);},"invalid-display");
        reject([&]{ure::display_layout(2136,3200,std::numeric_limits<double>::infinity(),1,75);},"invalid-display");
        reject([&]{ure::display_layout(2136,3200,0.001,1,75);},"invalid-display");
        DataManager::SetValue("tw_language","en"); DataManager::SetValue("pass_open","unlocked-fixture");
        float width=2136.0F/1080.0F,height=1;
        ure_gui_density(width,height,2136,3200);
        check(width==0.75F && height==width && DataManager::GetIntValue("ure_ui_scale_applied")==75,"Tablet default was not compact and uniform");
        for(const auto geometry:std::array<std::array<int,2>,3>{{{2136,3200},{3200,2136},{1080,1920}}})for(const int percent:{50,60,70,75,80,90,100}) {
            DataManager::SetValue("ure_ui_scale_percent",percent);
            width=static_cast<float>(geometry[0])/1080.0F; height=static_cast<float>(geometry[1])/3200.0F;
            ure_gui_density(width,height,geometry[0],geometry[1]); set_scale_values(width,height);
            check(get_scale_w()==get_scale_h(),"Icons or touch rectangles are stretched");
            const int cw=DataManager::GetIntValue("ure_canvas_width"),ch=DataManager::GetIntValue("ure_canvas_height");
            check(std::abs(scale_theme_x(cw)-geometry[0])<=1 && std::abs(scale_theme_y(ch)-geometry[1])<=1,"Full-screen edge moved inside the display");
            std::string value;
            check(ure_gui_variable("screen_w",value) && value==std::to_string(cw),"Theme width reverted to the phone canvas");
            check(ure_gui_variable("screen_original_h",value) && value==std::to_string(ch),"Bottom anchors retained the original phone height");
            check(ure_gui_variable("input_w",value) && value==std::to_string(cw-96),"Input field did not expand to the tablet viewport");
            check(!ure_gui_variable("ure_root",value),"Unrelated root selection was overwritten by a display hook");
            check(gui_parse_text("Scale: %ure_ui_scale_applied%%%") == "Scale: "+std::to_string(percent)+"%","Current percentage label is malformed");
            check(gui_parse_text("%screen_original_h%-144")==std::to_string(ch)+"-144","XML bottom-anchor expressions use the wrong canvas");
            // Actual upstream scaling functions are shared by rendered object
            // dimensions and hit rectangles. Check center/edges and a row at
            // the bottom of the canvas for all presets and orientations.
            const int x=scale_theme_x(48),y=scale_theme_y(ch-144),w=scale_theme_x(cw-96),h=scale_theme_y(96);
            const int touch_x=x+w/2,touch_y=y+h/2;
            check(w>0 && h>0 && touch_x>=x && touch_x<x+w && touch_y>=y && touch_y<y+h &&
                touch_x<geometry[0] && touch_y<geometry[1],"Scaled bottom row has an unreachable hit rectangle");
            check(scale_theme_min(48)==scale_theme_y(48),"Font density differs from control density");
        }
        const auto directory=work/"settings";
        ure::display_settings_save(directory,60);
        check(ure::display_settings_load(directory)["scale_percent"]==60,"Saved scale did not survive a fresh read");
        check((ure::Root(work).stat("settings").st_mode&07777)==0700 && (ure::Root(directory).stat("display.json").st_mode&07777)==0600,"Settings are not private");
        ure::display_settings_save(directory,80);
        check(ure::display_settings_load(directory)["scale_percent"]==80,"Scale replacement failed");
        const auto before=ure::Root(directory).read("display.json");
        reject([&]{ure::display_settings_save(directory,101);},"invalid-scale");
        check(before==ure::Root(directory).read("display.json"),"Invalid scale changed a saved record");
        check(::chmod((directory/"display.json").c_str(),0644)==0,"Cannot change fixture permissions");
        reject([&]{ure::display_settings_load(directory);},"unsafe-settings");
        reject([&]{ure::display_settings_save(directory,70);},"unsafe-settings");
        check(::chmod((directory/"display.json").c_str(),0600)==0,"Cannot restore fixture permissions");
        check(::link((directory/"display.json").c_str(),(work/"alias").c_str())==0,"Cannot create hardlink fixture");
        reject([&]{ure::display_settings_load(directory);},"unsafe-settings");
        check(::unlink((work/"alias").c_str())==0,"Cannot remove hardlink fixture");
        check(::symlink("settings",(work/"symlink").c_str())==0,"Cannot create symlink fixture");
        reject([&]{ure::display_settings_save(work/"symlink",75);},"path-unavailable");
        { std::ofstream bad(directory/"display.json"); bad<<"{\"schema\":1,\"format\":\"ure-display-settings\",\"scale_percent\":101,\"uniform_density\":true}"; }
        reject([&]{ure::display_settings_load(directory);},"invalid-scale");
        DataManager::SetValue("ure_ui_scale_applied",75); DataManager::SetValue("ure_ui_scale_percent",50);
        PageManager::RequestUreReload();
        check(PageManager::reloads==0 && DataManager::flushes==0,"Reload destroyed resources or flushed settings in the action thread");
        check(PageManager::RunReload()==0 && PageManager::reloads==1 && PageManager::current_page=="ure_display" &&
            DataManager::GetIntValue("ure_ui_scale_applied")==50 && PageManager::paths.back()=="/twres/ui.xml","Deferred scale reload did not use the stock theme");
        check(PageManager::mounts==0 && DataManager::settings_reads==0 && DataManager::flushes==0,"Scale reload mounted or wrote upstream settings storage");
        check(DataManager::GetStrValue("pass_open")=="unlocked-fixture","Scale reload changed lock state");
        const auto reloads=PageManager::reloads; check(PageManager::RunReload()==0 && PageManager::reloads==reloads,"Renderer repeated a completed reload");
        DataManager::SetValue("ure_ui_scale_percent",90); PageManager::failures=1; PageManager::RequestUreReload();
        check(PageManager::RunReload()==0 && DataManager::GetIntValue("ure_ui_scale_applied")==50 &&
            DataManager::GetStrValue("ure_scale_status").find("previous setting restored")!=std::string::npos,"Failed reload did not restore the last applied density");
        PageManager::failures=2; DataManager::SetValue("ure_ui_scale_percent",90); PageManager::RequestUreReload();
        check(PageManager::RunReload()!=0 && DataManager::GetStrValue("ure_scale_status").find("restart recovery")!=std::string::npos,"Double reload failure falsely claimed restoration");
        check(PageManager::mounts==0 && DataManager::flushes==0,"Failure recovery touched Android or calibration storage");
        ure::fs::remove_all(work); std::cout<<"Display presets, actual density/coordinate hooks, deferred reload/fallback and private settings fixtures passed; no rendering or tablet evidence.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
