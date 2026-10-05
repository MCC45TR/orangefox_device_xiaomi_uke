// SPDX-License-Identifier: Apache-2.0
#include "text-renderer.h"
#include "ure-text-layout.hpp"
#include <libxml/parser.h>
#include <ft2build.h>
#include FT_MULTIPLE_MASTERS_H
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <sys/resource.h>

unsigned int gr_rotation = 0;
GGLContext* gr_context = nullptr;
GRSurface* gr_draw = nullptr;
static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"Multilingual renderer contract: %s\n",message); std::abort(); }
}
static ure_text::Layout inspect(const char* text, void* font) {
    ure_text::Layout result;
    require(twrpTruetype::gr_ttf_inspectLayout(text,font,result),"production layout inspection");
    require(result.valid && !result.truncated && result.bytes==std::strlen(text),"whole original UTF-8 byte count");
    return result;
}
static void check_glyphs(const ure_text::Layout& layout, const char* language, const char* key) {
    for (const auto& glyph : layout.glyphs) if (!glyph.index) {
        std::fprintf(stderr,"Missing glyph: language=%s key=%s byte=%u\n",language,key,glyph.cluster);
        std::abort();
    }
}
struct Sample { const char* locale; const char* text; };
static constexpr Sample samples[]{
    {"en","Android /dev/block/sda 123"}, {"tr_TR","İstanbul — yedekle ve geri yükle"},
    {"ar_SA","استعادة النظام — Linux 123"}, {"fa_IR","بازیابی سیستم — Linux 123"},
    {"he_IL","שחזור מערכת — Linux 123"}, {"hi_IN","प्रणाली पुनर्स्थापित करें"},
    {"bn_BD","সিস্টেম পুনরুদ্ধার করুন"}, {"th_TH","กู้คืนระบบและสำรองข้อมูล"},
    {"ja_JP","システムを復元する"}, {"ko_KR","시스템 복원 및 백업"},
    {"zh_CN","备份与恢复系统"}, {"zh_TW","備份與還原系統"},
    {"el_GR","Αντίγραφο ασφαλείας"}, {"ru","Резервное копирование"}
};
static std::size_t strings = 0, scalars = 0;
static std::size_t rotations = 0, painted = 0;
static void bind(void*,const GGLSurface* surface) {
    require(surface && surface->data && surface->width<=4096 && surface->height<=4096,"bounded rotated text surface");
    for(std::size_t i=0;i<std::size_t(surface->height)*surface->stride;++i) if(surface->data[i]) ++painted;
}
static void rect(void*,int left,int top,int right,int bottom) {
    require(left>=0 && top>=0 && right<=gr_draw->width && bottom<=gr_draw->height,"multiscript rotated draw clipped to framebuffer");
    ++rotations;
}
static void noop(void*,GGLenum) {}
static void parameter_noop(void*,GGLenum,GGLenum,GGLint) {}
static void coordinate_noop(void*,GGLint,GGLint) {}
static void variable_regular(void* font,const char* cluster) {
    const auto shape=inspect(cluster,font);
    auto* actual=static_cast<TrueTypeFont*>(font);
    FT_Face face=actual->faces[shape.glyphs.front().slot].face;
    FT_MM_Var* axes=nullptr;
    require(!FT_Get_MM_Var(face,&axes) && axes,"licensed Indic variable-font axes");
    std::vector<FT_Fixed> coordinates(axes->num_axis);
    require(!FT_Get_Var_Design_Coordinates(face,axes->num_axis,coordinates.data()),"default variable-font coordinates");
    bool regular=false;
    for(unsigned int i=0;i<axes->num_axis;++i) if(axes->axis[i].tag==0x77676874UL) {
        require(axes->axis[i].def==400*65536 && coordinates[i]==axes->axis[i].def,"default Regular weight 400, no Thin instance");
        regular=true;
    }
    require(regular,"weight axis present");
    FT_Done_MM_Var(face->glyph->library,axes);
}
static void rotated_samples(void* font) {
    GGLContext context{};
    context.bindTexture=bind; context.recti=rect;
    context.texEnvi=parameter_noop; context.texGeni=parameter_noop;
    context.enable=noop; context.disable=noop; context.texCoord2i=coordinate_noop;
    GRSurface framebuffer{3200,2136,12800,4,nullptr,0};
    gr_context=&context; gr_draw=&framebuffer;
    for(unsigned int rotation : {0U,90U,180U,270U}) {
        gr_rotation=rotation;
        for(const auto& sample : samples) {
            require(twrpTruetype::gr_ttf_setLocale(sample.locale),"rotated script locale");
            require(twrpTruetype::gr_ttf_textExWH(gr_context,12,20,sample.text,font,1800,-1,gr_draw)==int(std::strlen(sample.text)),
                "all original sample bytes draw in every framebuffer orientation");
        }
    }
    gr_context=nullptr; gr_draw=nullptr; gr_rotation=0;
}
static void xml_strings(xmlNode* node, void* font, const char* locale) {
    for (; node; node=node->next) {
        if (node->type==XML_ELEMENT_NODE &&
            (xmlStrEqual(node->name,BAD_CAST "string") || xmlStrEqual(node->name,BAD_CAST "display"))) {
            auto* text=xmlNodeGetContent(node);
            auto* key=xmlGetProp(node,BAD_CAST "name");
            // GUI/console text is laid out one line at a time. A resource's
            // newline is a line separator, not a missing printable glyph.
            const std::string resource(reinterpret_cast<const char*>(text));
            for(std::size_t begin=0;begin<=resource.size();) {
                const auto end=resource.find_first_of("\r\n",begin);
                const auto line=resource.substr(begin,end==std::string::npos ? end : end-begin);
                const auto shape=inspect(line.c_str(),font);
                check_glyphs(shape,locale,key ? reinterpret_cast<const char*>(key) : "language-display");
                if(end==std::string::npos) break;
                begin=end+1;
            }
            const char* p=reinterpret_cast<const char*>(text);
            const char* end=p+std::strlen(p);
            while(p<end) { unsigned int cp=0; p+=twrpTruetype::utf8_to_unicode(p,end,&cp); if(cp>=32 && cp!=127) ++scalars; }
            ++strings;
            xmlFree(key); xmlFree(text);
        } else xml_strings(node->children,font,locale);
    }
}
static void atlas(const std::filesystem::path& directory, void* font, int scale) {
    constexpr int width=1800;
    std::vector<unsigned char> pixels;
    int y=0;
    for (const auto& sample : samples) {
        require(twrpTruetype::gr_ttf_setLocale(sample.locale),"sample locale");
        auto* actual=static_cast<TrueTypeFont*>(font);
        GGLSurface surface{};
        const int bytes=twrpTruetype::gr_ttf_render_text(actual,&surface,sample.text,width-32);
        require(bytes==int(std::strlen(sample.text)),"entire sample fits atlas width");
        const int height=int(surface.height)+12;
        const auto old=pixels.size();
        pixels.resize(old+std::size_t(width)*height*3,250);
        for (unsigned int row=0; row<surface.height; ++row)
            for (unsigned int column=0; column<surface.width; ++column) {
                const auto value=surface.data[row*surface.stride+column];
                const auto at=old+(std::size_t(row)*width+column+16)*3;
                pixels[at]=pixels[at+1]=pixels[at+2]=static_cast<unsigned char>(250-(unsigned(value)*225/255));
            }
        std::free(surface.data);
        y+=height;
    }
    std::ofstream out(directory/("text-scale-"+std::to_string(scale)+".ppm"),std::ios::binary);
    out<<"P6\n"<<width<<' '<<y<<"\n255\n";
    out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
    require(bool(out),"native raster atlas written");
}
int main(int argc,char** argv) {
    require(argc==5,"font, fallback directory, recovery source and private artifacts arguments");
    const auto start=std::chrono::steady_clock::now();
    require(twrpTruetype::gr_ttf_setFallbackDirectory(argv[2]),"trusted fallback directory before load");
    void* font=twrpTruetype::gr_ttf_loadFont(argv[1],48,300);
    require(font,"licensed primary font");
    require(!twrpTruetype::gr_ttf_setFallbackDirectory(argv[2]),"live fallback directory mutation refused");
    require(!twrpTruetype::gr_ttf_setLocale("../../other"),"unknown locale refused");
    const auto source_before=twrpTruetype::gr_ttf_getBudgetStats().font_source_bytes;
    require(source_before>19*1024*1024 && source_before<23*1024*1024,"one shared CJK source plus bounded small fonts");
    require(twrpTruetype::gr_ttf_setLocale("en"),"English shaping control");
    const auto ligature=inspect("ffi",font);
    require(ligature.glyphs.size()<3 && ligature.clusters.size()==1,"Roboto OpenType ligature and cluster");
    require(twrpTruetype::gr_ttf_maxExW("ffi",font,int((ligature.width_26-1)/64))==0,"ligature never split by width fit");
    require(twrpTruetype::gr_ttf_setLocale("ar_SA"),"Arabic locale");
    const auto joined=inspect("لل",font);
    require(joined.glyphs.size()==2 && joined.glyphs[0].cluster>joined.glyphs[1].cluster,"Arabic visual glyph order");
    auto* actual=static_cast<TrueTypeFont*>(font);
    for(const auto& glyph : joined.glyphs)
        require(glyph.index!=FT_Get_Char_Index(actual->faces[glyph.slot].face,0x644),"Arabic contextual joining glyph");
    require(joined.glyphs[0].index!=joined.glyphs[1].index,"initial and final Arabic forms differ");
    require(twrpTruetype::gr_ttf_setLocale("he_IL"),"Hebrew locale");
    const auto bidi=inspect("ABC אבג 12",font);
    const std::array<unsigned int,10> expected{0,1,2,3,11,12,10,8,6,4};
    require(bidi.glyphs.size()==expected.size(),"mixed bidi control glyph count");
    for(std::size_t i=0;i<expected.size();++i) require(bidi.glyphs[i].cluster==expected[i],"independent mixed Hebrew/numeric visual order");
    for(const char* cluster : {"שָ","कि","ক্ষ","ก้"}) {
        const auto shaped=inspect(cluster,font);
        check_glyphs(shaped,"combining-control","grapheme");
        require(shaped.clusters.size()==1,"base and combining/conjunct marks share one cluster");
        const int width=int((shaped.width_26+63)/64);
        require(width>1 && twrpTruetype::gr_ttf_maxExW(cluster,font,width/2)==0,"combining/conjunct prefix never splits");
    }
    variable_regular(font,"कि"); variable_regular(font,"ক্ষ");
    for(const auto& pair : std::array<std::pair<const char*,const char*>,4>{{
            {"ja_JP","Noto Sans CJK JP"},{"ko_KR","Noto Sans CJK KR"},
            {"zh_CN","Noto Sans CJK SC"},{"zh_TW","Noto Sans CJK TC"}}}) {
        require(twrpTruetype::gr_ttf_setLocale(pair.first),"regional CJK locale");
        const auto shape=inspect("骨",font);
        require(shape.glyphs.size()==1,"CJK glyph selection");
        require(std::string(actual->faces[shape.glyphs[0].slot].face->family_name)==pair.second,"region-specific CJK face");
    }
    require(twrpTruetype::gr_ttf_getBudgetStats().font_source_bytes==source_before,"four CJK faces share original file bytes");
    const auto root=std::filesystem::path(argv[3]);
    std::set<std::string> languages;
    for(const char* directory : {"gui/theme/common/languages","gui/theme/extra-languages/languages"})
        for(const auto& file : std::filesystem::directory_iterator(root/directory)) {
            if(file.path().extension()!=".xml") continue;
            const auto locale=file.path().stem().string();
            require(languages.insert(locale).second,"unique language resources");
            require(twrpTruetype::gr_ttf_setLocale(locale.c_str()),"every shipped locale is supported");
            auto* document=xmlReadFile(file.path().c_str(),nullptr,XML_PARSE_NONET);
            require(document,"pinned language XML parse");
            xml_strings(xmlDocGetRootElement(document),font,locale.c_str());
            xmlFreeDoc(document);
        }
    require(languages.size()==32 && strings>10000 && scalars>100000,"actual all-language resource coverage");
    require(twrpTruetype::gr_ttf_getBudgetStats().cache_bytes==0,"coverage measures without allocating pixel/string cache");
    // Shared faces must restore every fallback size, not only primary Roboto,
    // when preview and normal fonts are interleaved in one recovery frame.
    std::array<std::int64_t,std::size(samples)> normal_widths{};
    for(std::size_t i=0;i<std::size(samples);++i) {
        require(twrpTruetype::gr_ttf_setLocale(samples[i].locale),"normal font locale");
        normal_widths[i]=inspect(samples[i].text,font).width_26;
    }
    void* preview=twrpTruetype::gr_ttf_loadFont(argv[1],24,300);
    require(preview,"independent smaller preview font");
    for(int repeat=0;repeat<3;++repeat) for(std::size_t i=0;i<std::size(samples);++i) {
        require(twrpTruetype::gr_ttf_setLocale(samples[i].locale),"interleaved font locale");
        require(inspect(samples[i].text,preview).width_26<normal_widths[i],"smaller multiscript preview advances");
        require(inspect(samples[i].text,font).width_26==normal_widths[i],"original advances survive fallback face-size switches");
    }
    twrpTruetype::gr_ttf_freeFont(preview);
    const auto artifacts=std::filesystem::path(argv[4]);
    std::filesystem::create_directories(artifacts);
    for(int scale=50;scale<=100;scale+=5) {
        void* scaled=twrpTruetype::gr_ttf_loadFont(argv[1],48*scale/100,300);
        require(scaled,"all custom-scale font sizes");
        atlas(artifacts,scaled,scale);
        rotated_samples(scaled);
        twrpTruetype::gr_ttf_freeFont(scaled);
    }
    // Malformed display bytes retain their original byte-count boundaries.
    const char malformed[]{char(0xf8),char(0xe2),char(0x82),0};
    require(inspect(malformed,font).bytes==3,"malformed display bytes preserved");
    const auto stats=twrpTruetype::gr_ttf_getBudgetStats();
    require(rotations==11*4*14 && painted,"all 616 multiscript scale/orientation draws reached the native adapter");
    require(stats.cache_bytes<=32*1024*1024 && stats.font_source_bytes<=64*1024*1024 &&
        stats.parser_bytes<=64*1024*1024 && stats.layout_bytes<=ure_text::memory_budget,"combined text memory budgets");
    twrpTruetype::gr_ttf_freeFont(font);
    const auto released=twrpTruetype::gr_ttf_getBudgetStats();
    require(!released.fonts && !released.faces && !released.font_source_bytes && !released.cache_bytes,"all fallback and primary owners released");
    struct rusage usage{}; getrusage(RUSAGE_SELF,&usage);
    const auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::printf("Production multilingual text passed: languages=%zu strings=%zu scalars=%zu scales=11 orientation_draws=%zu elapsed_ms=%lld peak_rss_kib=%ld layout_bytes=%zu parser_bytes=%zu\n",
        languages.size(),strings,scalars,rotations,static_cast<long long>(milliseconds),usage.ru_maxrss,stats.layout_bytes,stats.parser_bytes);
}
