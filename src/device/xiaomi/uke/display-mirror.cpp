// SPDX-License-Identifier: Apache-2.0
// A separate atomic KMS pipeline mirrors the recovery canvas. It never changes
// the panel's CRTC/connector/planes and never opens another DRM device.
#include "display-mirror.hpp"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <sys/mman.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <cstdio>

namespace {
template<class T,void (*Free)(T*)>using Ptr=std::unique_ptr<T,decltype(Free)>;
using Resources=Ptr<drmModeRes,drmModeFreeResources>;
using Connector=Ptr<drmModeConnector,drmModeFreeConnector>;
using Encoder=Ptr<drmModeEncoder,drmModeFreeEncoder>;
using Planes=Ptr<drmModePlaneRes,drmModeFreePlaneResources>;
using Plane=Ptr<drmModePlane,drmModeFreePlane>;
using Properties=Ptr<drmModeObjectProperties,drmModeFreeObjectProperties>;
using Property=Ptr<drmModePropertyRes,drmModeFreeProperty>;
using Request=Ptr<drmModeAtomicReq,drmModeAtomicFree>;
std::atomic<bool> enabled{true};
std::atomic<bool> requested_blank{false};
uint64_t pack(unsigned w,unsigned h,unsigned mhz) { return (uint64_t(w)<<48)|(uint64_t(h)<<32)|mhz; }
std::atomic<uint64_t> preference{0},applied{0};
std::atomic<uint64_t> preference_revision{1};
uint64_t processed=0;
uint64_t processed_revision=0;
std::mutex mode_mutex;
std::string supported_modes="Connect a DP/HDMI monitor to list its modes";
bool selection_failed=false;
std::atomic<const char*> status{"DRM mirror unavailable"};
int drm_fd=-1;
uint32_t panel_connector=0,panel_crtc=0;
std::array<uint32_t,8> panel_planes{};
std::size_t panel_plane_count=0;
bool blanked=false,active=false,uncertain=false;
bool pending_frame=true;
uint32_t connector_id=0,crtc_id=0,plane_id=0,mode_blob=0;
drmModeModeInfo mode{};
struct Buffer { uint32_t handle=0,fb=0,pitch=0; uint64_t bytes=0; uint8_t* data=nullptr; };
std::array<Buffer,2> buffers{};
unsigned front=0;
using Clock=std::chrono::steady_clock;
Clock::time_point next_probe{},next_frame{};
struct Field { uint32_t id=0; uint64_t value=0; };
struct Route { uint32_t object=0,kind=0,id=0; const char* name=nullptr; };
std::array<Route,13> routes{};
Field field(uint32_t object,uint32_t kind,const char* name) {
    Properties props(drmModeObjectGetProperties(drm_fd,object,kind),drmModeFreeObjectProperties);
    if(!props || props->count_props>512)return {};
    for(uint32_t i=0;i<props->count_props;++i) {
        Property prop(drmModeGetProperty(drm_fd,props->props[i]),drmModeFreeProperty);
        if(prop && !std::strcmp(prop->name,name))return {prop->prop_id,props->prop_values[i]};
    }
    return {};
}
bool add(drmModeAtomicReq* req,uint32_t object,uint32_t kind,const char* name,uint64_t value) {
    for(const auto& route:routes)if(route.object==object && route.kind==kind && route.name && !std::strcmp(route.name,name))
        return route.id && drmModeAtomicAddProperty(req,object,route.id,value)>=0;
    return false;
}
bool bind_routes() {
    unsigned count=0;
    for(const char* name:{"FB_ID","CRTC_ID","SRC_X","SRC_Y","SRC_W","SRC_H","CRTC_X","CRTC_Y","CRTC_W","CRTC_H"})
        routes[count++]={plane_id,DRM_MODE_OBJECT_PLANE,field(plane_id,DRM_MODE_OBJECT_PLANE,name).id,name};
    routes[count++]={connector_id,DRM_MODE_OBJECT_CONNECTOR,field(connector_id,DRM_MODE_OBJECT_CONNECTOR,"CRTC_ID").id,"CRTC_ID"};
    for(const char* name:{"MODE_ID","ACTIVE"})routes[count++]={crtc_id,DRM_MODE_OBJECT_CRTC,field(crtc_id,DRM_MODE_OBJECT_CRTC,name).id,name};
    return std::all_of(routes.begin(),routes.end(),[](const Route& r){return r.id!=0;});
}
void release(Buffer& b) {
    if(b.data)munmap(b.data,std::size_t(b.bytes));
    if(b.fb)drmModeRmFB(drm_fd,b.fb);
    if(b.handle) { drm_mode_destroy_dumb args{}; args.handle=b.handle; drmIoctl(drm_fd,DRM_IOCTL_MODE_DESTROY_DUMB,&args); }
    b={};
}
bool allocate(Buffer& b) {
    drm_mode_create_dumb args{};
    args.width=mode.hdisplay; args.height=mode.vdisplay; args.bpp=32;
    if(drmIoctl(drm_fd,DRM_IOCTL_MODE_CREATE_DUMB,&args))return false;
    b.handle=args.handle; b.pitch=args.pitch; b.bytes=args.size;
    if(args.pitch<uint32_t(mode.hdisplay)*4 || args.pitch>1024*1024 ||
       args.size<uint64_t(args.pitch)*mode.vdisplay || args.size>32*1024*1024)return false;
    const uint32_t handles[4]={b.handle,0,0,0},pitches[4]={b.pitch,0,0,0},offsets[4]={};
    if(drmModeAddFB2(drm_fd,mode.hdisplay,mode.vdisplay,DRM_FORMAT_XBGR8888,handles,pitches,offsets,&b.fb,0))return false;
    drm_mode_map_dumb map{}; map.handle=b.handle;
    if(drmIoctl(drm_fd,DRM_IOCTL_MODE_MAP_DUMB,&map))return false;
    void* address=mmap(nullptr,std::size_t(b.bytes),PROT_READ|PROT_WRITE,MAP_SHARED,drm_fd,off_t(map.offset));
    if(address==MAP_FAILED)return false;
    b.data=static_cast<uint8_t*>(address); std::memset(b.data,0,std::size_t(b.bytes)); return true;
}
bool request(drmModeAtomicReq* req,uint32_t fb,bool on,bool modeset) {
    if(on && !modeset)return add(req,plane_id,DRM_MODE_OBJECT_PLANE,"FB_ID",fb);
    bool ok=add(req,plane_id,DRM_MODE_OBJECT_PLANE,"FB_ID",on ? fb : 0) &&
        add(req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_ID",on ? crtc_id : 0);
    if(on) {
        ok=ok && add(req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_X",0) && add(req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_Y",0) &&
            add(req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_W",uint64_t(mode.hdisplay)<<16) &&
            add(req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_H",uint64_t(mode.vdisplay)<<16) &&
            add(req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_X",0) && add(req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_Y",0) &&
            add(req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_W",mode.hdisplay) && add(req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_H",mode.vdisplay);
    }
    if(modeset)ok=ok && add(req,connector_id,DRM_MODE_OBJECT_CONNECTOR,"CRTC_ID",on ? crtc_id : 0) &&
        add(req,crtc_id,DRM_MODE_OBJECT_CRTC,"MODE_ID",on ? mode_blob : 0) &&
        add(req,crtc_id,DRM_MODE_OBJECT_CRTC,"ACTIVE",on ? 1 : 0);
    return ok;
}
bool commit(uint32_t fb,bool on,bool modeset,bool test) {
    Request req(drmModeAtomicAlloc(),drmModeAtomicFree);
    if(!req || !request(req.get(),fb,on,modeset))return false;
    uint32_t flags=modeset ? DRM_MODE_ATOMIC_ALLOW_MODESET : 0;
    if(test)flags|=DRM_MODE_ATOMIC_TEST_ONLY;
    return drmModeAtomicCommit(drm_fd,req.get(),flags,nullptr)==0;
}
bool stop() {
    // A failed disable has unknown scanout ownership. Retain its buffers until
    // detach closes the owning backend, rather than unmapping active scanout.
    if(active && !commit(0,false,true,false)) { uncertain=true; status="Mirror disable failed; resources retained"; return false; }
    active=false; uncertain=false;
    for(auto& b:buffers)release(b);
    if(mode_blob)drmModeDestroyPropertyBlob(drm_fd,mode_blob);
    mode_blob=connector_id=crtc_id=plane_id=0; applied=0; return true;
}
uint64_t refresh(const drmModeModeInfo& m) {
    return m.htotal && m.vtotal ? uint64_t(m.clock)*1000000/(uint64_t(m.htotal)*m.vtotal) : 0;
}
bool supported(const drmModeModeInfo& m) {
    const auto rate=refresh(m);
    return m.hdisplay>=320 && m.hdisplay<=2560 && m.vdisplay>=200 && m.vdisplay<=1440 &&
        m.clock && m.htotal && m.vtotal && m.vscan<=1 &&
        !(m.flags&(DRM_MODE_FLAG_INTERLACE|DRM_MODE_FLAG_DBLSCAN|DRM_MODE_FLAG_DBLCLK|DRM_MODE_FLAG_CLKDIV2)) && rate>=24000 && rate<=76000;
}
std::string label(uint64_t packed) {
    if(!packed)return "No active monitor mode";
    char buffer[96]; const unsigned rate=unsigned(packed&0xffffffffU);
    std::snprintf(buffer,sizeof(buffer),"%ux%u @ %u.%03u Hz",unsigned(packed>>48),unsigned((packed>>32)&0xffffU),rate/1000,rate%1000);
    return buffer;
}
bool choose_mode(const drmModeConnector& c) {
    int best=-1; uint64_t score=0;
    if(c.count_modes<=0 || c.count_modes>512)return false;
    const uint64_t requested=preference.load();
    const unsigned w=unsigned(requested>>48),h=unsigned((requested>>32)&0xffffU),rate=unsigned(requested&0xffffffffU);
    std::string modes;
    for(int i=0;i<c.count_modes;++i) {
        const auto& m=c.modes[i];
        if(!supported(m))continue;
        const uint64_t millihz=refresh(m);
        modes+=label(pack(m.hdisplay,m.vdisplay,unsigned(millihz)))+"\n";
        if(w && (m.hdisplay!=w || m.vdisplay!=h))continue;
        // Allow 59.94 vs an integer 60 Hz selection, but never change timings.
        if(rate && (millihz+500<rate || millihz>uint64_t(rate)+500))continue;
        // Automatic selection starts within 1080p60 where available. Explicit
        // 1440p75 requests use the monitor's reported mode and KMS test.
        const bool conservative=m.hdisplay<=1920 && m.vdisplay<=1080 && millihz<=61000;
        const uint64_t s=(requested==0 && conservative ? uint64_t(1)<<40 : 0)+
            uint64_t(m.hdisplay)*m.vdisplay*100000+millihz*2+((m.type&DRM_MODE_TYPE_PREFERRED) ? 1 : 0);
        if(best<0 || s>score) { best=i; score=s; }
    }
    { std::lock_guard<std::mutex> guard(mode_mutex); supported_modes=modes.empty() ? "No supported progressive EDID modes (up to 2560x1440 / 75 Hz)" : modes; }
    if(best<0)return false;
    mode=c.modes[best]; return true;
}
bool candidate(const drmModeConnector& c,const drmModeRes& res) {
    if(!choose_mode(c)) { status="Requested mode is not reported by this monitor"; return false; }
    const auto bound=field(c.connector_id,DRM_MODE_OBJECT_CONNECTOR,"CRTC_ID");
    if(!bound.id || bound.value) { status="External connector already owned; leaving it unchanged"; return false; }
    if(c.count_encoders<0 || c.count_encoders>64)return false;
    uint32_t possible=0;
    for(int i=0;i<c.count_encoders;++i) {
        Encoder e(drmModeGetEncoder(drm_fd,c.encoders[i]),drmModeFreeEncoder);
        if(e)possible|=e->possible_crtcs;
    }
    Planes planes(drmModeGetPlaneResources(drm_fd),drmModeFreePlaneResources);
    if(!planes || planes->count_planes>256)return false;
    for(int index=0;index<res.count_crtcs && index<32;++index) {
        const uint32_t id=res.crtcs[index]; const uint32_t bit=uint32_t(1)<<unsigned(index);
        if(id==panel_crtc || !(possible&bit))continue;
        const auto available=field(id,DRM_MODE_OBJECT_CRTC,"ACTIVE");
        if(!available.id || available.value)continue;
        bool associated=false;
        for(int other=0;other<res.count_connectors;++other) {
            const auto other_id=res.connectors[other]; if(other_id==c.connector_id)continue;
            const auto association=field(other_id,DRM_MODE_OBJECT_CONNECTOR,"CRTC_ID");
            if(!association.id || association.value==id) { associated=true; break; }
        }
        if(associated)continue;
        for(uint32_t p=0;p<planes->count_planes;++p) {
            const uint32_t pid=planes->planes[p];
            if(std::find(panel_planes.begin(),panel_planes.begin()+std::ptrdiff_t(panel_plane_count),pid)!=panel_planes.begin()+std::ptrdiff_t(panel_plane_count))continue;
            Plane plane(drmModeGetPlane(drm_fd,pid),drmModeFreePlane);
            if(!plane || !(plane->possible_crtcs&bit) || plane->crtc_id || plane->fb_id || plane->count_formats>256)continue;
            if(std::find(plane->formats,plane->formats+plane->count_formats,DRM_FORMAT_XBGR8888)==plane->formats+plane->count_formats)continue;
            const auto type=field(pid,DRM_MODE_OBJECT_PLANE,"type");
            if(!type.id || type.value==DRM_PLANE_TYPE_CURSOR)continue;
            connector_id=c.connector_id; crtc_id=id; plane_id=pid;
            if(!bind_routes() || drmModeCreatePropertyBlob(drm_fd,&mode,sizeof(mode),&mode_blob) || !allocate(buffers[0]) || !allocate(buffers[1])) { stop(); continue; }
            // TEST_ONLY checks bandwidth/topology and the independent plane
            // route while the panel is still active. No panel property in req.
            if(!commit(buffers[0].fb,true,true,true)) { stop(); continue; }
            front=0; pending_frame=true; selection_failed=false; status="Monitor ready; waiting for a rendered frame"; return true;
        }
    }
    status="No independent external CRTC/plane passed the DRM test"; return false;
}
void reconfigure(const uke_display::Frame& frame) {
    Connector c(drmModeGetConnector(drm_fd,connector_id),drmModeFreeConnector);
    if(!c || c->connection!=DRM_MODE_CONNECTED)return;
    const auto previous_mode=mode;
    if(!choose_mode(*c)) { mode=previous_mode; selection_failed=true; status="Requested mode unavailable; keeping previous mode"; return; }
    if(!std::memcmp(&mode,&previous_mode,sizeof(mode)))return;
    const auto old_buffers=buffers; const uint32_t old_blob=mode_blob; const unsigned old_front=front;
    buffers={}; mode_blob=0;
    bool ready=drmModeCreatePropertyBlob(drm_fd,&mode,sizeof(mode),&mode_blob)==0 &&
        allocate(buffers[0]) && allocate(buffers[1]) &&
        uke_display::render(frame,buffers[0].data,std::size_t(buffers[0].bytes),mode.hdisplay,mode.vdisplay,int(buffers[0].pitch)) &&
        commit(buffers[0].fb,true,true,true) && commit(buffers[0].fb,true,true,false);
    if(!ready) {
        for(auto& b:buffers)release(b);
        if(mode_blob)drmModeDestroyPropertyBlob(drm_fd,mode_blob);
        buffers=old_buffers; mode_blob=old_blob; front=old_front; mode=previous_mode;
        selection_failed=true; status="Requested mode failed the DRM check; keeping previous mode";
        return;
    }
    // The new atomic state is applied. Old buffers are no longer scanned out.
    auto obsolete=old_buffers; for(auto& b:obsolete)release(b);
    drmModeDestroyPropertyBlob(drm_fd,old_blob);
    active=true; front=1; pending_frame=false; next_frame=Clock::now()+std::chrono::milliseconds(34);
    applied=pack(mode.hdisplay,mode.vdisplay,unsigned(refresh(mode)));
}
void probe() {
    if(uncertain || drm_fd<0)return;
    if(connector_id) {
        Connector c(drmModeGetConnector(drm_fd,connector_id),drmModeFreeConnector);
        bool present=false;
        if(c && c->connection==DRM_MODE_CONNECTED && c->count_modes>0 && c->count_modes<=512)
            for(int i=0;i<c->count_modes;++i)if(!std::memcmp(&mode,&c->modes[i],sizeof(mode))) { present=true; break; }
        if(present)return;
        if(!stop())return;
        status="Monitor disconnected; tablet display remains active";
    }
    Resources res(drmModeGetResources(drm_fd),drmModeFreeResources);
    if(!res || res->count_connectors<0 || res->count_connectors>64 || res->count_crtcs<0 || res->count_crtcs>32)return;
    status="Waiting for a DP/HDMI monitor";
    { std::lock_guard<std::mutex> guard(mode_mutex); supported_modes="Connect a DP/HDMI monitor to list its modes"; }
    for(int i=0;i<res->count_connectors;++i) {
        if(res->connectors[i]==panel_connector)continue;
        Connector c(drmModeGetConnector(drm_fd,res->connectors[i]),drmModeFreeConnector);
        if(!c || c->connection!=DRM_MODE_CONNECTED || (c->connector_type!=DRM_MODE_CONNECTOR_DisplayPort &&
            c->connector_type!=DRM_MODE_CONNECTOR_HDMIA && c->connector_type!=DRM_MODE_CONNECTOR_HDMIB))continue;
        if(candidate(*c,*res))return;
    }
}
}
void ure_mirror_attach(int fd,uint32_t connector,uint32_t crtc,const uint32_t* planes,std::size_t count) {
    ure_mirror_detach();
    if(fd<0 || !connector || !crtc || !planes || !count || count>panel_planes.size())return;
    drm_fd=fd; panel_connector=connector; panel_crtc=crtc; panel_plane_count=count;
    std::copy_n(planes,count,panel_planes.begin()); next_probe={}; next_frame={}; blanked=false;
    requested_blank=false;
    processed=preference.load(); processed_revision=preference_revision.load(); selection_failed=false;
    status="Waiting for a DP/HDMI monitor";
}
void ure_mirror_detach() {
    if(drm_fd>=0) {
        stop();
        // Backend is exiting and closes the same fd immediately afterwards.
        for(auto& b:buffers)release(b);
        if(mode_blob)drmModeDestroyPropertyBlob(drm_fd,mode_blob);
    }
    drm_fd=-1; mode_blob=connector_id=crtc_id=plane_id=0; active=uncertain=false; applied=0; status="DRM mirror unavailable";
}
void ure_mirror_enable(bool value) { enabled.store(value); }
bool ure_mirror_select(int width,int height,int rate) {
    if((width!=0 || height!=0) && (width<320 || width>2560 || height<200 || height>1440))return false;
    if(rate!=0 && (rate<24000 || rate>75000))return false;
    preference=pack(unsigned(width),unsigned(height),unsigned(rate));
    preference_revision.fetch_add(1); return true;
}
std::string ure_mirror_modes() { std::lock_guard<std::mutex> guard(mode_mutex); return supported_modes; }
std::string ure_mirror_mode() { return label(applied.load()); }
void ure_mirror_blank(bool value) { requested_blank.store(value); }
const char* ure_mirror_status() { return status.load(); }
bool ure_mirror_update(const uke_display::Frame& frame,bool changed) {
    const char* before=status.load(); const auto now=Clock::now();
    pending_frame=pending_frame || changed;
    if(drm_fd<0)return false;
    if(blanked!=requested_blank.load()) {
        blanked=requested_blank.load(); next_probe={};
    }
    if(!enabled.load() || blanked) {
        if(stop())status=blanked ? "Mirror blanked with recovery" : "Monitor mirroring disabled";
        return before!=status.load();
    }
    const auto desired=preference.load();
    const auto revision=preference_revision.load();
    if(desired!=processed || revision!=processed_revision) {
        processed=desired; processed_revision=revision; selection_failed=false;
        if(connector_id && active && !uncertain)reconfigure(frame);
        else next_probe={};
    }
    if(now>=next_probe) { next_probe=now+std::chrono::seconds(1); probe(); }
    if(!connector_id || uncertain || !pending_frame || now<next_frame)return before!=status.load();
    next_frame=now+std::chrono::milliseconds(34); // At most ~30 updates/sec.
    auto& b=buffers[front];
    if(!uke_display::render(frame,b.data,std::size_t(b.bytes),mode.hdisplay,mode.vdisplay,int(b.pitch))) {
        if(stop())status="Unsupported recovery framebuffer; mirror stopped";
        return true;
    }
    if(!commit(b.fb,true,!active,false)) {
        // Failed atomic commit did not apply that new state. An already active
        // mirror retains its current scanout until a successful disable.
        if(stop())status="External DRM commit failed; tablet display remains active";
        return true;
    }
    active=true; front=1-front;
    pending_frame=false;
    applied=pack(mode.hdisplay,mode.vdisplay,unsigned(refresh(mode)));
    if(!selection_failed)status="Mirroring recovery to DP/HDMI";
    return before!=status.load();
}
