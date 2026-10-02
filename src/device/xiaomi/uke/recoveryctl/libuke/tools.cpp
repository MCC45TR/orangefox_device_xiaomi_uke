// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <linux/magic.h>
#include <sys/statfs.h>

namespace ure {
static Value process_json(const ProcessResult& result) {
    Value output; output["exit_status"]=result.status; output["timed_out"]=result.timed_out;
    output["output"]=redact(result.output); output["successful"]=result.status==0 && !result.timed_out; return output;
}
Value filesystem_check(int fd) {
    const auto type=filesystem_probe(fd)["type"].asString();
    const auto source="/proc/self/fd/"+std::to_string(fd);
    std::string tool; std::vector<std::string> args;
    if(type=="ext4") { tool="e2fsck"; args={"-f","-n",source}; }
    else if(type=="vfat") { tool="fsck.fat"; args={"-n",source}; }
    else if(type=="f2fs") { tool="fsck.f2fs"; args={"--dry-run",source}; }
    else if(type=="exfat") { tool="fsck.exfat"; args={"-n",source}; }
    else if(type=="ntfs") {
#ifdef __ANDROID__
        tool="fsck.ntfs"; // The pinned Android module is ntfsprogs/ntfsfix.c.
#else
        tool="ntfsfix"; // Host fsck.ntfs may be a different tool without -n.
#endif
        args={"-n",source};
    }
    else if(type=="btrfs") { tool="btrfs"; args={"check","--readonly",source}; }
    else throw Error("unsupported-filesystem","No reviewed read-only checker is available for the detected signature");
    auto output=process_json(run_tool(tool,args,300,{}, {fd}));
    output["filesystem"]=type; output["read_only"]=true; output["signature_only"]=false;
    output["source_write_descriptor"]=false;
    if(type=="ntfs")output["check_scope"]="Limited ntfsfix metadata validation; full NTFS repair still requires Windows chkdsk";
    return output;
}
Value image_tool(const std::string& command,const std::string& operation,int fd) {
    const auto signature=filesystem_probe(fd);
    const auto type=signature["type"].asString();
    const auto source="/proc/self/fd/"+std::to_string(fd);
    std::string tool; std::vector<std::string> args;
    if(command=="crypto" && operation=="info") {
        tool="cryptsetup";
        if(signature["encryption"]=="LUKS")args={"luksDump",source};
        else if(signature["encryption"]=="BITLK")args={"bitlkDump",source};
        else throw Error("unsupported-crypto","No recognized LUKS or BITLK header");
    } else if(command=="wim" && (operation=="info" || operation=="verify") && type=="wim") {
        tool="wimlib-imagex"; args={operation,source};
    } else if(command=="ntfs" && operation=="info" && type=="ntfs") {
        tool="ntfsresize"; args={"--info","--no-action",source};
    } else if(command=="btrfs" && operation=="check" && type=="btrfs") {
        tool="btrfs"; args={"check","--readonly",source};
    } else throw Error("unsupported-image","Operation does not match the detected image signature");
    auto output=process_json(run_tool(tool,args,300,{}, {fd})); output["read_only"]=true;
    output["signature"]=signature; return output;
}
Value tool_operation(const std::vector<std::string>& args, const Root& root, const Root& system) {
    (void)system;
    if(args.size()==2 && args[0]=="btrfs") {
        struct statfs info{};
        require(::fstatfs(root.fd(),&info)==0 && info.f_type==BTRFS_SUPER_MAGIC,"not-btrfs","Selected root is not a mounted Btrfs filesystem");
        const auto source="/proc/self/fd/"+std::to_string(root.fd());
        std::vector<std::string> operation;
        if(args[1]=="subvolumes")operation={"subvolume","list","-a","-u","-q",source};
        else if(args[1]=="usage")operation={"filesystem","usage","--raw",source};
        else if(args[1]=="scrub-status")operation={"scrub","status",source};
        else if(args[1]=="balance-status")operation={"balance","status",source};
        else if(args[1]=="device-stats")operation={"device","stats",source};
        else throw Error("unknown-command","Unknown read-only Btrfs operation");
        auto output=process_json(run_tool("btrfs",operation,30,{}, {root.fd()})); output["read_only"]=true; return output;
    }
    if(args.size()==2 && args[0]=="android" && args[1]=="slots") {
        Value output;
        for(const auto& command : {"get-number-slots","get-current-slot","get-snapshot-merge-status"})output[command]=process_json(run_tool("bootctl",{command}));
        output["read_only"]=true; return output;
    }
    if(args.size()==2 && args[0]=="android" && args[1]=="super")return process_json(run_tool("lpdump",{}));
    if(args.size()==2 && args[0]=="boot" && args[1]=="once")throw Error("backend-unavailable","No accepted Uke boot routing backend is installed");
    throw Error("unknown-command","Command is not implemented; use help for available interfaces");
}
} // namespace ure
