#pragma once
#include <linux/types.h>
#include "minuitwrp/truetype.hpp"
#include <algorithm>
#include <climits>
#include <string>
#include <vector>
extern unsigned int gr_rotation;
extern GGLContext* gr_context;
extern GRSurface* gr_draw;
using GRFont = TrueTypeFont;
using std::string;
struct TextFont { void* value; void* GetResource() { return value; } };
class GUIScrollList {
public:
    TextFont* mFont = nullptr;
    int mRenderW = 0;
    bool AddLines(std::vector<std::string>*, std::vector<std::string>*, size_t*, std::vector<std::string>*, std::vector<std::string>*);
};
