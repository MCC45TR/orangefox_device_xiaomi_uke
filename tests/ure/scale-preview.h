// SPDX-License-Identifier: Apache-2.0
// Host-only font ownership/graphics boundary, never a product implementation.
#pragma once
#include "layout-hooks.h"
#include "localization_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
struct COLOR { int red=255,green=128,blue=0,alpha=255; };
inline COLOR LoadAttrColor(xml_node<>*,const char*) { return {}; }
inline float preview_density=1;
inline int scale_theme_min(int units) { return int(float(units)*preview_density); }
struct PreviewFont { int height=1; };
class FontResource {
public:
    PreviewFont font;
    void* GetResource() { return &font; }
};
inline FontResource preview_source,preview_detail;
inline bool preview_missing_font=false;
inline FontResource* LoadAttrFont(xml_node<>*,const char* name) {
    if(preview_missing_font)return nullptr;
    return std::string(name)=="font" ? &preview_source : &preview_detail;
}
class DataManager {
public:
    static int SetValue(const std::string& name,const std::string& text) { layout_variables[name]=text; return 0; }
};
inline int preview_allocations=0,preview_live_fonts=0;
namespace twrpTruetype {
inline void* gr_ttf_scaleFont(void* source,int selected,int applied) {
    ++preview_allocations; ++preview_live_fonts;
    return new PreviewFont{std::max(1,static_cast<PreviewFont*>(source)->height*selected/applied-1)};
}
inline void gr_ttf_freeFont(void* font) { --preview_live_fonts; delete static_cast<PreviewFont*>(font); }
inline int gr_ttf_getMaxFontHeight(void* font) { return static_cast<PreviewFont*>(font)->height; }
}
struct PreviewText { int x,y,width,height; std::string text; };
inline std::vector<PreviewText> preview_texts;
inline int gr_textEx_scaleW(int x,int y,const char* text,void* font,int width,int,int) {
    preview_texts.push_back({x,y,width,static_cast<PreviewFont*>(font)->height,text}); return 0;
}
void ure_create_scale_preview(xml_node<>*,GUIObject*&,RenderObject*&);
