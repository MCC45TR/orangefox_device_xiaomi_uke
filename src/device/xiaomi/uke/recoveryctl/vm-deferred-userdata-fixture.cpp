// SPDX-License-Identifier: Apache-2.0
// Host-launched generic ARM64 VM only; this fixture is never a product payload.
#include "libuke/dualboot_device.cpp"
#include <iostream>
int main() {
    try {
        ure::Root system("/");
        ure::require(system.read("proc/cmdline",4096).find("ure_userdata_fixture=1")!=std::string::npos,"not-a-fixture","Disposable VM marker required");
        ure::Fd raw(::open("/dev/vda",O_RDWR|O_CLOEXEC|O_EXCL)); struct stat info{};
        constexpr std::uint64_t edge=8*1048576ULL,extent=512*1048576ULL;
        ure::require(raw.get()>=0 && ::fstat(raw.get(),&info)==0 && S_ISBLK(info.st_mode) && ure::storage_bytes(raw.get())==extent+2*edge,
            "wrong-fixture","Exactly one disposable 528 MiB block fixture is required");
        const std::string sentinel(131072,'Q');
        for(const auto offset:{std::uint64_t(0),edge,edge+extent})ure::write_exact(raw.get(),offset,sentinel);
        ure::sync_fd(raw.get()); raw=ure::Fd();
        const auto number=ure::devnumber(ure::trimmed(system.read("sys/devices/virtual/misc/device-mapper/dev",64)));
        ure::require(::mknod("/dev/device-mapper",S_IFCHR|0600,number)==0 || errno==EEXIST,"mapper-unavailable","Cannot publish VM mapper control");
        ure::DmControl control(system);
        const auto view=control.create_linear("ure-deferred-fixture","URE-DUALBOOT-fixture",info.st_rdev,edge,extent);
        const auto object=ure::map_node(view,extent);
        ure::dualboot_view_handoff(
            [&](bool exclusive){return ure::node(system,object,true,exclusive,true);},
            [&](const ure::Fd&,int count){ure::owned_view_policy(control.table(view.name),view,count);},
            [&](const ure::Fd& fd){ure::erase_android_userdata_header(fd.get());},
            [&](const ure::Fd& fd){ure::require(ure::storage_read(fd.get(),16384,sentinel.size()-16384)==sentinel.substr(16384),"header-overrun","Erase exceeded its exact 16 KiB boundary");});
        control.remove(view.name,view.uuid);
        raw=ure::Fd(::open("/dev/vda",O_RDONLY|O_CLOEXEC|O_EXCL));
        ure::require(raw.get()>=0 && ure::storage_read(raw.get(),0,sentinel.size())==sentinel &&
            ure::storage_read(raw.get(),edge+extent,sentinel.size())==sentinel && ure::storage_read(raw.get(),edge,16384)==std::string(16384,'\0'),
            "neighbor-changed","A byte outside the bounded userdata header changed");
        std::cout<<"URE_DEFERRED_USERDATA_VM 0\n"; return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
