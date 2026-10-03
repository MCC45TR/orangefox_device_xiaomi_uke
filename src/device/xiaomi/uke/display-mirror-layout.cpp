// SPDX-License-Identifier: Apache-2.0
#include "display-mirror.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <cstring>
#include <limits>

namespace uke_display {
bool parse_selection(const std::string& resolution,const std::string& rate,int& width,int& height,int& millihertz) {
    int w=0,h=0,mhz=0;
    const auto number=[](const std::string& text,int& result) {
        if(text.empty() || text.size()>8)return false;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
        return parsed.ec==std::errc() && parsed.ptr==text.data()+text.size();
    };
    if(resolution!="auto") {
        const auto split=resolution.find('x');
        if(split==std::string::npos || !number(resolution.substr(0,split),w) || !number(resolution.substr(split+1),h) ||
            w<320 || w>2560 || h<200 || h>1440)return false;
    }
    if(rate!="auto" && (!number(rate,mhz) || mhz<24000 || mhz>75000))return false;
    width=w; height=h; millihertz=mhz; return true;
}
bool fit(int w, int h, int ow, int oh, Viewport& v, int percent) {
    if(w<=0 || h<=0 || w>16384 || h>16384 || ow<320 || oh<200 || ow>2560 || oh>1440 || percent<50 || percent>100)return false;
    if(int64_t(ow)*h<=int64_t(oh)*w) {
        v.width=ow; v.height=std::max(1,int(int64_t(ow)*h/w));
    } else {
        v.height=oh; v.width=std::max(1,int(int64_t(oh)*w/h));
    }
    v.width=std::max(1,v.width*percent/100); v.height=std::max(1,v.height*percent/100);
    v.x=(ow-v.width)/2; v.y=(oh-v.height)/2;
    return true;
}
bool render(const Frame& f,uint8_t* dst,std::size_t capacity,int ow,int oh,int stride,int percent) {
    if(!f.pixels || !dst || ow<320 || ow>2560 || oh<200 || oh>1440 || f.width<=0 || f.height<=0 || f.width>16384 || f.height>16384 ||
       (f.bytes_per_pixel!=2 && f.bytes_per_pixel!=4) || f.stride<f.width*f.bytes_per_pixel ||
       f.stride>1024*1024 || (f.rotation!=0 && f.rotation!=90 && f.rotation!=180 && f.rotation!=270) ||
       stride<ow*4 || stride>1024*1024 || oh<=0 || capacity<std::size_t(stride)*std::size_t(oh))return false;
    const int lw=(f.rotation==90 || f.rotation==270) ? f.height : f.width;
    const int lh=(f.rotation==90 || f.rotation==270) ? f.width : f.height;
    Viewport v{}; if(!fit(lw,lh,ow,oh,v,percent))return false;
    // Deterministic black bars and padding; no uninitialized scanout bytes.
    std::memset(dst,0,std::size_t(stride)*std::size_t(oh));
    std::array<int,2560> columns{};
    for(int x=0;x<v.width;++x)columns[std::size_t(x)]=int(int64_t(x)*lw/v.width);
    for(int y=0;y<v.height;++y) {
        const int ly=int(int64_t(y)*lh/v.height);
        auto* row=dst+std::size_t(v.y+y)*std::size_t(stride)+std::size_t(v.x)*4;
        for(int x=0;x<v.width;++x) {
            const int lx=columns[std::size_t(x)];
            int sx=lx,sy=ly;
            if(f.rotation==90) { sx=f.width-1-ly; sy=lx; }
            if(f.rotation==180) { sx=f.width-1-lx; sy=f.height-1-ly; }
            if(f.rotation==270) { sx=ly; sy=f.height-1-lx; }
            const auto* p=f.pixels+std::size_t(sy)*std::size_t(f.stride)+std::size_t(sx)*std::size_t(f.bytes_per_pixel);
            // Current Uke profile uses RGBA/RGBX bytes. RGB565 supports fallback
            // source buffers; other source formats are refused by the adapter.
            if(f.bytes_per_pixel==4) { row[x*4]=p[0]; row[x*4+1]=p[1]; row[x*4+2]=p[2]; }
            else {
                const unsigned pixel=unsigned(p[0])|(unsigned(p[1])<<8);
                row[x*4]=uint8_t(((pixel>>11)&31)*255/31);
                row[x*4+1]=uint8_t(((pixel>>5)&63)*255/63);
                row[x*4+2]=uint8_t((pixel&31)*255/31);
            }
            row[x*4+3]=255;
        }
    }
    return true;
}
int pointer_axis(int position,int delta,float speed,int extent) {
    if(extent<=0)return 0;
    if(!std::isfinite(speed) || speed<=0 || speed>16)speed=1;
    const double next=double(position)+double(delta)*double(speed);
    return int(std::clamp(next,0.0,double(extent-1)));
}
}
