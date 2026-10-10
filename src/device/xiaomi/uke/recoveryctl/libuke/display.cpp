// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <array>
#include <chrono>
#include <dirent.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#ifdef __ANDROID__
#include "../../ure-telemetry.hpp"
#include "../../clock-sync/clock-mount-policy.hpp"
#include <linux/fs.h>
#include <linux/magic.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/system_properties.h>
#endif
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fcntl.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <csignal>
#include <cstring>

namespace ure {
int display_scale_parse(const std::string& text) {
    int percent=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),percent);
    require(!text.empty() && text.size()<=3 && text.front()>='0' && text.front()<='9' &&
        parsed.ec==std::errc() && parsed.ptr==text.data()+text.size() && percent>=50 && percent<=100,
        "invalid-scale","Interface scale must be a whole percentage from 50 to 100");
    return percent;
}
DisplayLayout display_layout(int width,int height,double base_width,double base_height,int percent) {
    require(width>=320 && height>=320 && width<=16384 && height<=16384 &&
        std::isfinite(base_width) && std::isfinite(base_height) && base_width>0 && base_height>0 &&
        base_width<=32 && base_height<=32,"invalid-display","Invalid framebuffer or theme scale");
    display_scale_parse(std::to_string(percent));
    // Use the theme's existing minimum density, which controls font size.
    // One uniform factor avoids stretching icons and changing hit boxes alone.
    const double factor=std::min(base_width,base_height)*static_cast<double>(percent)/100.0;
    require(factor>=0.125 && width/factor<=32768 && height/factor<=32768,
        "invalid-display","Interface canvas exceeds the reviewed limits");
    return {static_cast<float>(factor),static_cast<int>(std::ceil(width/factor)),
        static_cast<int>(std::ceil(height/factor)),percent};
}
namespace {
bool same(const struct stat& a,const struct stat& b) { return a.st_dev==b.st_dev && a.st_ino==b.st_ino; }
void settings_gate(int descriptor) {
    // Compare actual ancestors, so symlink/bind aliases do not turn calibration
    // or untrusted Android storage into an apparently unrelated settings root.
    std::vector<std::pair<struct stat,bool>> protected_roots;
    for(const auto* path:{"/persist","/mnt/vendor/persist","/data","/metadata"}) {
        struct stat st{},parent{};
        if(::stat(path,&st)==0 && S_ISDIR(st.st_mode)) {
            const bool separate_filesystem=::stat((fs::path(path)/"..").c_str(),&parent)==0 && st.st_dev!=parent.st_dev;
            protected_roots.emplace_back(st,separate_filesystem);
        }
    }
    Fd current(::fcntl(descriptor,F_DUPFD_CLOEXEC,0));
    require(current.get()>=0,"io-error","Cannot retain settings root");
    for(unsigned depth=0;depth<256;++depth) {
        struct stat st{}; require(::fstat(current.get(),&st)==0,"io-error","Cannot inspect settings ancestry");
        for(const auto& [forbidden,filesystem]:protected_roots)require(!same(st,forbidden) && !(filesystem && st.st_dev==forbidden.st_dev),"protected-settings-root",
            "Choose a dedicated mounted Linux or external settings directory; Android and calibration storage are refused");
        Fd parent(::openat(current.get(),"..",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)); struct stat parent_st{};
        require(parent.get()>=0 && ::fstat(parent.get(),&parent_st)==0,"io-error","Cannot inspect settings parent");
        if(same(st,parent_st))return;
        current=std::move(parent);
    }
    throw Error("invalid-root","Settings ancestry exceeds the reviewed depth");
}
Value record(int percent) {
    Value value; value["schema"]=1; value["format"]="ure-display-settings";
    value["scale_percent"]=percent; value["uniform_density"]=true; return value;
}
Value result(const Root& root,int percent) {
    struct statfs filesystem{}; require(::fstatfs(root.fd(),&filesystem)==0,"io-error","Cannot inspect settings filesystem");
    Value value=record(percent); value["private_record"]=true; value["mounts_performed"]=false;
    value["volatile_filesystem"]=filesystem.f_type==0x01021994 || filesystem.f_type==0x858458f6;
    value["physical_test_record"]=false; return value;
}
}
Value display_settings_load(const fs::path& directory) {
    auto root=private_directory(directory,false); settings_gate(root.fd());
    auto file=root.open("display.json",O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() &&
        (st.st_mode&07777)==0600,"unsafe-settings","Display settings must be a private single-link regular file");
    require(st.st_size>=0 && st.st_size<=4096,"size-limit","Display settings exceed the record limit");
    const auto value=parse_json(storage_read(file.get(),0,static_cast<std::size_t>(st.st_size)));
    require(value.isObject() && value.size()==4 && value["schema"]==1 && value["format"]=="ure-display-settings" &&
        value["scale_percent"].isInt() && value["uniform_density"]==true,"invalid-settings","Unknown display settings schema");
    const int percent=display_scale_parse(std::to_string(value["scale_percent"].asInt()));
    return result(root,percent);
}
Value display_settings_save(const fs::path& directory,int percent) {
    display_scale_parse(std::to_string(percent));
    Root parent(directory.parent_path().empty() ? fs::path(".") : directory.parent_path()); settings_gate(parent.fd());
    struct stat existing{}; const auto name=directory.filename().string();
    const bool exists=::fstatat(parent.fd(),name.c_str(),&existing,AT_SYMLINK_NOFOLLOW)==0;
    auto root=private_directory(directory,!exists); settings_gate(root.fd());
    if(root.exists("display.json")) {
        const auto previous=root.stat("display.json");
        require(S_ISREG(previous.st_mode) && previous.st_nlink==1 && previous.st_uid==::geteuid() &&
            (previous.st_mode&07777)==0600,"unsafe-settings","Existing display settings are not private");
    }
    root.save_record("display.json",record(percent),true);
    const auto loaded=display_settings_load(directory);
    require(loaded["scale_percent"]==percent,"readback-error","Saved interface scale does not match readback");
    return loaded;
}
bool ui_preference_key(std::string_view key) {
    static constexpr std::array<std::string_view,37> keys={
        "tw_language","tw_time_zone","tw_time_zone_guisel","tw_time_zone_guioffset","tw_time_zone_guidst",
        "tw_military_time","tw_brightness","tw_brightness_pct","tw_screen_timeout_secs","tw_no_screen_timeout",
        "tw_button_vibrate","tw_keyboard_vibrate","tw_action_vibrate","tw_gui_sort_order",
        "ure_ui_scale_percent","ure_theme_active","ure_theme_style","ure_theme_accent","ure_theme_light",
        "ure_theme_dark","ure_theme_dark_accent","ure_mirror_resolution","ure_mirror_refresh",
        "ure_mirror_scale_choice","ure_mirror_refresh_label","ure_auto_rotation","ure_auto_brightness","lock_btn",
        "center_clock","clock_style","style_battery","enable_battery","show_cpu_temp","tw_hidden_files",
        "lock_info","key_numbar","key_buttons"};
    return std::find(keys.begin(),keys.end(),key)!=keys.end();
}
Value ui_preferences_record(const Value& values) {
    require(values.isObject() && values.size()<=37,"invalid-preferences","Expected bounded UI preferences");
    for(const auto& key:values.getMemberNames()) {
        require(ui_preference_key(key) && values[key].isString() && values[key].asString().size()<=511 &&
            values[key].asString().find('\0')==std::string::npos,"invalid-preferences","Unknown or oversized UI preference");
        if(key=="ure_ui_scale_percent" || key=="ure_mirror_scale_choice")display_scale_parse(values[key].asString());
        const auto text=values[key].asString();
        auto integer=[&](int low,int high) {
            int number=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
            return !text.empty() && parsed.ec==std::errc() && parsed.ptr==text.data()+text.size() && number>=low && number<=high;
        };
        if(key=="tw_military_time" || key=="tw_no_screen_timeout" || key=="tw_time_zone_guidst" ||
            key=="ure_theme_active" || key=="ure_theme_dark_accent" || key=="ure_auto_rotation" ||
            key=="ure_auto_brightness" || key=="lock_btn" || key=="clock_style" || key=="enable_battery" ||
            key=="show_cpu_temp" || key=="tw_hidden_files" || key=="lock_info" || key=="key_numbar" || key=="key_buttons")
            require(text=="0" || text=="1","invalid-preferences","Expected a Boolean UI preference");
        if(key=="center_clock" || key=="style_battery")require(integer(0,2),"invalid-preferences","Invalid saved status bar style");
        if(key=="tw_brightness_pct")require(integer(5,100),"invalid-preferences","Saved brightness must be between 5 and 100 percent");
        if(key=="tw_brightness")require(integer(1,65535),"invalid-preferences","Invalid saved raw brightness");
        if(key=="tw_screen_timeout_secs")require(integer(0,86400),"invalid-preferences","Invalid saved screen timeout");
        if(key=="tw_button_vibrate" || key=="tw_keyboard_vibrate" || key=="tw_action_vibrate")
            require(integer(0,1000),"invalid-preferences","Invalid saved vibration duration");
        if(key=="tw_gui_sort_order")require(integer(-3,3) && text!="0","invalid-preferences","Invalid saved file sort order");
        if(key=="ure_theme_style")require(text=="Black" || text=="Cream" || text=="Dark" || text=="Gray" || text=="Light",
            "invalid-preferences","Unknown saved theme style");
        if(key=="ure_theme_light" || key=="ure_theme_dark")require(text.size()==7 && text.front()=='#' &&
            std::all_of(text.begin()+1,text.end(),[](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F'); }),
            "invalid-preferences","Invalid saved accent color");
        if(key=="tw_language")require(!text.empty() && text.size()<=32 &&
            std::all_of(text.begin(),text.end(),[](char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'; }),
            "invalid-preferences","Invalid saved language identifier");
        if(key=="ure_theme_accent")require(!text.empty() && text.size()<=32 &&
            std::all_of(text.begin(),text.end(),[](char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c==' ' || c=='_' || c=='-'; }),
            "invalid-preferences","Invalid saved accent name");
    }
    if(values.get("ure_theme_active","0")=="1")
        for(const auto* field:{"ure_theme_style","ure_theme_accent","ure_theme_light","ure_theme_dark","ure_theme_dark_accent"})
            require(values.isMember(field),"invalid-preferences","Active saved themes require the complete selection");
    Value record; record["schema"]=1; record["format"]="uke-ui-preferences"; record["values"]=values;
    record["values_sha256"]=sha256(json(values));
    require(json(record).size()<=32768,"size-limit","UI preference record exceeds 32 KiB");
    return record;
}
Value ui_preferences_read(const Root& directory) {
    struct stat dir{};
    require(::fstat(directory.fd(),&dir)==0 && S_ISDIR(dir.st_mode) && dir.st_uid==::geteuid() &&
        (dir.st_mode&07777)==0700,"unsafe-preferences","UI directory must be private and owned");
    struct stat st{};
    if(::fstatat(directory.fd(),"preferences.json",&st,AT_SYMLINK_NOFOLLOW)!=0) {
        require(errno==ENOENT,"unsafe-preferences","Cannot inspect the previous UI record");
        return ui_preferences_record(Value(Json::objectValue));
    }
    require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 && st.st_nlink==1 &&
        st.st_size>=0 && st.st_size<=32768,"unsafe-preferences","UI record must be private, regular and single-link");
    auto file=directory.open("preferences.json",O_RDONLY|O_NONBLOCK); struct stat opened{};
    require(::fstat(file.get(),&opened)==0 && opened.st_dev==st.st_dev && opened.st_ino==st.st_ino &&
        opened.st_uid==st.st_uid && opened.st_mode==st.st_mode && opened.st_nlink==1 && opened.st_size==st.st_size,
        "unsafe-preferences","UI record changed during admission");
    const auto record=parse_json(storage_read(file.get(),0,static_cast<std::size_t>(opened.st_size)));
    require(record.isObject() && record.size()==4 && record["schema"]==1 && record["format"]=="uke-ui-preferences" &&
        record==ui_preferences_record(record["values"]),"invalid-preferences","UI record failed schema or content verification");
    return record;
}
Value ui_preferences_write(const Root& directory,const Value& values) {
    const auto record=ui_preferences_record(values);
    // Admission also rejects a foreign, linked or non-private previous record.
    const auto previous=ui_preferences_read(directory);
    if(previous!=record || !directory.exists("preferences.json"))directory.save_record("preferences.json",record,true);
    const auto readback=ui_preferences_read(directory);
    require(readback==record,"readback-error","Saved UI preferences failed complete readback");
    return readback;
}
namespace {
Value preference_client(bool save,const Value& values) {
    // Run the namespace-owning CLI as a fresh single-threaded process. Never
    // fork-and-mount from the multithreaded GUI or invoke a shell with values.
    const std::string request=save ? json(ui_preferences_record(values)["values"]) : std::string{};
    std::array<char,64> name{}; std::strcpy(name.data(),"/tmp/uke-preferences-request-XXXXXX");
    Fd input(::mkostemp(name.data(),O_CLOEXEC));
    require(input.get()>=0,"preferences-unavailable","Cannot prepare a RAM preference request");
    ::unlink(name.data());
    struct statfs ram{};
    require(::fstatfs(input.get(),&ram)==0 && ram.f_type==0x01021994 && ::fchmod(input.get(),0600)==0,
        "preferences-unavailable","Preference requests require private tmpfs");
    for(std::size_t offset=0;offset<request.size();) {
        const auto count=::write(input.get(),request.data()+offset,request.size()-offset);
        if(count<0 && errno==EINTR)continue;
        require(count>0,"io-error","Cannot write the RAM preference request");
        offset+=static_cast<std::size_t>(count);
    }
    require(::lseek(input.get(),0,SEEK_SET)==0,"io-error","Cannot rewind preference request");
    int pipe[2]; require(::pipe2(pipe,O_CLOEXEC)==0,"io-error","Cannot open preference reply pipe");
    Fd read_end(pipe[0]),write_end(pipe[1]),null(::open("/dev/null",O_WRONLY|O_CLOEXEC|O_NOFOLLOW));
    require(null.get()>=0,"io-error","Cannot open preference diagnostic sink");
    posix_spawn_file_actions_t actions;
    require(::posix_spawn_file_actions_init(&actions)==0,"io-error","Cannot prepare preference subprocess");
    int action_error=0;
    action_error|=::posix_spawn_file_actions_adddup2(&actions,input.get(),0);
    action_error|=::posix_spawn_file_actions_adddup2(&actions,write_end.get(),1);
    action_error|=::posix_spawn_file_actions_adddup2(&actions,null.get(),2);
    DIR* descriptors=::opendir("/proc/self/fd");
    if(!descriptors)action_error=1;
    if(descriptors) {
        while(auto* entry=::readdir(descriptors)) {
            int fd=0; const std::string_view text(entry->d_name);
            const auto result=std::from_chars(text.data(),text.data()+text.size(),fd);
            if(result.ec==std::errc() && result.ptr==text.data()+text.size() && fd>=3 && fd!=::dirfd(descriptors))
                action_error|=::posix_spawn_file_actions_addclose(&actions,fd);
        }
        ::closedir(descriptors);
    }
    char command[]="/system/bin/uke-recoveryctl",mode[]= "preferences-private";
    char load[]="load",store[]="save",path[]="PATH=/system/bin",libs[]="LD_LIBRARY_PATH=/system/lib64:/system/lib";
    char locale[]="LC_ALL=C",lang[]="LANG=C";
    char* argv[]={command,mode,save?store:load,nullptr}; char* env[]={path,libs,locale,lang,nullptr};
    pid_t child=-1;
    const int spawned=action_error ? EINVAL : ::posix_spawn(&child,command,&actions,nullptr,argv,env);
    ::posix_spawn_file_actions_destroy(&actions);
    require(spawned==0,"preferences-unavailable","Cannot launch the installed preference helper");
    write_end=Fd{};
    int status=0; std::string response; bool eof=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    try {
        while(!eof) {
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            require(remaining>0,"preferences-timeout","Preference helper exceeded its deadline");
            pollfd fd{read_end.get(),POLLIN|POLLHUP,0};
            const int ready=::poll(&fd,1,static_cast<int>(std::min<std::int64_t>(remaining,1000)));
            if(ready<0 && errno==EINTR)continue;
            require(ready>=0,"io-error","Cannot wait for preference reply");
            if(!ready)continue;
            std::array<char,4096> bytes{}; const auto n=::read(read_end.get(),bytes.data(),bytes.size());
            if(n<0 && errno==EINTR)continue;
            require(n>=0,"io-error","Cannot read preference reply");
            eof=n==0; if(n>0)response.append(bytes.data(),static_cast<std::size_t>(n));
            require(response.size()<=65536,"size-limit","Preference reply exceeds its limit");
        }
        while(::waitpid(child,&status,0)<0)require(errno==EINTR,"io-error","Cannot reap preference helper");
        child=-1;
        const auto reply=parse_json(response);
        require(WIFEXITED(status) && WEXITSTATUS(status)==0 && reply.isObject() && reply["schema"]==1 &&
            reply["result"]=="ok" && reply["data"].isObject() && reply["data"]["persistent"]==true,
            "preferences-unavailable","Persistent preference storage refused the request");
        const auto record=ui_preferences_record(reply["data"]["values"]);
        require(!save || record["values"]==values,"readback-error","Preference reply differs from the requested values");
        require(reply["data"]["format"]==record["format"] && reply["data"]["schema"]==record["schema"] &&
            reply["data"]["values_sha256"]==record["values_sha256"],"readback-error","Preference helper returned an inconsistent record");
        return record;
    } catch(...) {
        if(child>0) { ::kill(child,SIGKILL); while(::waitpid(child,&status,0)<0 && errno==EINTR) {} }
        throw;
    }
}
}
Value ui_preferences_load() { return preference_client(false,Value(Json::objectValue)); }
Value ui_preferences_save(const Value& values) { return preference_client(true,values); }
Value ui_preferences_device(bool save,const Value& values) {
#ifndef __ANDROID__
    (void)save; (void)values;
    throw Error("preferences-unavailable","Device preference mounting is unavailable on the host");
#else
    // This sole storage exception writes UI preferences, not the legacy persist
    // settings/password fallback. Every other managed storage gate stays closed.
    require(::geteuid()==0,"preferences-unavailable","Preference mounting requires recovery root");
    std::array<char,PROP_VALUE_MAX> identity{};
    require(::__system_property_get("ro.product.device",identity.data())>0 &&
        std::string_view(identity.data())=="uke","preferences-profile","Preference storage requires the Uke device identity");
    if(save)(void)ui_preferences_record(values);
    Fd source(::open("/dev/block/sdf7",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));
    struct stat node{}; std::uint64_t bytes=0; int sector=0;
    require(source.get()>=0 && ::fstat(source.get(),&node)==0 && S_ISBLK(node.st_mode) &&
        ::ioctl(source.get(),BLKGETSIZE64,&bytes)==0 && ::ioctl(source.get(),BLKSSZGET,&sector)==0,
        "preferences-profile","Cannot identify the Uke persist partition");
    telemetry::detail::Reader sys("/sys");
    telemetry::detail::Fd entry(sys.directory("class/block/sdf7"));
    require(entry.get()>=0,"preferences-profile","Missing Uke persist sysfs identity");
    const std::string number=std::to_string(major(node.st_rdev))+":"+std::to_string(minor(node.st_rdev));
    auto attribute=[&](const char* name) {
        const auto value=sys.attribute(entry.get(),name);
        require(value.state==telemetry::State::observed,"preferences-profile","Unreadable persist geometry");
        return value.text;
    };
    require(attribute("dev")==number && clock_mount::persist_contract(attribute("partition"),attribute("start"),
        attribute("size"),bytes,sector),"preferences-profile","Persist geometry differs from the Uke profile");
    Fd event(::open("/sys/class/block/sdf7/uevent",O_RDONLY|O_CLOEXEC));
    std::array<char,513> event_bytes{}; const auto count=::read(event.get(),event_bytes.data(),event_bytes.size());
    require(count>0 && count<=512 && clock_mount::persist_event(std::string_view(event_bytes.data(),static_cast<std::size_t>(count)),"sdf7"),
        "preferences-profile","Persist label differs from the Uke profile");
    std::vector<std::string> holders,slaves;
    require(sys.entries("class/block/sdf7/holders",holders)==telemetry::State::observed && holders.empty() &&
        sys.entries("class/block/sdf/slaves",slaves)==telemetry::State::observed && slaves.empty(),
        "preferences-profile","Persist has an unresolved mapper dependency");
    std::array<unsigned char,4096> super{};
    require(::pread(source.get(),super.data(),super.size(),0)==static_cast<ssize_t>(super.size()) &&
        super[1080]==0x53 && super[1081]==0xef && (!save || (super[1082]==1 && super[1083]==0 && !(super[1120]&4))),
        "preferences-filesystem","Persist writes require clean ext4 without pending journal recovery");
    Fd info(::open("/proc/self/mountinfo",O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
    require(info.get()>=0,"preferences-profile","Cannot inspect inherited mounts");
    std::string contents; std::array<char,4096> buffer{};
    while(true) {
        const auto n=::read(info.get(),buffer.data(),buffer.size());
        if(n<0 && errno==EINTR)continue;
        require(n>=0,"preferences-profile","Cannot read inherited mounts");
        if(!n)break;
        contents.append(buffer.data(),static_cast<std::size_t>(n));
        require(contents.size()<=131072,"preferences-profile","Inherited mount table exceeds its limit");
    }
    std::vector<clock_mount::Mount> rows;
    require(clock_mount::mounts(contents,rows,false),"preferences-profile","Cannot parse inherited mounts");
    for(const auto& row:rows)if(row.device==number)
        require(!save && clock_mount::option(row.flags,"ro") && clock_mount::option(row.super_flags,"ro") && row.filesystem=="ext4",
            "preferences-busy","An inherited persist mount is writable or ambiguous");
    // The separate process is single-threaded. Mount propagation is disabled
    // before admitting any writable view; the GUI's persist mount stays read-only.
    require(::unshare(CLONE_NEWNS)==0 && ::mount(nullptr,"/",nullptr,MS_REC|MS_PRIVATE,nullptr)==0,
        "preferences-namespace","Cannot isolate preference filesystem access");
    struct statfs ram{};
    require(::statfs("/tmp",&ram)==0 && ram.f_type==TMPFS_MAGIC,"preferences-unavailable","Private mounts require recovery tmpfs");
    Fd lock(::open("/tmp/uke-ui-preferences.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600));
    struct stat lock_stat{};
    require(lock.get()>=0 && ::fstat(lock.get(),&lock_stat)==0 && S_ISREG(lock_stat.st_mode) && lock_stat.st_uid==0 &&
        lock_stat.st_nlink==1 && (lock_stat.st_mode&07777)==0600 && ::flock(lock.get(),LOCK_EX|LOCK_NB)==0,
        "preferences-busy","Preference storage is owned by another operation");
    std::array<char,64> mount_name{}; std::strcpy(mount_name.data(),"/tmp/uke-ui-store-XXXXXX");
    require(::mkdtemp(mount_name.data())!=nullptr,"io-error","Cannot reserve a private preference mount");
    bool mounted=false;
    try {
        const std::string device="/proc/self/fd/"+std::to_string(source.get());
        const unsigned long flags=MS_NOSUID|MS_NODEV|MS_NOEXEC|(save?0:MS_RDONLY);
        require(::mount(device.c_str(),mount_name.data(),"ext4",flags,save?nullptr:"noload")==0,
            "preferences-unavailable","Cannot mount the reviewed preference filesystem");
        mounted=true;
        Value record;
        {
        Root parent(mount_name.data()); struct stat mounted_root{}; struct statfs type{};
        require(::fstat(parent.fd(),&mounted_root)==0 && mounted_root.st_dev==node.st_rdev &&
            ::fstatfs(parent.fd(),&type)==0 && type.f_type==EXT4_SUPER_MAGIC &&
            ((type.f_flags&ST_RDONLY)!=0)==!save,"preferences-filesystem","Mounted preference filesystem differs from its admission");
        constexpr const char* directory="OrangeFox-uke-ui";
        struct stat existing{}; bool absent=false;
        if(::fstatat(parent.fd(),directory,&existing,AT_SYMLINK_NOFOLLOW)!=0) {
            require(errno==ENOENT,"unsafe-preferences","Cannot inspect UI directory");
            absent=!save;
            if(save) {
            struct statvfs space{};
            require(::fstatvfs(parent.fd(),&space)==0 && space.f_bavail>0 &&
                static_cast<std::uint64_t>(space.f_bavail)*space.f_frsize>=1048576+131072,
                "preferences-space","Persist lacks the 1 MiB free-space reserve");
            require(::mkdirat(parent.fd(),directory,0700)==0 && ::fsync(parent.fd())==0,
                "uncertain-preferences","Cannot create and synchronize the private UI directory");
            }
        }
        if(absent)record=ui_preferences_record(Value(Json::objectValue));
        else {
        if(save) {
            struct statvfs space{};
            require(::fstatvfs(parent.fd(),&space)==0 && space.f_bavail>0 &&
                static_cast<std::uint64_t>(space.f_bavail)*space.f_frsize>=1048576+131072,
                "preferences-space","Persist lacks the 1 MiB free-space reserve");
        }
        Fd dir(::openat(parent.fd(),directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
        require(dir.get()>=0,"unsafe-preferences","UI directory cannot be opened without aliases");
        Root store(std::move(dir));
        record=save ? ui_preferences_write(store,values) : ui_preferences_read(store);
        if(save)require(::syncfs(store.fd())==0,"uncertain-preferences","Cannot flush preference filesystem");
        }
        } // Release every filesystem descriptor before unmounting.
        require(::umount(mount_name.data())==0,"uncertain-preferences","Cannot release preference mount");
        mounted=false; ::rmdir(mount_name.data());
        auto result=record; result["persistent"]=true; result["calibration_files_written"]=false;
        return result;
    } catch(...) {
        if(mounted)::umount(mount_name.data());
        ::rmdir(mount_name.data()); throw;
    }
#endif
}
} // namespace ure
