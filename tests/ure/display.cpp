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
namespace rapidxml {
void parse_error_handler(const char* message,void*) { throw std::runtime_error(message); }
}
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
        // Run the actual upstream XML variable loader, including its arithmetic
        // branch. Firmware IDs, paths and JSON are native selections, not math.
        std::string defaults=R"(<variables>
          <variable name="ure_boot_model" value="poco-pad-x1"/>
          <variable name="ure_boot_profile" value="global-os3.0.303.0"/>
          <variable name="ure_default_path" value="/mnt/uke-images/lun-0.img"/>
          <variable name="ure_default_json" value="{&quot;name&quot;:&quot;before-rollback&quot;}"/>
          <variable name="ure_default_signed" value="-1"/>
          <variable name="ure_default_empty" value=""/>
          <variable name="theme_subtract" value="200-48"/>
          <variable name="theme_add" value="200+48"/>
        </variables>)";
        xml_document<> document; document.parse<0>(defaults.data()); PageSet theme;
        check(theme.LoadVariables(document.first_node("variables"))==0,"Actual variable loader failed");
        check(DataManager::GetStrValue("ure_boot_model")=="poco-pad-x1" &&
            DataManager::GetStrValue("ure_boot_profile")=="global-os3.0.303.0" &&
            DataManager::GetStrValue("ure_default_path")=="/mnt/uke-images/lun-0.img" &&
            DataManager::GetStrValue("ure_default_json")=="{\"name\":\"before-rollback\"}" &&
            DataManager::GetStrValue("ure_default_signed")=="-1" &&
            DataManager::GetStrValue("ure_default_empty").empty(),"Native URE defaults became theme arithmetic");
        check(DataManager::GetIntValue("theme_subtract")==152 && DataManager::GetIntValue("theme_add")==248,
            "Existing theme geometry arithmetic changed");
        DataManager::SetValue("ure_boot_model","xiaomi-pad-7"); theme.LoadVariables(document.first_node("variables"));
        check(DataManager::GetStrValue("ure_boot_model")=="xiaomi-pad-7","Reload overwrote the reviewed model selection");
        float width=2136.0F/1080.0F,height=1;
        ure_gui_density(width,height,2136,3200);
        check(width==0.75F && height==width && DataManager::GetIntValue("ure_ui_scale_applied")==75,"Tablet default was not compact and uniform");
        for(const auto geometry:std::array<std::array<int,2>,5>{{{2136,3200},{3200,2136},{1080,1920},{1600,2560},{2560,1600}}})for(int percent=50;percent<=100;percent+=5) {
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
            // Load the real stock variables, not a mirror of the layout math.
            // At compact density the status/menu right edge must still reach
            // the viewport and all five navigation targets must span the panel.
            std::ifstream vars(std::string(UKE_STOCK_THEME)+"/resources/vars.xml");
            std::string source((std::istreambuf_iterator<char>(vars)),std::istreambuf_iterator<char>());
            check(!source.empty(),"Stock theme fixture is unavailable");
            xml_document<> stock; stock.parse<0>(source.data());
            DataManager::SetValue("status_indent_right",24);
            check(theme.LoadVariables(stock.first_node("recovery")->first_node("variables"))==0,"Stock variables did not load");
            std::ifstream extra(UKE_DEVICE_THEME);
            std::string extra_source((std::istreambuf_iterator<char>(extra)),std::istreambuf_iterator<char>());
            check(!extra_source.empty(),"Extra theme fixture is unavailable");
            xml_document<> extra_theme; extra_theme.parse<0>(extra_source.data());
            check(theme.LoadVariables(extra_theme.first_node("recovery")->first_node("variables"))==0,"Extra variables did not load");
            const int apply_y=DataManager::GetIntValue("scale_apply_y");
            check(apply_y>0 && apply_y+128<DataManager::GetIntValue("row_nav_y") &&
                DataManager::GetIntValue("ab_h")+DataManager::GetIntValue("scale_scroll_h")<=apply_y,
                "Scale content or Apply button crosses the navigation footer");
            check(std::abs(geometry[0]-scale_theme_x(DataManager::GetIntValue("status_right_x"))-scale_theme_x(44))<=1,
                "Status bar retained the phone right edge");
            check(std::abs(geometry[0]-scale_theme_x(DataManager::GetIntValue("ab_btn1_x"))-scale_theme_x(84))<=1,
                "Trailing action button is stranded inside the viewport");
            check(DataManager::GetIntValue("content_w")==cw-96 && DataManager::GetIntValue("console_width")==cw-96,
                "Content or console retained the phone width");
            int previous_right=48;
            for(int item=1;item<=5;++item) {
                const int center=DataManager::GetIntValue("nav_item_"+std::to_string(item));
                const int slot=DataManager::GetIntValue("nav_item_w");
                check(center-slot/2>=previous_right-1 && center+slot/2<=cw-48,
                    "Navigation touch targets overlap or leave the panel");
                check(DataManager::GetIntValue("np_pill_x"+std::to_string(item))+96==center,
                    "Selection pill detached from its navigation target");
                previous_right=center+slot/2;
            }
            check(std::abs(scale_theme_x(previous_right)-scale_theme_x(cw-48))<=2,
                "Navigation stayed in the old phone corner");
            check(gui_parse_text("%fm_input_w%")==std::to_string(cw-264) &&
                gui_parse_text("%credits_width%")==std::to_string(cw-48),"Stock XML widths were not expanded");
            std::string placement=R"(<placement x="%fm_menu_x%" w="%fm_input_w%"/>)";
            xml_document<> position; position.parse<0>(placement.data());
            check(LoadAttrIntScaleX(position.first_node(),"x")==scale_theme_x(cw-126) &&
                LoadAttrIntScaleX(position.first_node(),"w")==scale_theme_x(cw-264),
                "Actual placement loader cannot resolve new stock anchors");
            check(DataManager::GetIntValue("nav_panel_y")+360==ch &&
                DataManager::GetIntValue("row_nav_y")+144==ch,"Stock bottom bar left the bottom edge");
            int footer_right=0;
            for(const auto* key:{"back_button_x","center_x","slideout_button_x"}) {
                const int center=DataManager::GetIntValue(key);
                const int half=DataManager::GetIntValue("row_navbtn_w")/2;
                check(center-half>=footer_right && center+half<=cw,"Navigation footer touch targets overlap");
                footer_right=center+half;
            }
            check(DataManager::GetIntValue("gst_line_y")==ch-72,
                "Gesture indicator is not centered in its reserved footer");
            for(const auto* splash_name:{"splash.xml","themes/sed/splash_orig.xml"}) {
            std::ifstream splash_input(std::string(UKE_STOCK_THEME)+"/"+splash_name);
            std::string splash_source((std::istreambuf_iterator<char>(splash_input)),std::istreambuf_iterator<char>());
            xml_document<> splash; splash.parse<0>(splash_source.data());
            theme.LoadVariables(splash.first_node("recovery")->first_node("variables"));
            auto* splash_page=splash.first_node("recovery")->first_node("pages")->first_node("page");
            for(auto* object=splash_page->first_node();object;object=object->next_sibling()) {
                auto* at=object->first_node("placement"); if(!at)continue;
                if(std::string(object->name())=="fill") {
                    check(std::abs(LoadAttrIntScaleX(at,"w")-geometry[0])<=1 &&
                        std::abs(LoadAttrIntScaleY(at,"h")-geometry[1])<=1,"Splash fill retains phone dimensions");
                } else {
                    check(std::abs(LoadAttrIntScaleX(at,"x")-geometry[0]/2)<=1,"Splash is not centered horizontally");
                    if(std::string(object->name())=="image")
                        check(std::abs(LoadAttrIntScaleY(at,"y")-geometry[1]/2)<=1,"Splash logo is not centered vertically");
                }
            }
            }
            std::ifstream sed_input(std::string(UKE_STOCK_THEME)+"/themes/sed/splash.xml");
            std::string sed_source((std::istreambuf_iterator<char>(sed_input)),std::istreambuf_iterator<char>());
            check(!sed_source.empty() && sed_source.find("x=\"540\"")==std::string::npos &&
                sed_source.find("<variable name=\"center_x\"")!=std::string::npos,
                "Customized splash still has fixed phone anchors");
            // Also catch unpatched literal dimensions in the real stock XML.
            for(const auto* file:{"pages/files.xml","pages/templates/navbar.xml","pages/settings.xml","resources/images.xml"}) {
                std::ifstream input(std::string(UKE_STOCK_THEME)+"/"+file);
                std::string xml((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
                check(!xml.empty(),"Responsive theme file is unavailable");
                check(xml.find("w=\"1080\"")==std::string::npos && xml.find("w=\"984\"")==std::string::npos &&
                    xml.find("w=\"1032\"")==std::string::npos && xml.find("x=\"1032\"")==std::string::npos,
                    "Stock content or gesture edge still contains a fixed phone coordinate");
            }
        }
        const auto directory=work/"settings";
        std::string menu_xml=R"(<listitem name="Open tools"/>)";
        xml_document<> menu_item; menu_item.parse<0>(menu_xml.data());
        ListItem initialized;
        initialize_actual_list_item(menu_item.first_node(),"","",initialized);
        check(!initialized.selected,"Action-only menu items inherit an empty selected value");
        GUIListBox menu;
        menu.mListItems.resize(20);
        menu.SetPageFocus(1);
        check(menu.firstDisplayedItem==0 && menu.mVisibleItems.size()==20 &&
            std::none_of(menu.mListItems.begin(),menu.mListItems.end(),[](const ListItem& item){return item.selected;}),
            "Action-only menu hides its first entries on page focus");
        menu.mListItems[0].mConditions=false; menu.NotifyVarChange("","");
        check(menu.mVisibleItems.size()==19 && menu.firstDisplayedItem==0,"Menu condition update resets its scroll position");
        GUIListBox selection;
        selection.mVariable="fixture_distribution";
        selection.mListItems.resize(3);
        for(size_t index=0;index<3;++index)selection.mListItems[index].variableValue=std::to_string(index);
        DataManager::SetValue(selection.mVariable,"2"); selection.SetPageFocus(1);
        check(selection.mListItems[2].selected && selection.firstDisplayedItem==2 && !selection.mListItems[0].selected,
            "Bound selection list no longer reveals its stored selection");
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
        DataManager::SetValue("pass_open","unlocked-fixture");
        DataManager::SetValue("ure_root","/selected-fixture-root");
        DataManager::SetValue("ure_future_selection","future-fixture");
        check(ure_gui_keep_variable("ure_root") && ure_gui_keep_variable("ure_future_selection") &&
            !ure_gui_keep_variable("ure_absent") && !ure_gui_keep_variable("tw_language"),"URE defaults overwrite selections or capture unrelated variables");
        PageManager::RequestUreReload();
        check(PageManager::reloads==0 && DataManager::flushes==0,"Reload destroyed resources or flushed settings in the action thread");
        check(PageManager::RunReload()==0 && PageManager::reloads==1 && PageManager::current_page=="ure_display" &&
            DataManager::GetIntValue("ure_ui_scale_applied")==50 && PageManager::paths.back()=="/twres/ui.xml","Deferred scale reload did not use the stock theme");
        check(PageManager::mounts==0 && DataManager::settings_reads==0 && DataManager::flushes==0,"Scale reload mounted or wrote upstream settings storage");
        check(PageManager::main_page_side_effects==0 && PageManager::mStartPage=="main" &&
            DataManager::GetStrValue("ure_root")=="/selected-fixture-root","Reload repeated startup actions, changed routing or lost selected OS state");
        check(DataManager::GetStrValue("pass_open")=="unlocked-fixture","Scale reload changed lock state");
        const auto reloads=PageManager::reloads; check(PageManager::RunReload()==0 && PageManager::reloads==reloads,"Renderer repeated a completed reload");
        DataManager::SetValue("ure_ui_scale_percent",90); PageManager::failures=1; PageManager::RequestUreReload();
        check(PageManager::RunReload()==0 && DataManager::GetIntValue("ure_ui_scale_applied")==50 &&
            DataManager::GetStrValue("ure_scale_status").find("previous setting restored")!=std::string::npos,"Failed reload did not restore the last applied density");
        PageManager::failures=2; DataManager::SetValue("ure_ui_scale_percent",90); PageManager::RequestUreReload();
        check(PageManager::RunReload()!=0 && DataManager::GetStrValue("ure_scale_status").find("restart recovery")!=std::string::npos,"Double reload failure falsely claimed restoration");
        check(PageManager::mounts==0 && DataManager::flushes==0,"Failure recovery touched Android or calibration storage");
        check(PageManager::main_page_side_effects==0 && PageManager::mStartPage=="main" &&
            DataManager::GetStrValue("ure_root")=="/selected-fixture-root","Failure recovery repeated startup actions or lost URE selection");
        ure::fs::remove_all(work); std::cout<<"Display presets, actual density/coordinate hooks, deferred reload/fallback and private settings fixtures passed; no rendering or tablet evidence.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
