// Host-only independent ioctl/mapping oracle for the actual renderer functions.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct GRSurface { int width,height,row_bytes,pixel_bytes; unsigned char* data; __u32 format; };
struct drm_surface { GRSurface base; uint32_t fb_id,handle; };
static int drm_fd=-1;
static int failure=0,closed=0,removed=0,mapped=0;
#define RECOVERY_RGBX 1
#define GGL_PIXEL_FORMAT_RGBA_8888 1
int drmIoctl(int,unsigned long request,void* argument) {
    if(request==DRM_IOCTL_MODE_CREATE_DUMB) {
        if(failure==2)return -1;
        auto* create=static_cast<drm_mode_create_dumb*>(argument);
        create->handle=17;create->pitch=create->width*4;create->size=create->pitch*create->height;return 0;
    }
    if(request==DRM_IOCTL_MODE_MAP_DUMB) { static_cast<drm_mode_map_dumb*>(argument)->offset=0;return 0; }
    if(request==DRM_IOCTL_GEM_CLOSE) { if(static_cast<drm_gem_close*>(argument)->handle!=17)std::abort();++closed;return 0; }
    std::abort();
}
int drmModeAddFB2(int,uint32_t width,uint32_t height,uint32_t format,const uint32_t handles[4],
    const uint32_t pitches[4],const uint32_t offsets[4],uint32_t* result,uint32_t flags) {
    if(width!=16 || height!=8 || format!=DRM_FORMAT_XBGR8888 || flags!=0 || handles[0]!=17 || pitches[0]!=64 || offsets[0]!=0)std::abort();
    // Every unused plane must be zero. Poisoned automatic storage makes the
    // old uninitialized arrays fail deterministically rather than by chance.
    for(int plane=1;plane<4;++plane)if(handles[plane] || pitches[plane] || offsets[plane])return -2;
    if(failure==1)return -2;
    *result=23;return 0;
}
int drmModeRmFB(int,uint32_t framebuffer) { if(framebuffer!=23)std::abort();++removed;return 0; }
static void* fixture_mmap(void*,std::size_t bytes,int,int,int,off_t) {
    ++mapped;return ::mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
}
#define mmap fixture_mmap
#include "drm-surface.inc"
#undef mmap
struct minui_backend {};
static bool current_blank_state = true;
static int current_buffer = 0, commits = 0;
static GRSurface* draw_buf;
static drm_surface* drm_surfaces[2];
static void update_plane_fb() {
    if (current_blank_state) std::abort();
    ++commits;
}
#define __unused __attribute__((unused))
#include "drm-flip.inc"
#undef __unused
int main() {
    auto* surface=drm_create_surface(16,8);
    if(!surface || surface->base.row_bytes!=64 || surface->base.pixel_bytes!=4)return 1;
    drm_destroy_surface(surface);
    if(closed!=1 || removed!=1 || mapped!=1)return 2;
    failure=1;
    if(drm_create_surface(16,8) || closed!=2 || removed!=1 || mapped!=1)return 3;
    failure=2;
    if(drm_create_surface(16,8) || closed!=2 || removed!=1 || mapped!=1)return 4;
    unsigned char drawing[16] = {1}, buffers[2][16] = {};
    GRSurface draw{2,2,8,4,drawing,0};
    drm_surface back[2] = {{GRSurface{2,2,8,4,buffers[0],0},0,0},
                           {GRSurface{2,2,8,4,buffers[1],0},0,0}};
    draw_buf = &draw;
    drm_surfaces[0] = &back[0]; drm_surfaces[1] = &back[1];
    for (int i=0; i<3; ++i) {
        if (drm_flip(nullptr)!=&draw || commits!=0 || current_buffer!=0 || buffers[0][0]!=0) return 5;
        ++drawing[0];
    }
    current_blank_state = false;
    if (drm_flip(nullptr)!=&draw || commits!=1 || current_buffer!=1 ||
        std::memcmp(drawing,buffers[0],sizeof(drawing))!=0) return 6;
    ++drawing[0];
    if (drm_flip(nullptr)!=&draw || commits!=2 || current_buffer!=0 ||
        std::memcmp(drawing,buffers[1],sizeof(drawing))!=0) return 7;
    std::puts("Actual DRM surface allocation: zero unused planes, positive mapping and failed allocation cleanup passed.");
    std::puts("Actual DRM flip: blanked planes receive no update; the latest draw survives blanking and resumes double buffering.");
}
