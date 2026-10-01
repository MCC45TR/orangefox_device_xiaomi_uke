// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fcntl.h>
#include <sys/vfs.h>
#include <unistd.h>

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
} // namespace ure
