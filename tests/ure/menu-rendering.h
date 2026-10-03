// SPDX-License-Identifier: Apache-2.0
// Host graphics stand-ins for the actual patched OrangeFox row renderer.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
inline double density=1;
inline int scale_theme_x(int n) { return static_cast<int>(std::lround(n*density)); }
inline int scale_theme_y(int n) { return scale_theme_x(n); }
inline int scale_theme_min(int n) { return scale_theme_x(n); }
struct COLOR { int red=0,green=0,blue=0,alpha=255; };
enum class RoundedCornerFlags { NONE=0,TOP_LEFT=1,TOP_RIGHT=2,BOTTOM_LEFT=4,BOTTOM_RIGHT=8,ALL=15 };
inline RoundedCornerFlags operator|(RoundedCornerFlags a,RoundedCornerFlags b) { return static_cast<RoundedCornerFlags>(static_cast<int>(a)|static_cast<int>(b)); }
using GGLubyte=unsigned char;
struct GGLSurface { int version=0,width=0,height=0,stride=0,format=0; GGLubyte* data=nullptr; };
using gr_surface=GGLSurface*;
struct ImageResource { GGLSurface surface; int GetWidth() const { return surface.width; } int GetHeight() const { return surface.height; } gr_surface GetResource() { return &surface; } };
struct FontResource { int height; void* GetResource() { return this; } };
struct TextDraw { int x,y,width; void* font; };
struct ImageDraw { gr_surface surface; int x,y,width; };
inline std::vector<TextDraw> texts;
inline std::vector<ImageDraw> images;
inline void gr_color(int,int,int,int) {}
inline void gr_fill(int,int,int,int) {}
inline void gr_blit(gr_surface s,int,int,int w,int,int x,int y) { images.push_back({s,x,y,w}); }
inline void gr_textEx_scaleW(int x,int y,const char*,void* font,int width,int,int) { texts.push_back({x,y,width,font}); }
inline void ConvertStrToColor(const std::string&,COLOR*) {}
inline uint32_t* createShape(int,int,int,int,COLOR,RoundedCornerFlags) { return static_cast<uint32_t*>(std::malloc(4)); }
inline int res_get_pixel_format() { return 0; }
inline void res_free_surface(gr_surface s) { std::free(s); }
inline constexpr int TEXT_ONLY_RIGHT=0;
class DataManager { public: static std::string GetStrValue(const std::string&) { return {}; } };
class GUIScrollList {
public:
    COLOR mFocusColor,mHighlightColor,mFontHighlightColor,mFontColor,groupColor;
    bool hasHighlightColor=true;
    int actualItemHeight=192,mRenderX=0,mRenderW=1080,mPadding=0,maxIconWidth=144,mItemPaddingTop=0,mItemPaddingBottom=0;
    ImageResource* groupArrow=nullptr;
    FontResource* mFont=nullptr;
    FontResource* mSecondaryFont=nullptr;
    bool HasFocus() const { return true; }
    void RenderStdItem(int,bool,ImageResource*,const char*,const char*,int);
};
