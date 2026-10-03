// SPDX-License-Identifier: Apache-2.0
#include "menu-rendering.h"
static void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
int main() {
    try {
        for(const int width:{1600,2136,2560,3200})for(const int percent:{50,75,100})for(const int group:{0,1,2,3,4}) {
            density=static_cast<double>(width)/1080*percent/100;
            GUIScrollList row; row.mRenderX=scale_theme_x(32); row.mRenderW=width-row.mRenderX*2;
            row.maxIconWidth=scale_theme_x(144); row.actualItemHeight=scale_theme_y(192);
            ImageResource icon,arrow; icon.surface.width=icon.surface.height=scale_theme_x(72);
            arrow.surface.width=arrow.surface.height=scale_theme_x(40);
            FontResource primary{scale_theme_y(42)},secondary{scale_theme_y(28)};
            row.mFont=&primary; row.mSecondaryFont=&secondary; row.groupArrow=group ? &arrow : nullptr;
            texts.clear(); images.clear(); row.RenderStdItem(200,true,&icon,"Long entry name","Smaller explanatory text",group);
            check(texts.size()==2,"Description was not rendered");
            check(texts[0].font==&primary && texts[1].font==&secondary && secondary.height<primary.height,"Description did not use a smaller font");
            const auto rendered=std::find_if(images.begin(),images.end(),[&](auto i){return i.surface==&icon.surface;});
            check(rendered!=images.end() && texts[0].x>=rendered->x+rendered->width+scale_theme_x(20),"Icon and label overlap after scaling");
            check(texts[0].width>=0 && texts[0].x+texts[0].width<=row.mRenderX+row.mRenderW,"Text clipping exceeds the row");
            if(group) {
                const auto trailing=std::find_if(images.begin(),images.end(),[&](auto i){return i.surface==&arrow.surface;});
                check(trailing!=images.end() && texts[0].x+texts[0].width<=trailing->x && trailing->x+trailing->width<=row.mRenderX+row.mRenderW,"Description or arrow crosses the trailing margin");
            }
            row.mSecondaryFont=nullptr; texts.clear(); row.RenderStdItem(200,false,&icon,"Name","Legacy description",group);
            check(texts[1].font==&primary,"Existing rows lost their font fallback");
        }
        std::cout<<"Actual row renderer: scaled icon gaps, trailing clipping, secondary fonts and legacy fallback passed in 60 layouts.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
