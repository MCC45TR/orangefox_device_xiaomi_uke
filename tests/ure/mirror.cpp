// SPDX-License-Identifier: Apache-2.0
// Host-only fake DRM IOCTL boundary. The actual native sink, mode policy,
// framebuffer conversion and input routing are compiled without a GPU/device.
#include "display-mirror.hpp"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <cassert>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <set>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

struct _drmModeAtomicReq { std::vector<std::tuple<uint32_t,uint32_t,uint64_t>> fields; };
namespace {
bool connected=true,occupied=false,reject_test=false,reject_real=false,reject_disable=false,no_plane=false;
unsigned tests=0,real=0,handles=0,blobs=0,maps=0,property_reads=0;
uint32_t handle_sequence=0,blob_sequence=0;
std::map<uint32_t,uint32_t> current;
std::map<uint32_t,drmModeModeInfo> blob_modes;
std::vector<drmModeModeInfo> edid;
drmModeModeInfo displayed{};
const char* names[]={"FB_ID","CRTC_ID","SRC_X","SRC_Y","SRC_W","SRC_H","CRTC_X","CRTC_Y","CRTC_W","CRTC_H","type","MODE_ID","ACTIVE"};
template<class T>T* alloc() { return static_cast<T*>(std::calloc(1,sizeof(T))); }
template<class T>T* array(std::initializer_list<T> values) {
    auto* data=static_cast<T*>(std::calloc(values.size(),sizeof(T))); std::copy(values.begin(),values.end(),data); return data;
}
drmModeModeInfo timing(uint16_t w,uint16_t h,uint32_t rate) {
    drmModeModeInfo m{}; m.hdisplay=w; m.vdisplay=h; m.htotal=uint16_t(w+160); m.vtotal=uint16_t(h+40);
    m.clock=uint32_t(uint64_t(m.htotal)*m.vtotal*rate/1000000); m.type=DRM_MODE_TYPE_PREFERRED; return m;
}
void fresh(int fd) {
    ure_mirror_detach(); connected=true; occupied=reject_test=reject_real=reject_disable=no_plane=false;
    tests=real=property_reads=0; current.clear(); edid={timing(1920,1080,60000),timing(2560,1440,75000),timing(1280,720,59940)};
    assert(handles==0 && blobs==0); handle_sequence=blob_sequence=0;
    assert(ftruncate(fd,512*1024*1024)==0);
    ure_mirror_enable(true); assert(ure_mirror_select(0,0,0));
    const uint32_t panel[]={30}; ure_mirror_attach(fd,10,20,panel,1);
}
void tick(const uke_display::Frame& f,int ms=36) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); ure_mirror_update(f); }
}
extern "C" {
drmModeRes* drmModeGetResources(int) {
    auto* r=alloc<drmModeRes>(); r->count_crtcs=2; r->crtcs=array<uint32_t>({20,21}); r->count_connectors=2; r->connectors=array<uint32_t>({10,11}); return r;
}
void drmModeFreeResources(drmModeRes* r) { std::free(r->crtcs); std::free(r->connectors); std::free(r); }
drmModeConnector* drmModeGetConnector(int,uint32_t id) {
    auto* c=alloc<drmModeConnector>(); c->connector_id=id;
    c->connector_type=id==10 ? DRM_MODE_CONNECTOR_DSI : DRM_MODE_CONNECTOR_DisplayPort;
    c->connection=id==10 || connected ? DRM_MODE_CONNECTED : DRM_MODE_DISCONNECTED;
    c->count_encoders=1; c->encoders=array<uint32_t>({40}); c->count_modes=int(edid.size());
    c->modes=static_cast<drmModeModeInfo*>(std::calloc(edid.size(),sizeof(drmModeModeInfo))); std::copy(edid.begin(),edid.end(),c->modes); return c;
}
void drmModeFreeConnector(drmModeConnector* c) { std::free(c->modes); std::free(c->encoders); std::free(c); }
drmModeEncoder* drmModeGetEncoder(int,uint32_t) { auto* e=alloc<drmModeEncoder>(); e->possible_crtcs=3; return e; }
void drmModeFreeEncoder(drmModeEncoder* e) { std::free(e); }
drmModePlaneRes* drmModeGetPlaneResources(int) { auto* p=alloc<drmModePlaneRes>(); p->count_planes=no_plane ? 1U : 2U; p->planes=array<uint32_t>({30,31}); return p; }
void drmModeFreePlaneResources(drmModePlaneRes* p) { std::free(p->planes); std::free(p); }
drmModePlane* drmModeGetPlane(int,uint32_t id) { auto* p=alloc<drmModePlane>(); p->plane_id=id; p->possible_crtcs=3; p->count_formats=1; p->formats=array<uint32_t>({DRM_FORMAT_XBGR8888}); p->crtc_id=current[id*100+1]; p->fb_id=current[id*100]; return p; }
void drmModeFreePlane(drmModePlane* p) { std::free(p->formats); std::free(p); }
drmModeObjectProperties* drmModeObjectGetProperties(int,uint32_t object,uint32_t type) {
    ++property_reads; auto* p=alloc<drmModeObjectProperties>();
    std::vector<unsigned> indexes;
    if(type==DRM_MODE_OBJECT_CONNECTOR)indexes={1};
    if(type==DRM_MODE_OBJECT_CRTC)indexes={11,12};
    if(type==DRM_MODE_OBJECT_PLANE)indexes={0,1,2,3,4,5,6,7,8,9,10};
    p->count_props=uint32_t(indexes.size()); p->props=static_cast<uint32_t*>(std::calloc(indexes.size(),sizeof(uint32_t)));
    p->prop_values=static_cast<uint64_t*>(std::calloc(indexes.size(),sizeof(uint64_t)));
    for(std::size_t i=0;i<indexes.size();++i) {
        const auto index=indexes[i]; p->props[i]=object*100+index;
        p->prop_values[i]=current[p->props[i]];
        if(index==10)p->prop_values[i]=DRM_PLANE_TYPE_PRIMARY;
        if(object==21 && index==12 && occupied)p->prop_values[i]=1;
    }
    return p;
}
void drmModeFreeObjectProperties(drmModeObjectProperties* p) { std::free(p->props); std::free(p->prop_values); std::free(p); }
drmModePropertyRes* drmModeGetProperty(int,uint32_t id) { auto* p=alloc<drmModePropertyRes>(); p->prop_id=id; std::strncpy(p->name,names[id%100],sizeof(p->name)-1); return p; }
void drmModeFreeProperty(drmModePropertyRes* p) { std::free(p); }
drmModeAtomicReq* drmModeAtomicAlloc() { return new drmModeAtomicReq; }
void drmModeAtomicFree(drmModeAtomicReq* r) { delete r; }
int drmModeAtomicAddProperty(drmModeAtomicReq* r,uint32_t object,uint32_t prop,uint64_t value) {
    assert(object==11 || object==21 || object==31); // panel connector/CRTC/planes never touched
    r->fields.emplace_back(object,prop,value); return int(r->fields.size());
}
int drmModeAtomicCommit(int,drmModeAtomicReq* r,uint32_t flags,void*) {
    if(flags&DRM_MODE_ATOMIC_TEST_ONLY) { ++tests; return reject_test ? -1 : 0; }
    ++real; bool disabling=false;
    for(const auto& [object,prop,value]:r->fields)if(object==21 && prop%100==12 && value==0)disabling=true;
    if(reject_real || (disabling && reject_disable))return -1;
    for(const auto& [object,prop,value]:r->fields) {
        current[prop]=uint32_t(value);
        if(object==21 && prop%100==11 && value)displayed=blob_modes[uint32_t(value)];
    }
    return 0;
}
int drmModeCreatePropertyBlob(int,const void* data,size_t size,uint32_t* id) {
    assert(size==sizeof(drmModeModeInfo)); *id=++blob_sequence; blob_modes[*id]=*static_cast<const drmModeModeInfo*>(data); ++blobs; return 0;
}
int drmModeDestroyPropertyBlob(int,uint32_t id) { assert(blob_modes.erase(id)); --blobs; return 0; }
int drmModeAddFB2(int,uint32_t,uint32_t,uint32_t format,const uint32_t handles_in[4],const uint32_t[4],const uint32_t[4],uint32_t* id,uint32_t) {
    assert(format==DRM_FORMAT_XBGR8888); *id=handles_in[0]+100; return 0;
}
int drmModeRmFB(int,uint32_t) { return 0; }
int drmIoctl(int,unsigned long request,void* data) {
    if(request==DRM_IOCTL_MODE_CREATE_DUMB) {
        auto* d=static_cast<drm_mode_create_dumb*>(data); d->handle=++handle_sequence; ++handles;
        d->pitch=d->width*4; d->size=uint64_t(d->pitch)*d->height; return 0;
    }
    if(request==DRM_IOCTL_MODE_MAP_DUMB) { auto* d=static_cast<drm_mode_map_dumb*>(data); d->offset=uint64_t(d->handle-1)*16*1024*1024; ++maps; return 0; }
    if(request==DRM_IOCTL_MODE_DESTROY_DUMB) { assert(handles); --handles; return 0; }
    assert(false); return -1;
}
}
int main() {
    int w=-1,h=-1,mhz=-1;
    assert(uke_display::parse_selection("2560x1440","75000",w,h,mhz) && w==2560 && h==1440 && mhz==75000);
    assert(!uke_display::parse_selection("2560x1440;bad","75000",w,h,mhz));
    assert(!uke_display::parse_selection("3840x2160","75000",w,h,mhz));
    assert(!uke_display::parse_selection("auto","76000",w,h,mhz));
    assert(uke_display::pointer_axis(10,INT_MAX,2.5F,2560)==2559);
    assert(uke_display::pointer_axis(10,INT_MIN,2.5F,2560)==0);
    assert(uke_display::pointer_axis(10,1,NAN,2560)==11);
    std::vector<uint8_t> pixels={1,2,3,255,4,5,6,255,7,8,9,255,10,11,12,255,13,14,15,255,16,17,18,255};
    uke_display::Frame frame{pixels.data(),3,2,12,4,0};
    std::vector<uint8_t> output(2560*1440*4,128);
    for(unsigned rotation:{0U,90U,180U,270U}) {
        frame.rotation=rotation;
        assert(uke_display::render(frame,output.data(),output.size(),2560,1440,2560*4));
        uke_display::Viewport viewport{}; assert(uke_display::fit(rotation%180 ? 2 : 3,rotation%180 ? 3 : 2,2560,1440,viewport));
        const auto start=std::size_t(viewport.y*2560+viewport.x)*4;
        const uint8_t expected=rotation==0 ? 1 : rotation==90 ? 7 : rotation==180 ? 16 : 10;
        assert(output[start]==expected && output[start+3]==255);
        if(viewport.x)assert(output[0]==0 && output[3]==0);
    }
    frame.rotation=0;
    assert(!uke_display::render(frame,output.data(),12,2560,1440,2560*4));
    char path[]="/tmp/ure-drm-test-XXXXXX"; int fd=mkstemp(path); assert(fd>=0); unlink(path);
    fresh(fd); ure_mirror_update(frame); assert(tests==1 && real==1 && displayed.hdisplay==1920);
    const auto reads=property_reads; tick(frame); assert(property_reads==reads); // framebuffer swaps use cached property IDs
    const auto idle=real; std::this_thread::sleep_for(std::chrono::milliseconds(36)); ure_mirror_update(frame,false);
    assert(real==idle); // idle connector polling does not copy or commit an unchanged canvas
    assert(ure_mirror_modes().find("2560x1440")!=std::string::npos);
    assert(ure_mirror_select(2560,1440,75000)); tick(frame); assert(displayed.hdisplay==2560 && displayed.vdisplay==1440);
    assert(ure_mirror_mode().find("75.000")!=std::string::npos);
    assert(ure_mirror_select(1600,900,75000)); tick(frame); assert(displayed.hdisplay==2560);
    assert(std::string(ure_mirror_status()).find("keeping previous")!=std::string::npos);
    reject_test=true; assert(ure_mirror_select(1280,720,60000)); tick(frame); assert(displayed.hdisplay==2560);
    assert(handles==2 && blobs==1); reject_test=false;
    // Applying the same selection again must retry after a transient failure.
    assert(ure_mirror_select(1280,720,60000)); tick(frame); assert(displayed.hdisplay==1280);
    assert(ure_mirror_select(2560,1440,75000)); tick(frame); assert(displayed.hdisplay==2560);
    reject_real=true; assert(ure_mirror_select(1920,1080,60000)); tick(frame); reject_real=false;
    assert(displayed.hdisplay==2560 && handles==2 && blobs==1);
    ure_mirror_enable(false); ure_mirror_update(frame); assert(handles==0 && blobs==0 && current[2112]==0);
    ure_mirror_enable(true); tick(frame,1050); assert(handles==2);
    connected=false; tick(frame,1050); assert(handles==0 && blobs==0);
    connected=true; tick(frame,1050); assert(handles==2);
    ure_mirror_blank(true); assert(handles==2); // action thread never destroys resources
    ure_mirror_update(frame); assert(handles==0);
    ure_mirror_blank(false); tick(frame); assert(handles==2);
    reject_disable=true; ure_mirror_enable(false); ure_mirror_update(frame); assert(handles==2 && std::string(ure_mirror_status()).find("retained")!=std::string::npos);
    reject_disable=false; ure_mirror_update(frame); assert(handles==0);
    fresh(fd); occupied=true; ure_mirror_update(frame); assert(tests==0 && real==0 && handles==0);
    fresh(fd); no_plane=true; ure_mirror_update(frame); assert(tests==0 && real==0 && handles==0);
    fresh(fd); reject_test=true; ure_mirror_update(frame); assert(tests==1 && real==0 && handles==0 && blobs==0);
    fresh(fd); edid={timing(3840,2160,60000)}; ure_mirror_update(frame); assert(tests==0 && real==0 && handles==0);
    ure_mirror_detach(); close(fd); assert(handles==0 && blobs==0);
}
