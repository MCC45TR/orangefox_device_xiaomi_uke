// SPDX-License-Identifier: Apache-2.0
#include "layout-hooks.h"
#include <iostream>
#include <memory>
namespace {
void check(bool ok,const std::string& message) { if(!ok)throw std::runtime_error(message); }
ure::Value graph() {
    ure::Value out; out["format"]="ure-partition-layout"; out["pool"]["bytes"]=Json::UInt64(1000); out["rows"]=ure::Value(Json::arrayValue);
    unsigned cursor=0; for(const auto& [name,bytes]:std::array<std::pair<const char*,unsigned>,4>{{{"userdata",550},{"esp",50},{"linux",300},{"windows",100}}}) {
        ure::Value row; row["role"]=name; row["pool_offset"]=Json::UInt64(cursor); row["bytes"]=Json::UInt64(bytes); cursor+=bytes; out["rows"].append(row);
    } return out;
}
}
int main() {
    try {
        xml_node<> node; GUIObject* object=nullptr; RenderObject* render=nullptr; ure_create_partition_map(&node,object,render); std::unique_ptr<GUIObject> owner(object);
        check(object && render,"Widget factory did not register both interfaces"); layout_variables["ure_layout_graph"]=ure::json(graph());
        check(render->Update()==2 && render->Update()==0,"Widget did not invalidate only changed state"); render->Render();
        check(layout_fills.size()==6 && layout_fills[1].x==11 && layout_fills[1].w==550 && layout_fills[2].w==50 && layout_fills[3].w==300 && layout_fills[4].w==100,"Actual widget changed allocation proportions");
        check(layout_fills[1].color==std::array<int,4>{164,116,208,255} && layout_fills[2].color==std::array<int,4>{210,160,64,255},"Graph role legend differs from rendering");
        render->SetRenderPos(31,47,333,64); check(render->Update()==2,"Resized graph did not recalculate its pixel widths"); layout_fills.clear(); render->Render();
        unsigned used=0; for(std::size_t i=1;i<layout_fills.size();++i) { const auto& rect=layout_fills[i]; check(rect.x==31+static_cast<int>(used) && rect.y==47 && rect.w>=0,"Resized graph has gaps or escaped its placement"); used+=static_cast<unsigned>(rect.w); }
        check(used==333,"Resized graph has a misleading capacity width");
        node.visible=false; layout_fills.clear(); render->Render(); check(layout_fills.empty(),"Hidden graph rendered"); node.visible=true;
        for(const auto& invalid:{std::string("broken"),std::string(65537,'x')}) {
            layout_variables["ure_layout_graph"]=invalid; render->Update(); layout_fills.clear(); render->Render(); check(layout_fills.size()==1,"Invalid graph retained a stale allocation preview");
        }
        auto bad=graph(); bad["rows"][1]["pool_offset"]=Json::UInt64(0); layout_variables["ure_layout_graph"]=ure::json(bad); render->Update(); layout_fills.clear(); render->Render();
        check(layout_fills.size()==1,"Overlapping graph painted misleading partitions");
        std::cout<<"Actual partition graph widget: factory registration, allocation colors/proportions, resize invalidation, conditional rendering and bounded invalid-state clearing passed; host graphics stand-ins only.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
