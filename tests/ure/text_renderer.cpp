#include "text-renderer.h"
#include <array>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>
#include <sys/resource.h>
#include <fstream>
#include <filesystem>
#include <unistd.h>

unsigned int gr_rotation = 0;
GGLContext* gr_context = nullptr;
GRSurface* gr_draw = nullptr;
static std::atomic<int> fail_after{-1};
enum Allocation { ANY, MALLOC, CALLOC, CPP_NEW };
static Allocation allocation_kind = ANY;
static size_t injected_allocations = 0;
static bool synthetic_outline = false;
static size_t render_calls = 0;
static FT_Vector large_points[4]{{0,0},{1536*64,0},{1536*64,1536*64},{0,1536*64}};
static char large_tags[4]{1,1,1,1};
static short large_contour[1]{3};
extern "C" FT_Error __real_FT_Load_Glyph(FT_Face, FT_UInt, FT_Int32);
extern "C" FT_Error __wrap_FT_Load_Glyph(FT_Face face, FT_UInt glyph, FT_Int32 flags) {
    if (!synthetic_outline) return __real_FT_Load_Glyph(face,glyph,flags);
    face->glyph->outline = FT_Outline{1,4,large_points,large_tags,large_contour,0};
    face->glyph->metrics.width = 1536*64; face->glyph->metrics.height = 1536*64;
    return 0;
}
extern "C" FT_Error __real_FT_Render_Glyph(FT_GlyphSlot, FT_Render_Mode);
extern "C" FT_Error __wrap_FT_Render_Glyph(FT_GlyphSlot glyph, FT_Render_Mode mode) {
    ++render_calls;
    return __real_FT_Render_Glyph(glyph,mode);
}
static bool fail_alloc(Allocation kind) {
    if (allocation_kind != ANY && allocation_kind != kind) return false;
    int n = fail_after.load();
    if (n < 0) return false;
    if (n == 0) { fail_after = -1; ++injected_allocations; return true; }
    --fail_after; return false;
}
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __wrap_malloc(size_t n) { return fail_alloc(MALLOC) ? nullptr : __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n, size_t size) { return fail_alloc(CALLOC) ? nullptr : __real_calloc(n, size); }
extern "C" void* __real__Znwm(size_t);
extern "C" void* __real__Znam(size_t);
extern "C" void* __real__ZnwmRKSt9nothrow_t(size_t, const std::nothrow_t&);
extern "C" void* __real__ZnamRKSt9nothrow_t(size_t, const std::nothrow_t&);
extern "C" void* __wrap__Znwm(size_t n) {
    if (fail_alloc(CPP_NEW)) throw std::bad_alloc();
    return __real__Znwm(n);
}
extern "C" void* __wrap__Znam(size_t n) {
    if (fail_alloc(CPP_NEW)) throw std::bad_alloc();
    return __real__Znam(n);
}
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(size_t n, const std::nothrow_t& tag) {
    return fail_alloc(CPP_NEW) ? nullptr : __real__ZnwmRKSt9nothrow_t(n,tag);
}
extern "C" void* __wrap__ZnamRKSt9nothrow_t(size_t n, const std::nothrow_t& tag) {
    return fail_alloc(CPP_NEW) ? nullptr : __real__ZnamRKSt9nothrow_t(n,tag);
}
static void require(bool ok, const char* message) {
    if (!ok) { fprintf(stderr, "Text renderer contract: %s\n", message); std::abort(); }
}
static size_t draw_calls = 0;
static size_t nonzero_pixels = 0;
static int texture_width = 0, texture_height = 0, texture_x = 0, texture_y = 0;
static void bind(void*, const GGLSurface* surface) {
    texture_width = surface->width; texture_height = surface->height;
    require(surface->width <= 4096 && surface->height <= 4096, "surface edge budget");
    require(size_t(surface->width) * surface->height <= 2*1024*1024, "surface pixel budget");
    for (size_t i = 0; i < size_t(surface->width)*surface->height; ++i) if (surface->data[i]) ++nonzero_pixels;
}
static void rect(void*, int l, int t, int r, int b) {
    require(l >= 0 && t >= 0 && r <= gr_draw->width && b <= gr_draw->height, "draw clipping");
    ++draw_calls;
}
static void enum_noop(void*, GGLenum) {}
static void parameter_noop(void*, GGLenum, GGLenum, GGLint) {}
static void coords_noop(void*, GGLint x, GGLint y) { texture_x = x; texture_y = y; }
static int draw(const char* text, void* font, int width = 1500, int height = -1) {
    return twrpTruetype::gr_ttf_textExWH(gr_context, 12, 20, text, font, width, height, gr_draw);
}
static void glyph_clips() {
    std::array<unsigned char,12> source{1,2,3,4,5,6,7,8,9,10,11,12};
    FT_BitmapGlyphRec glyph{};
    glyph.bitmap.rows = 3; glyph.bitmap.width = 4; glyph.bitmap.pitch = 4;
    glyph.bitmap.pixel_mode = FT_PIXEL_MODE_GRAY; glyph.bitmap.buffer = source.data();
    auto storage = std::make_unique<unsigned char[]>(4);
    GGLSurface dest{}; dest.width = 2; dest.height = 2; dest.stride = 2; dest.data = storage.get();
    glyph.left = -1; glyph.top = 1;
    require(!twrpTruetype::gr_ttf_copy_glyph_to_surface(&dest, &glyph, 0, 0, 0), "negative bearing clip");
    require(storage[0] == 6 && storage[1] == 7 && storage[2] == 10 && storage[3] == 11, "positive pitch oracle");
    glyph.bitmap.pitch = -4;
    require(!twrpTruetype::gr_ttf_copy_glyph_to_surface(&dest, &glyph, 0, 0, 0), "negative pitch clip");
    require(storage[0] == 6 && storage[1] == 7 && storage[2] == 2 && storage[3] == 3, "negative pitch oracle");
    for (int coordinate : {INT_MIN, INT_MAX})
        require(!twrpTruetype::gr_ttf_copy_glyph_to_surface(&dest, &glyph, coordinate, coordinate, coordinate), "extreme clipped coordinates");
    glyph.bitmap.pitch = 2;
    require(twrpTruetype::gr_ttf_copy_glyph_to_surface(&dest, &glyph, 0, 0, 0) == -1, "undersized pitch refusal");
}
int main(int argc, char** argv) {
    require(argc == 2, "licensed source font argument");
    GGLContext context{};
    context.bindTexture = bind; context.recti = rect;
    context.texEnvi = parameter_noop; context.texGeni = parameter_noop;
    context.enable = enum_noop; context.disable = enum_noop; context.texCoord2i = coords_noop;
    GRSurface surface{3200,2136,12800,4,nullptr,0};
    gr_context = &context; gr_draw = &surface;
    glyph_clips();
    require(!twrpTruetype::gr_ttf_loadFont(nullptr, 48, 300), "null font path");
    require(!twrpTruetype::gr_ttf_loadFont(argv[1], INT_MAX, INT_MAX), "overflow size/dpi");
    void* font = twrpTruetype::gr_ttf_loadFont(argv[1], 48, 300);
    require(font, "normal font load");
    auto* actual_font = static_cast<TrueTypeFont*>(font);
    const FT_GlyphSlotRec original_slot = *actual_font->face->glyph;
    const size_t before_render = render_calls;
    synthetic_outline = true;
    require(!twrpTruetype::gr_ttf_glyph_cache_get(actual_font,INT_MAX), "pre-render product refusal");
    synthetic_outline = false;
    *actual_font->face->glyph = original_slot;
    require(render_calls == before_render, "large outline refused before raster allocation");
    const char* valid = "j Uke — İstanbul café Ελληνικά Русский";
    require(twrpTruetype::gr_ttf_measureEx(valid, font) > 0, "multilingual control");
    require(twrpTruetype::gr_ttf_getBudgetStats().cache_bytes == 0, "measurement does not raster/cache");
    require(twrpTruetype::gr_ttf_maxExW("W", font, 0) == 0, "zero-width byte fit");
    require(twrpTruetype::gr_ttf_maxExW("W", font, 1) == 0, "oversized first glyph byte fit");
    const int w = twrpTruetype::gr_ttf_measureEx("W", font);
    require(twrpTruetype::gr_ttf_maxExW("WQ", font, w) == 1, "exact fit bytes");
    void* enlarged = twrpTruetype::gr_ttf_scaleFont(font,100,50);
    require(enlarged && twrpTruetype::gr_ttf_measureEx(valid,enlarged) > twrpTruetype::gr_ttf_measureEx(valid,font), "scale preview enlargement");
    require(twrpTruetype::gr_ttf_getMaxFontHeight(enlarged) > twrpTruetype::gr_ttf_getMaxFontHeight(font), "scale preview height");
    twrpTruetype::gr_ttf_freeFont(enlarged);
    require(!twrpTruetype::gr_ttf_scaleFont(font,INT_MAX,1), "excessive scale refusal");
    for (unsigned int rotation : {0U,90U,180U,270U}) {
        gr_rotation = rotation;
        require(draw(valid, font) > 0, "all rotations");
        require(draw(valid, font, 1500, 20) == 0, "vertical early clip");
        require(draw("j\xcc\x81\xc3\x81\xc4\xb0\xd0\xb9", font) > 0, "bearings and combining clipping");
        require(draw("\xf8\xe2\x82", font) > 0, "malformed drawing control");
    }
    require(nonzero_pixels && draw_calls, "real FreeType pixels reached drawing adapter");
    require(gr_textEx_scaleW(100,100,"input-control",font,0,TOP_LEFT,0) > 0, "zero width input sentinel remains visible");
    const auto count_before = twrpTruetype::gr_ttf_getBudgetStats().fonts;
    for (int i = 0; i < 500; ++i) require(gr_textEx_scaleW(100,100,valid,font,250,CENTER,1) > 0, "scaled drawing");
    require(twrpTruetype::gr_ttf_getBudgetStats().fonts == count_before, "temporary font released");
    for (int i = 0; i < 1000; ++i) {
        const auto text = "cache-control-"+std::to_string(i);
        require(draw(text.c_str(),font) > 0, "cache churn");
    }
    auto stats = twrpTruetype::gr_ttf_getBudgetStats();
    require(stats.string_entries <= 128 && stats.glyph_entries <= 512 && stats.cache_bytes <= 32*1024*1024, "global cache accounting");
    std::string oversized(65537,'x');
    require(twrpTruetype::gr_ttf_measureEx(oversized.c_str(),font) == -1 && draw(oversized.c_str(),font) == -1, "oversized input refusal");
    std::string characters(8193,'x');
    require(twrpTruetype::gr_ttf_measureEx(characters.c_str(),font) == -1, "character budget");
    require(draw(characters.c_str(),font,100) >= 0, "long but clipped input");
    require(!twrpTruetype::gr_ttf_scaleFont(font,100,0), "zero divisor refusal");
    GUIScrollList list;
    TextFont resource{font}; list.mFont = &resource; list.mRenderW = 0;
    std::vector<std::string> input{"\xe4\xb8\xad\xc4\xb0W\xf8"}, wrapped;
    size_t last = 0;
    require(list.AddLines(&input,nullptr,&last,&wrapped,nullptr), "actual console wrap progress");
    std::string joined;
    for (const auto& line : wrapped) joined += line;
    require(joined == input[0] && wrapped.size() == 4, "scalar-boundary wrap preserves raw bytes");
    for (const char* delimiter : {".", "/", ",", "-", ":", "_", ";"}) {
        std::vector<std::string> original{std::string(delimiter)+"abc"}, rows;
        size_t seen = 0;
        require(list.AddLines(&original,nullptr,&seen,&rows,nullptr), "delimiter wrapping progress");
        std::string bytes;
        for (const auto& row : rows) bytes += row;
        require(rows.size() == 4 && bytes == original[0], "delimiter preserves content");
    }
    // The full rotated texture origin survives framebuffer/height clipping.
    gr_rotation = 180;
    const int full_height = twrpTruetype::gr_ttf_getMaxFontHeight(font);
    const int logical_y = gr_draw->height - full_height/2;
    require(twrpTruetype::gr_ttf_textExWH(gr_context,12,logical_y,"clip-control",font,1500,-1,gr_draw)>0, "edge-clipped rotation");
    require(texture_height == full_height && texture_y == logical_y+full_height+1-gr_draw->height, "full texture origin");
    gr_rotation = 90;
    require(twrpTruetype::gr_ttf_textExWH(gr_context,12,gr_draw->width-full_height/2,"clip-control",font,1500,-1,gr_draw)>0, "90-degree edge clip");
    require(texture_width == full_height && texture_x == gr_draw->width-full_height/2+full_height+1-gr_draw->width, "full rotated horizontal origin");
    gr_rotation = 270;
    // Compact compressed font declares 256 MiB of decompressed data. FreeType
    // may parse it, but its memory callbacks refuse before a large allocation.
    const auto scratch = std::filesystem::temp_directory_path()/("ure-text-"+std::to_string(getpid()));
    std::filesystem::create_directory(scratch);
    std::array<unsigned char,68> woff{};
    auto be16 = [&woff](size_t at, unsigned int value) { woff[at]=value>>8; woff[at+1]=value; };
    auto be32 = [&woff](size_t at, unsigned int value) { for(int i=0;i<4;++i) woff[at+i]=value>>(24-i*8); };
    be32(0,0x774f4646); be32(4,0x00010000); be32(8,68); be16(12,1);
    be32(16,28+256*1024*1024); be32(44,0x68656164); be32(48,64); be32(52,1); be32(56,256*1024*1024);
    const auto woff_file = scratch/"compressed.ttf";
    { std::ofstream file(woff_file,std::ios::binary); file.write(reinterpret_cast<const char*>(woff.data()),woff.size()); }
    const size_t before_refusal = twrpTruetype::gr_ttf_getBudgetStats().refused_parser_allocations;
    require(!twrpTruetype::gr_ttf_loadFont(woff_file.c_str(),48,300), "decompression allocation refused");
    require(twrpTruetype::gr_ttf_getBudgetStats().refused_parser_allocations > before_refusal, "parser memory budget reached");
    // Theme reload uses a changed inode at the same pathname while old fonts
    // remain live. Each font must retain the face generation it actually loaded.
    const auto mutable_file = scratch/"theme.ttf";
    std::filesystem::copy_file(argv[1],mutable_file);
    void* old_face = twrpTruetype::gr_ttf_loadFont(mutable_file.c_str(),48,300);
    const auto replacement_file = scratch/"replacement.ttf";
    std::filesystem::copy_file(argv[1],replacement_file);
    std::filesystem::rename(replacement_file,mutable_file);
    void* new_face = twrpTruetype::gr_ttf_loadFont(mutable_file.c_str(),49,300);
    require(old_face && new_face && static_cast<TrueTypeFont*>(old_face)->face != static_cast<TrueTypeFont*>(new_face)->face, "theme reload face identity");
    twrpTruetype::gr_ttf_freeFont(old_face); twrpTruetype::gr_ttf_freeFont(new_face);
    std::filesystem::remove_all(scratch);
    require(draw("rotation-allocation-control",font)>0, "warm rotation cache");
    allocation_kind = MALLOC; fail_after = 0;
    require(draw("rotation-allocation-control",font)==-1, "rotation malloc refusal");
    allocation_kind = CALLOC; fail_after = 0;
    require(draw("raster-allocation-control",font)==-1, "raster calloc refusal");
    allocation_kind = CPP_NEW; fail_after = 0;
    require(draw("string-allocation-control",font)==-1, "string allocation refusal");
    allocation_kind = ANY;
    // Sweep allocation failures through independent load, map, glyph, raster,
    // cache and rotated-surface paths; success or visible refusal is allowed.
    size_t failures = 0;
    for (int n = 0; n < 80; ++n) {
        const auto text = "allocation-"+std::to_string(n);
        fail_after = n;
        void* other = twrpTruetype::gr_ttf_loadFont(argv[1],49+n%10,300);
        if (other) {
            const int result = draw(text.c_str(),other);
            if (result < 0) ++failures;
            fail_after = -1;
            twrpTruetype::gr_ttf_freeFont(other);
        } else { ++failures; fail_after = -1; }
        require(twrpTruetype::gr_ttf_getBudgetStats().fonts == count_before, "failure releases font/lock");
    }
    require(failures > 0 && injected_allocations > 10, "allocation failures exercised");
    std::vector<std::thread> threads;
    for (int n = 0; n < 4; ++n) threads.emplace_back([&] {
        for (int i = 0; i < 100; ++i) {
            void* acquired = twrpTruetype::gr_ttf_loadFont(argv[1],48,300);
            require(acquired && twrpTruetype::gr_ttf_measureEx(valid,acquired)>0, "concurrent font lifecycle");
            twrpTruetype::gr_ttf_freeFont(acquired);
        }
    });
    for (auto& thread : threads) thread.join();
    twrpTruetype::gr_ttf_freeFont(font);
    twrpTruetype::gr_ttf_freeFont(font); // stale handle is never dereferenced
    stats = twrpTruetype::gr_ttf_getBudgetStats();
    require(!stats.fonts && !stats.faces && !stats.cache_bytes && !stats.font_source_bytes, "all ownership released");
    struct rusage usage{}; getrusage(RUSAGE_SELF,&usage);
    printf("Production text renderer passed; injected_allocations=%zu observed_refusals=%zu peak_rss_kib=%ld\n", injected_allocations, failures+3, usage.ru_maxrss);
}
