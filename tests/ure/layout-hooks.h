// SPDX-License-Identifier: Apache-2.0
// Host platform stand-ins; the widget implementation is extracted unchanged.
#pragma once
#include "uke.h"
#include <array>
#include <map>
#include <string>
#include <vector>
template<class Char=char> struct xml_node { int x=11,y=19,w=1000,h=64; bool visible=true; };
inline xml_node<>* FindNode(xml_node<>* node,const char*) { return node; }
inline void LoadPlacement(xml_node<>* node,int* x,int* y,int* w,int* h) { *x=node->x; *y=node->y; *w=node->w; *h=node->h; }
inline std::map<std::string,std::string> layout_variables;
inline std::string value(const std::string& name) { return layout_variables[name]; }
class GUIObject {
    xml_node<>* node_;
public:
    explicit GUIObject(xml_node<>* node):node_(node) {}
    virtual ~GUIObject()=default;
    bool isConditionTrue() const { return node_->visible; }
};
class RenderObject {
protected:
    int mRenderX=0,mRenderY=0,mRenderW=0,mRenderH=0;
public:
    virtual ~RenderObject()=default;
    virtual int Render()=0;
    virtual int Update() { return 0; }
    virtual int SetRenderPos(int x,int y,int w=0,int h=0) { mRenderX=x; mRenderY=y; if(w || h) { mRenderW=w; mRenderH=h; } return 0; }
};
struct LayoutFill { int x,y,w,h; std::array<int,4> color; };
inline std::array<int,4> layout_color{};
inline std::vector<LayoutFill> layout_fills;
inline void gr_color(int r,int g,int b,int a) { layout_color={r,g,b,a}; }
inline void gr_fill(int x,int y,int w,int h) { layout_fills.push_back({x,y,w,h,layout_color}); }
void ure_create_partition_map(xml_node<>*,GUIObject*&,RenderObject*&);
