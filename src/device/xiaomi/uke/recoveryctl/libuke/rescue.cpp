// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/mount.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ure {
namespace {
std::string seal(Value value) { value.removeMember("plan_sha256"); return sha256(json(value)); }
std::string trim(std::string value) { const auto first=value.find_first_not_of(" \t\n\r"),last=value.find_last_not_of(" \t\n\r"); return first==value.npos ? "" : value.substr(first,last-first+1); }
std::string host_architecture() {
    struct utsname info{}; require(::uname(&info)==0,"identity-unavailable","Cannot inspect recovery architecture"); return info.machine;
}
Value executable(const Root& root,const std::string& path) {
    auto fd=root.open_resolved(path,O_RDONLY|O_NONBLOCK); struct stat st{}; std::array<unsigned char,20> header{};
    require(::fstat(fd.get(),&st)==0 && S_ISREG(st.st_mode) && (st.st_mode&0111) && st.st_size>=20 && st.st_size<=64*1024*1024 &&
        ::pread(fd.get(),header.data(),header.size(),0)==static_cast<ssize_t>(header.size()) && std::memcmp(header.data(),"\x7f" "ELF",4)==0 && header[4]==2 && header[5]==1,
        "native-executable-required","Managed rescue commands require a regular native ELF64 executable");
    const unsigned machine=static_cast<unsigned>(header[18])|static_cast<unsigned>(header[19])<<8;
    const auto architecture=machine==183 ? "aarch64" : machine==62 ? "x86_64" : "unsupported";
    require(architecture==host_architecture(),"architecture-mismatch","Installed programs cannot execute with this recovery architecture");
    auto out=descriptor_identity(fd.get()); out["sha256"]=sha256(fd.get()); out["path"]=path; out["architecture"]=architecture; return out;
}
std::string family(const Value& info) {
    std::string id=info["distribution"].get("ID","").asString(),like=info["distribution"].get("ID_LIKE","").asString();
    std::istringstream words(id+" "+like); std::string word;
    while(words>>word) { if(word=="arch" || word=="manjaro")return "arch";
        if(word=="fedora" || word=="rhel" || word=="centos")return "fedora";
        if(word=="debian" || word=="ubuntu")return "debian";
        if(word=="alpine")return "alpine"; }
    return "generic";
}
std::string choose(const Root& root,std::initializer_list<const char*> candidates) {
    for(const auto* path:candidates)if(root.exists_resolved(path))return path;
    throw Error("missing-rescue-tool","The selected distribution's native rescue tool is not installed");
}
Value mounted_connections(const Root& root,const Root* esp) {
    Value connections(Json::arrayValue); if(!root.exists_resolved("etc/fstab"))return connections;
    std::istringstream lines(root.read_resolved("etc/fstab",1024*1024)); std::string line; std::set<std::string> seen;
    while(std::getline(lines,line)) {
        line=trim(line); if(line.empty() || line[0]=='#')continue; std::istringstream row(line); std::string source,point,type,options;
        require(static_cast<bool>(row>>source>>point>>type>>options),"invalid-fstab","Malformed fstab entry");
        require(seen.insert(point).second,"invalid-fstab","Duplicate fstab mount point");
        if(point=="/" || point=="none" || type=="swap")continue;
        if(point!="/boot" && point!="/boot/efi" && point!="/efi" && point!="/home" && point!="/usr" && point!="/var")continue;
        const auto relative=point.substr(1); auto directory=root.open(relative,O_RDONLY|O_DIRECTORY); Value item;
        item["mount_point"]=relative; item["source_selector"]=source; item["filesystem"]=type;
        const auto location=descriptor_identity(directory.get());
        if(location["mount_id"]!=descriptor_identity(root.fd())["mount_id"]) { item["method"]="existing-mount"; item["identity"]=location; }
        else if(esp && (point=="/boot/efi" || point=="/efi" || (point=="/boot" && (type=="vfat" || type=="fat")))) {
            item["method"]="selected-esp"; item["identity"]=descriptor_identity(esp->fd());
        } else {
            require(source.starts_with("UUID=") || source.starts_with("PARTUUID="),"unstable-fstab-source","Automatic mounts require UUID or PARTUUID selectors");
            Root system("/"); const auto graph=storage_graph(system); Value selected;
            for(const auto& object:graph["objects"]) {
                if(object["partition"]!=true || (object["label"]!="uke_linux" && object["label"]!="uke_esp" && object["label"]!="uke_home"))continue;
                auto target=storage_select(system,object["stable_id"].asString()); const auto signature=filesystem_probe(target.descriptor.get());
                const bool matches=source.starts_with("PARTUUID=") ? target.identity["partuuid"]==source.substr(9) : signature["uuid"]==source.substr(5);
                if(matches) { require(selected.isNull(),"ambiguous-fstab-source","More than one storage object matches fstab");
                    require(signature["encryption"]=="none" && (signature["type"]=="ext4" || signature["type"]=="btrfs" || signature["type"]=="vfat"),
                        "unsupported-fstab-mount","Encrypted or unsupported automatic mount needs its own reviewed workflow");
                    selected=target.identity; item["detected_type"]=signature["type"]; }
            }
            require(selected.isObject(),"fstab-source-unavailable","A separate Linux/ESP fstab mount is unresolved; select its mounted ESP or mount the exact Linux filesystem first");
            item["method"]="automatic-block"; item["identity"]=selected;
            item["read_only_mount_options"]=item["detected_type"]=="ext4" ? "noload" : item["detected_type"]=="btrfs" ? "nologreplay" : "";
            // Preserve only subvolume selectors. Never interpret arbitrary fstab mount-helper options.
            std::istringstream flags(options); std::string flag;
            while(std::getline(flags,flag,','))if(flag.starts_with("subvol=") || flag.starts_with("subvolid=")) {
                require(item["detected_type"]=="btrfs" && flag.size()<=1024 && flag.find_first_of("\\ \t\n") == flag.npos,
                    "invalid-fstab","Invalid Btrfs subvolume selector");
                const auto value=flag.substr(flag.find('=')+1); components(value.starts_with('/') ? value.substr(1) : value);
                item["subvolume_option"]=flag;
            }
        }
        connections.append(item); require(connections.size()<=16,"size-limit","Too many automatic Linux connections");
    }
    return connections;
}
void hooks_without_python(const Root& root,const std::string& base,unsigned depth=0,unsigned* total=nullptr) {
    unsigned local=0; if(!total)total=&local;
    if(!root.exists_resolved(base))return;
    auto fd=root.open_resolved(base,O_RDONLY|O_DIRECTORY); Root directory(std::move(fd));
    for(const auto& name:directory.list(".",4096)) {
        require(++*total<=10000 && depth<=8,"size-limit","Initramfs hook inventory exceeds its bound");
        try { const auto st=directory.stat(name);
            if(S_ISDIR(st.st_mode))hooks_without_python(root,base+"/"+name,depth+1,total);
            else if(S_ISREG(st.st_mode) && st.st_size<1024*1024) {
                const auto text=directory.read(name,1024*1024);
                require(text.find("python")==text.npos && text.find("pypy")==text.npos,"python-hook-rejected","An installed initramfs hook references Python; it cannot run in this recovery");
            }
        } catch(const Error& error) { if(error.code=="path-unavailable")throw Error("unreviewed-hook-alias","Initramfs hooks with symlinks require explicit review"); throw; }
    }
}
std::string fd_path(int fd) { return "/proc/self/fd/"+std::to_string(fd); }
void readonly_recursive(int fd,bool read_only) {
    struct mount_attr attributes{};
    attributes.attr_set=MOUNT_ATTR_NOSUID|MOUNT_ATTR_NODEV|(read_only ? MOUNT_ATTR_RDONLY : 0);
    require(::syscall(SYS_mount_setattr,fd,"",AT_EMPTY_PATH|AT_RECURSIVE,&attributes,sizeof(attributes))==0,
        "mount-policy-unavailable","Kernel cannot enforce recursive rescue mount attributes");
}
void attach(int source,const std::string& destination,bool read_only) {
    // Descriptors retained before unshare refer to the previous mount namespace.
    // Reopen the kernel-reported directory in this clone, then prove the inode.
    std::array<char,4096> path{}; const auto count=::readlink(fd_path(source).c_str(),path.data(),path.size());
    require(count>0 && static_cast<std::size_t>(count)<path.size() && path[0]=='/',"stale-rescue-mount","Selected mount has no stable directory path in the private namespace");
    Root current(std::string(path.data(),static_cast<std::size_t>(count))); struct stat before{},after{};
    require(::fstat(source,&before)==0 && ::fstat(current.fd(),&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino,
        "stale-rescue-mount","Selected directory changed while entering the private mount namespace");
    Fd tree(static_cast<int>(::syscall(SYS_open_tree,current.fd(),"",AT_EMPTY_PATH|AT_RECURSIVE|OPEN_TREE_CLONE|OPEN_TREE_CLOEXEC)));
    require(tree.get()>=0,"mount-failed","Cannot clone the selected rescue mount through its descriptor");
    readonly_recursive(tree.get(),read_only);
    require(::syscall(SYS_move_mount,tree.get(),"",AT_FDCWD,destination.c_str(),MOVE_MOUNT_F_EMPTY_PATH)==0,
        "mount-failed","Cannot attach the selected rescue mount");
}
void tmpfs(const std::string& point,const char* options) {
    require(::mount("tmpfs",point.c_str(),"tmpfs",MS_NOSUID|MS_NODEV,options)==0,"mount-failed","Cannot create a private rescue runtime filesystem");
}
void drop_mount_capabilities() {
    struct __user_cap_header_struct header{}; header.version=_LINUX_CAPABILITY_VERSION_3; std::array<struct __user_cap_data_struct,2> data{};
    for(const auto cap:{CAP_CHOWN,CAP_DAC_OVERRIDE,CAP_FOWNER,CAP_SETUID,CAP_SETGID,CAP_SETFCAP}) {
        data[static_cast<std::size_t>(cap/32)].effective|=1U<<(cap%32); data[static_cast<std::size_t>(cap/32)].permitted|=1U<<(cap%32); }
    require(::syscall(SYS_capset,&header,data.data())==0,"capability-error","Cannot restrict the rescue command capabilities");
    require(::prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0,"capability-error","Cannot restrict privilege acquisition");
}
void report_error(int fd,const std::string& code) {
    const auto message=code+"\n"; static_cast<void>(::write(fd,message.data(),message.size()));
}
bool send_process_descriptor(int socket,int fd) {
    char marker='I'; iovec vector{&marker,1}; std::array<char,CMSG_SPACE(sizeof(int))> control{};
    msghdr message{}; message.msg_iov=&vector; message.msg_iovlen=1;
    message.msg_control=control.data(); message.msg_controllen=control.size();
    auto* item=CMSG_FIRSTHDR(&message); item->cmsg_level=SOL_SOCKET; item->cmsg_type=SCM_RIGHTS; item->cmsg_len=CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(item),&fd,sizeof(fd)); return ::sendmsg(socket,&message,MSG_NOSIGNAL)==1;
}
void receive_process_descriptor(int socket,Fd& process) {
    if(process.get()>=0)return;
    char marker=0; iovec vector{&marker,1}; std::array<char,CMSG_SPACE(sizeof(int))> control{};
    msghdr message{}; message.msg_iov=&vector; message.msg_iovlen=1;
    message.msg_control=control.data(); message.msg_controllen=control.size();
    const auto count=::recvmsg(socket,&message,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
    if(count<=0)return;
    auto* item=CMSG_FIRSTHDR(&message);
    if(count==1 && marker=='I' && item && item->cmsg_level==SOL_SOCKET && item->cmsg_type==SCM_RIGHTS && item->cmsg_len==CMSG_LEN(sizeof(int))) {
        int fd=-1; std::memcpy(&fd,CMSG_DATA(item),sizeof(fd)); process=Fd(fd);
    }
}
void child_setup(const Root& root,const Root* esp,const Value& plan,const std::string& anchor) {
    const bool writable=plan["request"]["write"].asBool();
    require(::mount(nullptr,"/",nullptr,MS_REC|MS_PRIVATE,nullptr)==0,"mount-failed","Cannot isolate mount propagation");
    attach(root.fd(),anchor,!writable); Root mounted(anchor);
    for(const auto& connection:plan["connections"]) {
        const auto point=connection["mount_point"].asString(); auto target=mounted.open(point,O_RDONLY|O_DIRECTORY); const auto destination=anchor+"/"+point;
        if(connection["method"]=="selected-esp") { require(esp,"esp-required","Selected ESP is required"); attach(esp->fd(),destination,!writable); }
        else if(connection["method"]=="automatic-block") {
            Root system("/"); auto source=storage_select(system,connection["identity"]["stable_id"].asString());
            require(json(source.identity)==json(connection["identity"]),"stale-fstab-source","Automatic filesystem identity changed");
            std::string options=connection.get("subvolume_option","").asString();
            if(!writable) { if(!options.empty())options+=","; options+=connection["read_only_mount_options"].asString(); }
            require(::mount(fd_path(source.descriptor.get()).c_str(),destination.c_str(),connection["detected_type"].asCString(),MS_NOSUID|MS_NODEV|(!writable ? static_cast<unsigned long>(MS_RDONLY) : 0UL),options.c_str())==0,
                "mount-failed","Cannot mount the exact Linux fstab filesystem");
        }
    }
    const auto proc=anchor+"/proc",sys=anchor+"/sys",dev=anchor+"/dev",run=anchor+"/run",temp=anchor+"/tmp";
    for(const auto* directory:{"proc","sys","dev","run","tmp"}) { auto fd=mounted.open(directory,O_RDONLY|O_DIRECTORY); (void)fd; }
    require(::mount("proc",proc.c_str(),"proc",MS_NOSUID|MS_NODEV|MS_NOEXEC,nullptr)==0,"mount-failed","Cannot mount the rescue process namespace");
    Root host_sys("/sys"); attach(host_sys.fd(),sys,true);
    tmpfs(dev,"size=4m,mode=0755"); tmpfs(run,"size=16m,mode=0755"); tmpfs(temp,"size=512m,mode=1777");
    for(const auto* name:{"null","zero","random","urandom","tty"}) {
        const auto source=std::string("/dev/")+name,destination=dev+"/"+name; Fd output(::open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));
        require(output.get()>=0 && ::mount(source.c_str(),destination.c_str(),nullptr,MS_BIND,nullptr)==0,"mount-failed","Cannot bind a minimal rescue character device");
    }
    require(::mkdir((dev+"/pts").c_str(),0755)==0 && ::mount("devpts",(dev+"/pts").c_str(),"devpts",MS_NOSUID|MS_NOEXEC,"newinstance,ptmxmode=0666,mode=0620")==0 &&
        ::symlink("pts/ptmx",(dev+"/ptmx").c_str())==0,"mount-failed","Cannot create private rescue pseudoterminals");
    // Keep only the selected executable and standard streams across exec.
    auto program=root.open_resolved(plan["executable"]["path"].asString(),O_RDONLY);
    require(::chroot(anchor.c_str())==0 && ::chdir("/")==0,"chroot-failed","Cannot enter the selected Linux root");
    drop_mount_capabilities();
    std::vector<std::string> words{plan["executable"]["path"].asString()}; for(const auto& item:plan["arguments"])words.push_back(item.asString());
    std::vector<char*> argv; for(auto& word:words)argv.push_back(word.data()); argv.push_back(nullptr);
    char path[]="PATH=/usr/sbin:/usr/bin:/sbin:/bin",term[]="TERM=xterm-256color",locale[]="LC_ALL=C",home[]="HOME=/root",shell[]="SHELL=/bin/bash";
    char* environment[]{path,term,locale,home,shell,nullptr};
    ::fexecve(program.get(),argv.data(),environment); throw Error("exec-failed","Installed command or its ELF interpreter cannot run");
}
} // namespace
Value linux_rescue_plan(const Root& root,const Value& request,const Root* esp) {
    filesystem_tree_gate(root.fd()); Root host("/"); require(descriptor_identity(root.fd())!=descriptor_identity(host.fd()),"invalid-rescue-root","The recovery root cannot be its own chroot");
    if(esp) { filesystem_tree_gate(esp->fd()); require(descriptor_identity(esp->fd())!=descriptor_identity(host.fd()),"invalid-rescue-esp","The recovery root cannot be selected as an ESP"); }
    require(request.isObject() && request["schema"]==1 && request["action"].isString() && request["write"].isBool() && request["network"].isBool() &&
        request["timeout_seconds"].isUInt() && request["timeout_seconds"].asUInt()>=1 && request["timeout_seconds"].asUInt()<=7200,
        "invalid-rescue-request","Rescue requests require action, write/network choices and a bounded timeout");
    for(const auto& key:request.getMemberNames())require(key=="schema" || key=="action" || key=="write" || key=="network" || key=="timeout_seconds" || key=="kernel_release" || key=="shell_input",
        "invalid-rescue-request","Unknown rescue request field");
    require(!request["network"].asBool(),"network-rescue-unavailable","Rescue currently uses an isolated network namespace");
    const auto action=request["action"].asString(),distribution=family(linux_detect(root)); const bool writable=request["write"].asBool();
    Value plan; plan["schema"]=1; plan["operation"]="linux.rescue"; plan["operation_id"]=operation_id(); plan["request"]=request;
    plan["root_identity"]=descriptor_identity(root.fd()); if(esp)plan["esp_identity"]=descriptor_identity(esp->fd());
    plan["distribution_family"]=distribution; plan["connections"]=mounted_connections(root,esp); plan["arguments"]=Value(Json::arrayValue);
    if(root.exists_resolved("etc/fstab"))plan["fstab_sha256"]=sha256(root.read_resolved("etc/fstab"));
    std::string executable_path;
    if(action=="shell") { executable_path=choose(root,{"usr/bin/bash","bin/bash"}); plan["arguments"].append("--noprofile"); plan["arguments"].append("--norc"); }
    else if(action=="package-check" || action=="package-repair") {
        require(action!="package-repair" || writable,"write-choice-required","Package database repair requires a reviewed writable session");
        if(distribution=="fedora") { executable_path=choose(root,{"usr/bin/rpmdb","usr/bin/rpm"}); plan["arguments"].append(action=="package-check" ? "--verifydb" : "--rebuilddb"); }
        else if(distribution=="arch") { require(action=="package-check","unsupported-rescue-action","Arch database writes require a separate package restore plan");
            executable_path=choose(root,{"usr/bin/pacman"}); plan["arguments"].append("-Dk"); }
        else if(distribution=="debian") { require(action=="package-check","unsupported-rescue-action","Debian package repair may execute arbitrary maintainer scripts"); executable_path=choose(root,{"usr/bin/dpkg"}); plan["arguments"].append("--audit"); }
        else throw Error("unsupported-distribution","No reviewed native package database adapter is available for this distribution");
    } else if(action=="module-index" || action=="initramfs-rebuild") {
        require(writable && request["kernel_release"].isString() && identifier(request["kernel_release"].asString()),"invalid-rescue-request","Kernel repair requires an exact installed release and writable session");
        const auto release=request["kernel_release"].asString(); require(root.exists_resolved("usr/lib/modules/"+release) || root.exists_resolved("lib/modules/"+release),"missing-kernel-modules","Selected kernel modules are absent");
        if(action=="module-index") { executable_path=choose(root,{"usr/sbin/depmod","usr/bin/depmod","sbin/depmod"}); plan["arguments"].append("-a"); plan["arguments"].append(release); }
        else if(distribution=="fedora") { hooks_without_python(root,"usr/lib/dracut/modules.d"); executable_path=choose(root,{"usr/bin/dracut"});
            // dracut is a shell script, invoked by the verified native Bash binary.
            const auto script=root.read_resolved(executable_path,1024*1024); require(script.starts_with("#!") && script.find("python")==script.npos,"python-hook-rejected","dracut script is unreviewed or references Python");
            plan["script_path"]=executable_path; plan["script_sha256"]=sha256(script); executable_path=choose(root,{"usr/bin/bash","bin/bash"});
            plan["arguments"].append("/"+plan["script_path"].asString()); plan["arguments"].append("--force"); plan["arguments"].append("--kver"); plan["arguments"].append(release);
            plan["arguments"].append("/boot/initramfs-"+release+".img"); plan["repair_output"]="boot/initramfs-"+release+".img";
        } else if(distribution=="arch") { hooks_without_python(root,"usr/lib/initcpio"); const auto script_path=choose(root,{"usr/bin/mkinitcpio"}); const auto script=root.read_resolved(script_path);
            require(script.starts_with("#!") && script.find("python")==script.npos,"python-hook-rejected","mkinitcpio script is unreviewed or references Python");
            plan["script_path"]=script_path; plan["script_sha256"]=sha256(script); executable_path=choose(root,{"usr/bin/bash","bin/bash"}); plan["arguments"].append("/"+script_path);
            plan["arguments"].append("-k"); plan["arguments"].append(release); plan["arguments"].append("-g"); plan["arguments"].append("/boot/initramfs-"+release+".img"); plan["repair_output"]="boot/initramfs-"+release+".img";
        } else throw Error("unsupported-distribution","No reviewed initramfs repair adapter is available");
    } else if(action=="selinux-relabel") { require(distribution=="fedora" && writable,"unsupported-rescue-action","SELinux repair requires a writable Fedora-family root");
        executable_path=choose(root,{"usr/sbin/restorecon","usr/bin/restorecon","sbin/restorecon"});
        for(const auto* word:{"-RF","/etc","/usr","/var"})plan["arguments"].append(word);
    } else throw Error("unsupported-rescue-action","Unknown managed rescue action");
    require(!request.isMember("shell_input") || (action=="shell" && request["shell_input"].isString() && request["shell_input"].asString().size()<=4096 &&
        request["shell_input"].asString().find('\0')==std::string::npos),"invalid-rescue-request","Bounded shell input applies only to an explicit shell session");
    plan["executable"]=executable(root,executable_path); for(const auto* point:{"proc","sys","dev","run","tmp"}) { auto directory=root.open(point,O_RDONLY|O_DIRECTORY); (void)directory; }
    plan["mount_namespace_private"]=true; plan["pid_namespace_private"]=true; plan["network_isolated"]=true;
    plan["raw_block_devices_exposed"]=false; plan["physical_test_record"]=false; plan["private_record"]=true;
    plan["risk"]=writable ? "Installed-system writes are enabled; keep a verified root/boot backup. Package scripts and user shell commands are not an atomic filesystem transaction." : "Selected root and ESP are read-only; private proc, sys, dev, run and tmp mounts are removed with the session.";
    plan["plan_sha256"]=seal(plan); return plan;
}
Value linux_rescue_execute(const Root& root,const Value& plan,const fs::path& path,const std::string& confirmation,const Root* esp) {
    require(plan["schema"]==1 && plan["operation"]=="linux.rescue" && plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()) && seal(plan)==plan["plan_sha256"].asString(),"invalid-rescue-plan","Invalid sealed rescue plan");
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact rescue plan hash");
    auto refreshed=linux_rescue_plan(root,plan["request"],esp); refreshed["operation_id"]=plan["operation_id"]; refreshed["plan_sha256"]=seal(refreshed);
    require(json(refreshed)==json(plan),"stale-rescue-plan","Installed root, tools, script, fstab or connections changed since review");
    auto store=private_directory(path,true); store.save_record("plan.json",plan); Value state;
    state["schema"]=1; state["plan_sha256"]=plan["plan_sha256"]; state["state"]="PREPARING"; store.save_record("state.json",state);
    if(plan["request"]["write"].asBool() && plan.isMember("repair_output") && root.exists_resolved(plan["repair_output"].asString())) {
        const auto backup=backup_file(root,plan["repair_output"].asString(),path/"original-initramfs.bin"); store.save_record("original-initramfs.json",backup);
    }
    const auto anchor=fs::absolute(path/"mount-root").lexically_normal().string(); require(::mkdirat(store.fd(),"mount-root",0700)==0,"io-error","Cannot create private rescue mount anchor");
    int pipe[2]{},channel[2]{};
    require(::pipe2(pipe,O_CLOEXEC|O_NONBLOCK)==0,"process-error","Cannot create rescue status pipe"); Fd output(pipe[0]),input(pipe[1]);
    require(::socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,channel)==0,"process-error","Cannot create rescue lifetime channel"); Fd monitor(channel[0]),sender(channel[1]);
    const bool interactive=plan["request"]["action"]=="shell" && !plan["request"].isMember("shell_input"); Fd console,console_read,console_write;
    if(!interactive) {
        console=store.open("console.log",O_WRONLY|O_CREAT|O_EXCL,0600); int capture[2]{};
        require(::pipe2(capture,O_CLOEXEC)==0,"process-error","Cannot create private rescue console pipe"); console_read=Fd(capture[0]); console_write=Fd(capture[1]);
        require(::fcntl(console_read.get(),F_SETFL,O_NONBLOCK)==0,"io-error","Cannot make rescue console collection nonblocking");
    }
    const pid_t owner=::getpid(),worker=::fork(); require(worker>=0,"process-error","Cannot create rescue namespace worker");
    if(worker==0) {
        output=Fd(); monitor=Fd(); console_read=Fd(); console=Fd(); ::prctl(PR_SET_PDEATHSIG,SIGKILL);
        Fd lifetime(static_cast<int>(::syscall(SYS_pidfd_open,::getpid(),0)));
        if(lifetime.get()<0 || ::getppid()!=owner || ::unshare(CLONE_NEWNS|CLONE_NEWPID|CLONE_NEWIPC|CLONE_NEWUTS|CLONE_NEWNET)!=0) { report_error(input.get(),"namespace-unavailable"); ::_exit(125); }
        const pid_t init=::fork(); if(init<0) { report_error(input.get(),"process-error"); ::_exit(125); }
        if(init==0) {
            sender=Fd();
            ::prctl(PR_SET_PDEATHSIG,SIGKILL); pollfd lifetime_check{lifetime.get(),POLLIN,0};
            if(::poll(&lifetime_check,1,0)>0)::_exit(125);
            try {
                if(plan["request"].isMember("shell_input")) { int shell_pipe[2]{}; require(::pipe2(shell_pipe,O_CLOEXEC)==0,"process-error","Cannot create explicit shell input");
                    const auto text=plan["request"]["shell_input"].asString(); require(::write(shell_pipe[1],text.data(),text.size())==static_cast<ssize_t>(text.size()),"io-error","Cannot supply explicit shell input");
                    ::close(shell_pipe[1]); require(::dup2(shell_pipe[0],STDIN_FILENO)>=0,"io-error","Cannot connect shell input"); ::close(shell_pipe[0]); }
                // PID 1 supervises the payload and kills/reaps every surviving descendant.
                require(::mount(nullptr,"/",nullptr,MS_REC|MS_PRIVATE,nullptr)==0,"mount-failed","Cannot isolate rescue mounts");
                const pid_t payload=::fork(); require(payload>=0,"process-error","Cannot start rescue command");
                if(payload==0) {
                    ::prctl(PR_SET_PDEATHSIG,SIGKILL);
                    if(console_write.get()>=0)require(::dup2(console_write.get(),STDERR_FILENO)>=0,"io-error","Cannot connect private rescue output");
                    require(::dup2(STDERR_FILENO,STDOUT_FILENO)>=0,"io-error","Cannot connect rescue console");
                    child_setup(root,esp,plan,anchor); ::_exit(126);
                }
                console_write=Fd();
                int status=0; pid_t waited=0;
                while((waited=::waitpid(payload,&status,WNOHANG))==0 || (waited<0 && errno==EINTR)) {
                    pollfd alive{lifetime.get(),POLLIN,0}; if(::poll(&alive,1,20)>0) { static_cast<void>(::kill(-1,SIGKILL)); ::_exit(125); }
                }
                static_cast<void>(::kill(-1,SIGKILL)); while(::waitpid(-1,nullptr,0)>0 || errno==EINTR) {}
                ::_exit(waited==payload ? (WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status)) : 125);
            } catch(const Error& error) { report_error(input.get(),error.code+"\t"+error.what()+" ("+std::strerror(errno)+")"); ::_exit(125); }
        }
        Fd init_process(static_cast<int>(::syscall(SYS_pidfd_open,init,0)));
        if(init_process.get()<0 || !send_process_descriptor(sender.get(),init_process.get())) {
            report_error(input.get(),"lifetime-channel-failed"); static_cast<void>(::kill(init,SIGKILL));
        }
        sender=Fd(); console_write=Fd();
        input=Fd(); int status=0; pid_t waited; do { waited=::waitpid(init,&status,0); } while(waited<0 && errno==EINTR);
        ::_exit(waited==init ? (WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status)) : 125);
    }
    input=Fd(); sender=Fd(); console_write=Fd(); Fd init_process; std::uint64_t logged=0; bool truncated=false,console_failed=false;
    auto collect_console=[&] {
        if(console_read.get()<0)return;
        std::array<char,8192> buffer{};
        for(unsigned blocks=0;blocks<32;++blocks) {
            const auto count=::read(console_read.get(),buffer.data(),buffer.size()); if(count<0 && errno==EINTR)continue; if(count<=0)break;
            const auto amount=static_cast<std::size_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(count),2*1024*1024-logged));
            std::size_t done=0; while(done<amount) {
                const auto wrote=::write(console.get(),buffer.data()+done,amount-done); if(wrote<0 && errno==EINTR)continue;
                if(wrote<=0) { console_failed=true; break; } done+=static_cast<std::size_t>(wrote);
            }
            logged+=done; truncated=truncated || amount<static_cast<std::size_t>(count);
        }
    };
    state["state"]="RUNNING"; state["worker_pid"]=worker; store.save_record("state.json",state,true);
    const auto deadline=monotonic_ms()+static_cast<std::uint64_t>(plan["request"]["timeout_seconds"].asUInt())*1000;
    int status=0; bool timeout=false; pid_t waited=0;
    for(;;) {
        waited=::waitpid(worker,&status,WNOHANG); if(waited<0 && errno==EINTR)continue; if(waited!=0)break;
        receive_process_descriptor(monitor.get(),init_process);
        collect_console();
        if(monotonic_ms()>=deadline) {
            timeout=true; if(init_process.get()>=0)static_cast<void>(::syscall(SYS_pidfd_send_signal,init_process.get(),SIGKILL,nullptr,0));
            ::kill(worker,SIGKILL); do { waited=::waitpid(worker,&status,0); } while(waited<0 && errno==EINTR); break;
        }
        pollfd item{init_process.get()>=0 ? init_process.get() : monitor.get(),POLLIN,0}; ::poll(&item,1,20);
    }
    require(waited==worker,"process-error","Cannot collect rescue namespace worker"); receive_process_descriptor(monitor.get(),init_process);
    bool namespace_released=!timeout && WIFEXITED(status);
    if(init_process.get()>=0) {
        pollfd completed{init_process.get(),POLLIN,0};
        namespace_released=::poll(&completed,1,timeout ? 5000 : 0)>0 && (completed.revents&POLLIN);
    } else if(timeout)namespace_released=false;
    collect_console();
    std::array<char,1024> error{}; const auto count=::read(output.get(),error.data(),error.size());
    if(namespace_released)require(::unlinkat(store.fd(),"mount-root",AT_REMOVEDIR)==0 && ::fsync(store.fd())==0,"cleanup-failed","Private rescue mount anchor could not be removed");
    state["state"]=timeout ? "TIMED_OUT" : WIFEXITED(status) && WEXITSTATUS(status)==0 ? "COMPLETE" : "FAILED";
    state["exit_status"]=WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status); state["successful"]=state["state"]=="COMPLETE";
    state["namespace_worker_reaped"]=true; state["session_mounts_released"]=namespace_released; state["cleanup_pending"]=!namespace_released;
    state["descendants_bound_to_pid_namespace"]=init_process.get()>=0;
    state["host_mounts_unmounted"]=false; state["physical_test_record"]=false;
    if(count>0) {
        const auto message=trim(std::string(error.data(),static_cast<std::size_t>(count))); const auto tab=message.find('\t');
        state["error_code"]=message.substr(0,tab); if(tab!=message.npos)state["error_message"]=message.substr(tab+1);
    }
    if(!interactive) {
        require(::fsync(console.get())==0,"io-error","Cannot sync private rescue console");
        state["console_file"]="console.log"; state["console_bytes"]=Json::UInt64(logged); state["console_limit_bytes"]=2*1024*1024;
        state["console_truncated"]=truncated; state["console_write_failed"]=console_failed;
    }
    if(plan.isMember("repair_output") && root.exists_resolved(plan["repair_output"].asString())) { auto file=root.open_resolved(plan["repair_output"].asString(),O_RDONLY); state["output_sha256"]=sha256(file.get()); }
    store.save_record("state.json",state,true); return state;
}
} // namespace ure
