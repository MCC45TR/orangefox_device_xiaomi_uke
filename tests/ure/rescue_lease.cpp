// SPDX-License-Identifier: Apache-2.0
// No installed OS, payload execution, real mount or unshare in this fixture.
#include "uke.h"
#include "operation_lease.hpp"
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/utsname.h>
#include <unistd.h>

namespace {
bool fail_fork=false;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void write_file(const ure::fs::path& path,const std::string& bytes) {
    ure::fs::create_directories(path.parent_path()); std::ofstream stream(path,std::ios::binary);
    stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); check(stream.good(),"Cannot write private rescue fixture"); stream.close();
}
}
extern "C" pid_t __real_fork();
extern "C" pid_t __wrap_fork() { if(fail_fork) { errno=EAGAIN; return -1; } return __real_fork(); }
extern "C" int __wrap_unshare(int) { errno=EPERM; return -1; }
extern "C" int __wrap_mount(const char*,const char*,const char*,unsigned long,const void*) { ::_exit(90); }
int main(int argc,char** argv) {
    auto pattern=(ure::fs::path(argc>1 ? argv[1] : ".")/"ure-rescue-ownership-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back('\0'); const auto made=::mkdtemp(name.data()); if(!made)return 1;
    const ure::fs::path work(made);
    try {
        const auto root_path=work/"root"; write_file(root_path/"etc/os-release","ID=arch\nNAME=Arch fixture\n");
        write_file(root_path/"etc/fstab","UUID=fixture-root / ext4 defaults 0 1\n"); write_file(root_path/"etc/marker","original contents\n");
        // This deliberately incomplete header is only plan metadata. unshare
        // is refused before init/payload creation, so it is never executed.
        std::string elf(20,'\0'); elf.replace(0,4,"\x7f" "ELF"); elf[4]=2; elf[5]=1; struct utsname host{};
        check(::uname(&host)==0,"Cannot inspect host fixture architecture"); const auto machine=std::string(host.machine)=="aarch64" ? 183 : 62;
        elf[18]=static_cast<char>(machine); write_file(root_path/"usr/bin/bash",elf);
        check(::chmod((root_path/"usr/bin/bash").c_str(),0700)==0,"Cannot set fixture executable permission");
        for(const auto* directory:{"proc","sys","dev","run","tmp"})ure::fs::create_directories(root_path/directory);
        ure::Root root(root_path); const auto original=ure::sha256(root.read("etc/marker"));
        for(const bool writable:{false,true})for(const bool failed_fork:{false,true}) {
            ure::Value request; request["schema"]=1; request["action"]="shell"; request["write"]=writable; request["network"]=false;
            request["timeout_seconds"]=5; request["shell_input"]="exit 0\n"; const auto plan=ure::linux_rescue_plan(root,request);
            const auto journal=work/(std::string(writable ? "writable" : "readonly")+(failed_fork ? "-fork" : "-namespace"));
            fail_fork=failed_fork; const auto result=ure::linux_rescue_execute(root,plan,journal,plan["plan_sha256"].asString()); fail_fork=false;
            check(result["successful"]==false && result["session_init_started"]==false && result["session_mounts_released"]==true &&
                result["cleanup_pending"]==false && result["ownership_lifetime_verified"]==true && result["operation_owner_released"]==true,
                "Pre-init/fork failure did not prove and release its native lifetime");
            check(result["target_contents_verified"]==false,"Lifetime closure claimed installed content validation");
            check(result["error_code"]==(failed_fork ? "process-error" : "namespace-unavailable"),"Wrong pre-init failure code");
            check(!ure::fs::exists(journal/"mount-root") && ure::sha256(root.read("etc/marker"))==original,"Failed pre-init session changed its original or retained anchor");
            check(ure::operation_lease_status()["state"]=="IDLE","Verified pre-init failure stranded shared ownership");
            { auto lifecycle=ure::LifecycleLease::acquire("reboot"); lifecycle.require_active(); }
        }
        ure::fs::remove_all(work);
        std::cout<<"PASS rescue ownership: injected worker fork/unshare failures, read-only/writable plans, private no-init lifetime, anchor cleanup, unchanged private contents and no mount/payload execution; host fixtures only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\nPrivate fixture retained: "<<work<<'\n'; return 1; }
}
