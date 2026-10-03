// SPDX-License-Identifier: Apache-2.0
#include "scale-preview.h"
#include <iostream>
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
}
int main() {
    try {
        unsigned reviewed=0;
        for(const float density:{0.25F,0.375F,0.5F,0.6F,0.8F})for(int applied=50;applied<=100;applied+=5) {
            preview_density=density;
            preview_source.font.height=std::max(1,scale_theme_min(42));
            preview_detail.font.height=std::max(1,scale_theme_min(28));
            xml_node<> node; node.w=1600; node.h=scale_theme_min(640);
            GUIObject* object=nullptr; RenderObject* renderer=nullptr;
            ure_create_scale_preview(&node,object,renderer);
            layout_variables["ure_ui_scale_applied"]=std::to_string(applied);
            layout_variables["ure_ui_scale_percent"]=std::to_string(applied);
            int last_height=0;
            for(int selected=50;selected<=100;selected+=5) {
                layout_variables["ure_scale_choice"]=std::to_string(selected);
                layout_fills.clear(); preview_texts.clear();
                check(renderer->Update()==2,"Selection did not invalidate the sample");
                check(renderer->Render()==0 && preview_texts.size()==3,"Sample text/button disappeared");
                const auto allocations=preview_allocations;
                check(renderer->Update()==0 && preview_allocations==allocations && preview_live_fonts==2,
                    "Unchanged sample allocated fonts or retained old references");
                check(preview_texts[0].height>=last_height && preview_texts[1].height<=preview_texts[0].height,
                    "Sample size is not monotonic or description is oversized");
                last_height=preview_texts[0].height;
                for(const auto& rect:layout_fills)check(rect.x>=node.x && rect.y>=node.y && rect.w>0 && rect.h>0 &&
                    rect.x+rect.w<=node.x+node.w && rect.y+rect.h<=node.y+node.h,"Sample draw escapes its frame");
                for(const auto& text:preview_texts)check(text.width>0 && text.x>=node.x && text.y>=node.y &&
                    text.x+text.width<=node.x+node.w && text.y+text.height<=node.y+node.h,"Sample text escapes its frame");
                check((value("ure_scale_warning").find("hard to tap")!=std::string::npos)==(selected<=70),
                    "Small-target warning has the wrong threshold");
                check(value("ure_ui_scale_applied")==std::to_string(applied) && value("ure_ui_scale_percent")==std::to_string(applied),
                    "Preview changed the real interface scale");
                ++reviewed;
            }
            layout_variables["ure_scale_choice"]="101";
            check(renderer->Update()==2 && preview_live_fonts==0,"Invalid sample retained font references");
            layout_fills.clear(); preview_texts.clear(); renderer->Render();
            check(preview_texts.empty(),"Invalid sample rendered selected content");
            delete object;
            check(preview_live_fonts==0,"Destroyed sample leaked font references");
        }
        preview_missing_font=true;
        xml_node<> node; GUIObject* object=nullptr; RenderObject* renderer=nullptr;
        ure_create_scale_preview(&node,object,renderer);
        layout_variables["ure_scale_choice"]="75"; layout_variables["ure_ui_scale_applied"]="75";
        renderer->Render(); delete object;
        check(preview_live_fonts==0,"Missing resources retained fonts");
        std::cout<<"Actual size preview: "<<reviewed<<" selections, bounded drawing, small-target warnings and font ownership passed.\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
