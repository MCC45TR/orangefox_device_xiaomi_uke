// SPDX-License-Identifier: Apache-2.0
// Host-only controls around the exact compiled minuitwrp parser.
#include <linux/input.h>
#include <poll.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#define ABS_MT_POSITION 0x2a
#define TW_NO_HAPTICS 1
#include "touch-vk-types.inc"
static bool rescan_input=false;
static int gr_fb_width(){return 2136;}
static int gr_fb_height(){return 3200;}
static int ABS(int value){return value<0?-value:value;}
#include "touch-vk-modify.inc"
[[noreturn]] static void fail(const char* message){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
static void require(bool value,const char* message){if(!value)fail(message);}
struct Contact {
    ev device{};
    unsigned down=0,up=0,keys=0;
    Contact(){device.touch=true;device.p.xi.maximum=device.mt_p.xi.maximum=21359;device.p.yi.maximum=device.mt_p.yi.maximum=31999;}
    void send(unsigned type,unsigned code,int value){
        input_event event{};event.type=static_cast<__u16>(type);event.code=static_cast<__u16>(code);event.value=value;
        if(vk_modify(&device,&event)!=0)return;
        if(event.type==EV_KEY)++keys;
        if(event.type==EV_ABS){if(event.code==0)++up;else ++down;}
    }
    void sync(){send(EV_SYN,SYN_REPORT,0);}
    void start(int id,int x=6000,int y=9000){send(EV_ABS,ABS_MT_TRACKING_ID,id);send(EV_ABS,ABS_MT_PRESSURE,1);send(EV_ABS,ABS_MT_TOUCH_MAJOR,8);send(EV_ABS,ABS_MT_POSITION_X,x);send(EV_ABS,ABS_MT_POSITION_Y,y);sync();}
};
static void fixture(const char* path){
    std::ifstream stream(path);require(bool(stream),"sanitized fixture unavailable");
    const std::map<std::string,unsigned> types={{"EV_ABS",EV_ABS},{"EV_KEY",EV_KEY},{"EV_SYN",EV_SYN}};
    const std::map<std::string,unsigned> codes={{"ABS_MT_TRACKING_ID",ABS_MT_TRACKING_ID},{"BTN_TOUCH",BTN_TOUCH},{"BTN_TOOL_FINGER",BTN_TOOL_FINGER},{"ABS_MT_PRESSURE",ABS_MT_PRESSURE},{"ABS_MT_POSITION_X",ABS_MT_POSITION_X},{"ABS_MT_POSITION_Y",ABS_MT_POSITION_Y},{"ABS_MT_TOUCH_MAJOR",ABS_MT_TOUCH_MAJOR},{"ABS_MT_SLOT",ABS_MT_SLOT},{"SYN_REPORT",SYN_REPORT}};
    Contact contact;std::string line;unsigned packets=0,reports=0;
    while(std::getline(stream,line)){
        if(line.empty()||line[0]=='#')continue;
        char type[32]{},code[48]{};int value=0;require(std::sscanf(line.c_str(),"%31s %47s %d",type,code,&value)==3,"invalid fixture packet");
        require(types.count(type)&&codes.count(code),"unknown fixture packet");++packets;if(types.at(type)==EV_SYN)++reports;
        contact.send(types.at(type),codes.at(code),value);
    }
    require(packets==111&&reports==30,"sanitized four-contact ordering changed");
    require(contact.up==4,"tracking-ID release lost after trailing zeros");
    require(contact.down==12&&contact.keys==16,"four-contact nonrelease reports changed");
    std::puts("Four-contact parser replay: 111 packets, 30 frames, 12 coordinates, four releases.");
}
static void permutations(){
    std::array<unsigned,3> order{ABS_MT_TRACKING_ID,ABS_MT_PRESSURE,ABS_MT_TOUCH_MAJOR};unsigned contacts=0;
    std::sort(order.begin(),order.end());
    do{
        Contact contact;
        for(int repeated=0;repeated<4;++repeated){
            const auto oldUp=contact.up,oldDown=contact.down;contact.start(++contacts,6000+repeated*11,9000+repeated*17);
            require(contact.down==oldDown+1,"contact did not start");
            // Stationary metadata updates do not release a held finger.
            for(int i=0;i<3;++i){contact.send(EV_ABS,ABS_MT_TOUCH_MAJOR,8+i);contact.sync();}
            require(contact.up==oldUp,"stationary major frame released touch");
            // An ordinary USB HID synchronization must not steal the touch.
            ev hid{};input_event packet{};packet.type=EV_SYN;packet.code=SYN_REPORT;
            require(vk_modify(&hid,&packet)!=0,"HID report interfered with touch");
            for(const auto code:order)contact.send(EV_ABS,code,code==ABS_MT_TRACKING_ID?-1:0);
            contact.sync();require(contact.up==oldUp+1,"release permutation lost contact");
        }
    }while(std::next_permutation(order.begin(),order.end()));
    require(contacts==24,"all release permutations were not exercised");
    std::puts("All six lift/pressure/major orders pass across 24 consecutive contacts.");
}
static void compatibility(const std::string& mode){
    Contact contact;
    if(mode=="single"){
        contact.send(EV_ABS,ABS_X,6000);contact.send(EV_ABS,ABS_Y,9000);contact.sync();contact.sync();
    }else{
        contact.send(EV_ABS,ABS_MT_POSITION_X,6000);contact.send(EV_ABS,ABS_MT_POSITION_Y,9000);
        if(mode=="type-a")contact.send(EV_SYN,SYN_MT_REPORT,0);
        contact.sync();
        if(mode=="pressure")contact.send(EV_ABS,ABS_MT_PRESSURE,0);
        if(mode=="major")contact.send(EV_ABS,ABS_MT_TOUCH_MAJOR,0);
        contact.sync();
    }
    require(contact.down==1&&contact.up==1,"legacy release protocol regressed");
    std::printf("Legacy %s contact and release preserved.\n",mode.c_str());
}
static void interference(){
    Contact first,second;
    first.send(EV_ABS,ABS_MT_POSITION_X,6000);first.send(EV_ABS,ABS_MT_POSITION_Y,9000);first.sync();
    second.send(EV_ABS,ABS_MT_PRESSURE,0);second.sync();
    // Explicit separate diagnosis, not acceptance of cross-device behavior.
    require(second.up==1,"shared-state diagnostic changed; review its expected failure");
    std::fputs("KNOWN_REMAINING: touchscreen state is shared across touch-capable devices\n",stderr);std::exit(10);
}
static void isolated(){
    Contact first,second;first.start(1);
    second.send(EV_ABS,ABS_MT_PRESSURE,0);second.sync();
    require(second.up==0&&second.down==0,"idle touch device emitted a phantom release");
    first.send(EV_ABS,ABS_MT_TRACKING_ID,-1);first.send(EV_ABS,ABS_MT_PRESSURE,0);first.send(EV_ABS,ABS_MT_TOUCH_MAJOR,0);first.sync();
    require(first.up==1,"another touch device consumed the touchscreen contact");
    std::puts("Per-device touch release isolation preserved.");
}
int main(int argc,char** argv){
    require(argc>=2,"scenario required");const std::string mode=argv[1];
    if(mode=="fixture"){require(argc==3,"fixture path required");fixture(argv[2]);}
    else if(mode=="permutations")permutations();
    else if(mode=="interference")interference();
    else if(mode=="isolated")isolated();
    else if(mode=="single"||mode=="type-a"||mode=="pressure"||mode=="major")compatibility(mode);
    else fail("unknown scenario");
}
