// SPDX-License-Identifier: Apache-2.0
// Disposable generic-kernel VM fixture. This is not a shipping CLI backend.
#include "dualboot_view_handoff.hpp"
#include <errno.h>
#include <fcntl.h>
#include <linux/dm-ioctl.h>
#include <linux/fs.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr uint64_t mib=1024*1024, edge=8*mib, extent=512*mib, capacity=extent+2*edge;
constexpr const char* node="/dev/ure-format-view";
constexpr const char* foreign_node="/dev/ure-format-foreign";
constexpr const char* fat_node="/dev/ure-format-fat300";
[[noreturn]] void fail(const char* why,int status=37) {
    fprintf(stderr,"URE_FORMAT_FIXTURE_REFUSAL %s errno=%d\n",why,errno); fflush(nullptr); _exit(status);
}
void need(bool ok,const char* why) { if(!ok)fail(why); }
struct Fd {
    int value=-1;
    explicit Fd(int fd=-1):value(fd) {}
    ~Fd() { if(value>=0)close(value); }
    Fd(Fd&& other):value(other.value) { other.value=-1; }
    Fd& operator=(Fd&& other) { if(this!=&other) { if(value>=0)close(value); value=other.value; other.value=-1; } return *this; }
    Fd(const Fd&)=delete; Fd& operator=(const Fd&)=delete;
};
uint64_t bytes(int fd) { uint64_t size=0; need(ioctl(fd,BLKGETSIZE64,&size)==0,"block size unavailable"); return size; }
void read_exact(int fd,void* data,size_t count,uint64_t offset) {
    auto* p=static_cast<unsigned char*>(data); size_t done=0;
    while(done<count) { const auto n=pread(fd,p+done,count-done,static_cast<off_t>(offset+done)); if(n<0&&errno==EINTR)continue;
        need(n>0,"short fixture read"); done+=static_cast<size_t>(n); }
}
void write_exact(int fd,const void* data,size_t count,uint64_t offset) {
    const auto* p=static_cast<const unsigned char*>(data); size_t done=0;
    while(done<count) { const auto n=pwrite(fd,p+done,count-done,static_cast<off_t>(offset+done)); if(n<0&&errno==EINTR)continue;
        need(n>0,"short fixture write"); done+=static_cast<size_t>(n); }
}
struct Table {
    char name[DM_NAME_LEN]{},uuid[DM_UUID_LEN]{},type[DM_MAX_TYPE_NAME]{},parameters[128]{};
    dev_t device=0; uint64_t start=0,length=0; uint32_t flags=0,event=0; int opens=-1;
};
struct alignas(8) Buffer { unsigned char data[4096]{}; };
class Maps {
    Fd control;
    static Buffer request(const char* name,uint32_t flags=0) {
        Buffer b; auto* io=reinterpret_cast<dm_ioctl*>(b.data);
        io->version[0]=DM_VERSION_MAJOR; io->data_size=sizeof(b); io->data_start=sizeof(dm_ioctl); io->flags=flags;
        need(strlen(name)<sizeof(io->name),"map name too long"); strcpy(io->name,name); return b;
    }
    void call(unsigned long operation,Buffer& b) const {
        auto* io=reinterpret_cast<dm_ioctl*>(b.data);
        const auto result=ioctl(control.value,operation,io); const auto error=errno;
        printf("URE_DM_IOCTL command=%lu number=%u result=%d errno=%d data_size=%u fixed_fields=%zu struct_size=%zu data_start=%u flags=%u targets=%u version=%u.%u.%u\n",
            operation,static_cast<unsigned>(_IOC_NR(operation)),result,error,io->data_size,offsetof(dm_ioctl,data),sizeof(dm_ioctl),io->data_start,io->flags,
            io->target_count,io->version[0],io->version[1],io->version[2]); fflush(nullptr);
        need(result==0,"mapper ioctl refused");
        need(io->version[0]==DM_VERSION_MAJOR && io->data_size>=offsetof(dm_ioctl,data) && io->data_size<=sizeof(b) &&
            !(io->flags&DM_BUFFER_FULL_FLAG),"mapper result truncated");
    }
public:
    Maps() {
        FILE* file=fopen("/sys/class/misc/device-mapper/dev","r"); unsigned maj=0,min=0;
        need(file && fscanf(file,"%u:%u",&maj,&min)==2,"mapper identity unavailable"); fclose(file);
        mkdir("/dev/mapper",0700);
        if(mknod("/dev/mapper/control",S_IFCHR|0600,makedev(maj,min))!=0)need(errno==EEXIST,"cannot create mapper control");
        control=Fd(open("/dev/mapper/control",O_RDWR|O_CLOEXEC)); struct stat st{};
        need(control.value>=0 && fstat(control.value,&st)==0 && S_ISCHR(st.st_mode) && st.st_rdev==makedev(maj,min),"wrong mapper control");
    }
    Table table(const char* name) const {
        auto b=request(name,DM_STATUS_TABLE_FLAG); call(DM_TABLE_STATUS,b); const auto* io=reinterpret_cast<const dm_ioctl*>(b.data);
        need(io->target_count==1 && (io->flags&DM_ACTIVE_PRESENT_FLAG) && !(io->flags&(DM_SUSPEND_FLAG|DM_INACTIVE_PRESENT_FLAG)) &&
            io->data_start>=sizeof(dm_ioctl) && io->data_start%alignof(dm_target_spec)==0 && io->data_start<io->data_size &&
            sizeof(dm_target_spec)<io->data_size-io->data_start,"unsupported mapper response");
        const auto* spec=reinterpret_cast<const dm_target_spec*>(b.data+io->data_start);
        const auto room=io->data_size-io->data_start-sizeof(*spec); const auto* parameters=reinterpret_cast<const char*>(spec+1);
        need(strnlen(io->name,sizeof(io->name))<sizeof(io->name) && strnlen(io->uuid,sizeof(io->uuid))<sizeof(io->uuid) &&
            strnlen(spec->target_type,sizeof(spec->target_type))<sizeof(spec->target_type) && strnlen(parameters,room)<room && strlen(parameters)<128,
            "unterminated mapper response");
        Table result; strcpy(result.name,io->name); strcpy(result.uuid,io->uuid); strcpy(result.type,spec->target_type);
        strcpy(result.parameters,parameters); result.device=static_cast<dev_t>(io->dev); result.start=spec->sector_start;
        result.length=spec->length; result.flags=io->flags; result.event=io->event_nr; result.opens=io->open_count; return result;
    }
    Table create(const char* name,const char* uuid,dev_t backing,const char* path,uint64_t view_bytes=extent) const {
        auto b=request(name); auto* io=reinterpret_cast<dm_ioctl*>(b.data); strcpy(io->uuid,uuid); call(DM_DEV_CREATE,b);
        b=request(name); io=reinterpret_cast<dm_ioctl*>(b.data); io->target_count=1;
        need(view_bytes>0 && view_bytes<=extent && view_bytes%4096==0,"invalid fixture view size");
        auto* spec=reinterpret_cast<dm_target_spec*>(b.data+io->data_start); spec->length=view_bytes/512; strcpy(spec->target_type,"linear");
        char parameters[128]; snprintf(parameters,sizeof(parameters),"%u:%u %llu",major(backing),minor(backing),static_cast<unsigned long long>(edge/512));
        strcpy(reinterpret_cast<char*>(spec+1),parameters); spec->next=static_cast<uint32_t>((sizeof(*spec)+strlen(parameters)+1+7)&~size_t(7));
        io->data_size=io->data_start+spec->next; call(DM_TABLE_LOAD,b); b=request(name); call(DM_DEV_SUSPEND,b);
        const auto result=table(name); need(result.opens==0 && result.start==0 && result.length==view_bytes/512 && strcmp(result.parameters,parameters)==0,
            "new bounded map differs");
        need(mknod(path,S_IFBLK|0600,result.device)==0,"cannot publish fixture view"); return result;
    }
    void remove(const Table& expected,const char* path) const {
        const auto current=table(expected.name); need(current.opens==0 && strcmp(current.uuid,expected.uuid)==0,"busy or replaced map cleanup");
        auto b=request(expected.name); call(DM_DEV_REMOVE,b); need(unlink(path)==0,"fixture node cleanup failed");
    }
};
void verify(const Maps& maps,const Table& expected,const Fd& retained,int opens,bool write) {
    struct stat st{}; const int flags=fcntl(retained.value,F_GETFL); const auto actual=maps.table(expected.name);
    need(retained.value>=0 && fstat(retained.value,&st)==0 && S_ISBLK(st.st_mode) && st.st_rdev==expected.device && bytes(retained.value)==expected.length*512 &&
        flags>=0 && (flags&O_ACCMODE)==(write?O_RDWR:O_RDONLY),"retained descriptor identity or mode differs");
    need(strcmp(actual.name,expected.name)==0 && strcmp(actual.uuid,expected.uuid)==0 && strcmp(actual.type,expected.type)==0 &&
        strcmp(actual.parameters,expected.parameters)==0 && actual.device==expected.device && actual.start==expected.start && actual.length==expected.length &&
        actual.event==expected.event && actual.flags==expected.flags && actual.opens==opens,"mapper identity, generation or own-only open count differs");
}
Fd open_view(bool exclusive,bool write=true,const char* path=node) {
    Fd result(open(path,(write?O_RDWR:O_RDONLY)|O_CLOEXEC|(exclusive?O_EXCL:0))); need(result.value>=0,"view open refused"); return result;
}
uint64_t milliseconds() { timespec t{}; need(clock_gettime(CLOCK_MONOTONIC,&t)==0,"clock unavailable"); return uint64_t(t.tv_sec)*1000+uint64_t(t.tv_nsec)/1000000; }
int wait_tool(pid_t child) {
    const auto deadline=milliseconds()+300000; int status=0;
    for(;;) {
        const auto result=waitpid(child,&status,WNOHANG);
        if(result==child)return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
        if(result<0&&errno!=EINTR)fail("waitpid failed");
        if(milliseconds()>=deadline) { kill(-child,SIGKILL); kill(child,SIGKILL); while(waitpid(child,&status,0)<0&&errno==EINTR){} return 124; }
        usleep(10000);
    }
}
int tool(const char* executable,const char* const* options,int anchor,const char* log=nullptr) {
    char path[64]; snprintf(path,sizeof(path),"/proc/self/fd/%d",anchor); char* argv[24]{}; unsigned n=0;
    argv[n++]=const_cast<char*>(executable); for(unsigned i=0;options[i];++i) { need(n<22,"too many tool options"); argv[n++]=const_cast<char*>(options[i]); }
    argv[n++]=path; argv[n]=nullptr; printf("URE_FORMAT_TOOL %s",executable); for(unsigned i=1;i<n;++i)printf(" %s",argv[i]); printf("\n"); fflush(nullptr);
    const auto child=fork(); need(child>=0,"cannot fork tool");
    if(child==0) { setpgid(0,0); need(fcntl(anchor,F_SETFD,0)==0,"cannot inherit exact anchor");
        if(log) { Fd output(open(log,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));
            need(output.value>=0 && dup2(output.value,1)==1 && dup2(output.value,2)==2,"cannot capture refusal diagnostic"); }
        execv(executable,argv); _exit(127); }
    setpgid(child,child); return wait_tool(child);
}
void digest_view(char (&out)[65],const char* path=node) {
    auto anchor=open_view(false,false,path); int pipes[2]{}; need(pipe2(pipes,O_CLOEXEC)==0,"digest pipe failed");
    Fd read(pipes[0]),write(pipes[1]); const auto child=fork(); need(child>=0,"cannot fork digest");
    if(child==0) { setpgid(0,0); need(dup2(write.value,1)==1 && fcntl(anchor.value,F_SETFD,0)==0,"cannot inherit digest anchor");
        char path[64]; snprintf(path,sizeof(path),"/proc/self/fd/%d",anchor.value);
        execl("/system/bin/toybox","toybox","sha256sum",path,static_cast<char*>(nullptr)); _exit(127); }
    setpgid(child,child); write=Fd(); need(fcntl(read.value,F_SETFL,O_NONBLOCK)==0,"cannot bound digest reads");
    char output[256]{}; size_t done=0; const auto deadline=milliseconds()+300000;
    for(;;) { if(milliseconds()>=deadline) { kill(-child,SIGKILL); kill(child,SIGKILL); wait_tool(child); fail("digest timed out"); }
        const auto count=::read(read.value,output+done,sizeof(output)-1-done);
        if(count<0&&errno==EINTR)continue;
        if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK)) { usleep(10000); continue; } need(count>=0,"digest read failed");
        if(count==0)break; done+=static_cast<size_t>(count); need(done<sizeof(output)-1,"digest output exceeded bound"); }
    need(wait_tool(child)==0 && done>=65 && output[64]==' ',"digest failed");
    for(unsigned i=0;i<64;++i)need((output[i]>='0'&&output[i]<='9')||(output[i]>='a'&&output[i]<='f'),"invalid digest");
    memcpy(out,output,64); out[64]=0;
}
void fill(int raw,bool sentinels) {
    auto* data=static_cast<unsigned char*>(calloc(1,mib)); need(data,"cannot allocate bounded fill buffer");
    if(sentinels) {
        for(uint64_t offset=0;offset<edge;offset+=mib) { for(uint64_t i=0;i<mib;++i)data[i]=static_cast<unsigned char>((offset+i)*37+11); write_exact(raw,data,mib,offset); }
        for(uint64_t offset=edge+extent;offset<capacity;offset+=mib) { for(uint64_t i=0;i<mib;++i)data[i]=static_cast<unsigned char>((offset+i)*53+17); write_exact(raw,data,mib,offset); }
    } else for(uint64_t offset=edge;offset<edge+extent;offset+=mib)write_exact(raw,data,mib,offset);
    free(data); need(fsync(raw)==0,"fixture fill fsync failed");
}
void sentinels(int raw) {
    auto* data=static_cast<unsigned char*>(malloc(mib)); need(data,"cannot allocate sentinel buffer");
    for(uint64_t offset=0;offset<capacity;offset+=mib) {
        if(offset>=edge && offset<edge+extent)continue; read_exact(raw,data,mib,offset);
        for(uint64_t i=0;i<mib;++i)need(data[i]==static_cast<unsigned char>((offset+i)*(offset<edge?37:53)+(offset<edge?11:17)),"neighboring sentinel byte changed");
    } free(data);
}
void zero_tail(int raw,uint64_t start) {
    auto* data=static_cast<unsigned char*>(malloc(mib)); need(data,"cannot allocate zero-tail buffer");
    for(uint64_t offset=edge+start;offset<edge+extent;offset+=mib) { read_exact(raw,data,mib,offset);
        for(uint64_t i=0;i<mib;++i)need(data[i]==0,"formatter wrote beyond shortened FAT view"); } free(data);
}
uint16_t le16(const unsigned char* p) { return uint16_t(p[0])|(uint16_t(p[1])<<8); }
uint32_t le32(const unsigned char* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
void signature(int fd,const char* fs) {
    unsigned char head[4096]{}; read_exact(fd,head,sizeof(head),0);
    if(strcmp(fs,"f2fs")==0)need(le32(head+1024)==0xf2f52010 && (le32(head+1024+2180)&1),"F2FS encryption feature missing");
    else if(strcmp(fs,"ext4")==0)need(head[1080]==0x53 && head[1081]==0xef,"ext4 signature missing");
    else if(strcmp(fs,"fat32")==0) {
        const uint64_t bps=le16(head+11),spc=head[13],reserved=le16(head+14),fats=head[16],fat_sectors=le32(head+36),total=le32(head+32);
        need(memcmp(head+82,"FAT32   ",8)==0 && bps==4096 && spc>0 && (spc&(spc-1))==0 && reserved>0 && fats>0 &&
            le16(head+17)==0 && le16(head+19)==0 && le16(head+22)==0 && fat_sectors>0 && total==bytes(fd)/bps &&
            total>reserved+fats*fat_sectors,"FAT32 BPB geometry differs");
        const auto clusters=(total-reserved-fats*fat_sectors)/spc; const auto root=le32(head+44);
        printf("URE_FORMAT_FAT_BPB sector_bytes=%llu sectors_per_cluster=%llu clusters=%llu total_sectors=%llu fat_sectors=%llu root_cluster=%u\n",
            static_cast<unsigned long long>(bps),static_cast<unsigned long long>(spc),static_cast<unsigned long long>(clusters),
            static_cast<unsigned long long>(total),static_cast<unsigned long long>(fat_sectors),root);
        need(clusters>=65525 && clusters<0x0ffffff5 && fat_sectors*bps>=(clusters+2)*4 && root>=2 && root<clusters+2,
            "FAT32 cluster count or root range invalid");
    }
    else if(strcmp(fs,"ntfs")==0)need(memcmp(head+3,"NTFS    ",8)==0,"NTFS signature missing");
    else if(strcmp(fs,"btrfs")==0) { read_exact(fd,head,sizeof(head),65536); need(memcmp(head+64,"_BHRfS_M",8)==0,"Btrfs signature missing"); }
    else fail("unknown fixture filesystem");
}
template<class Test> void refusal(const char* name,Test&& test) {
    fflush(nullptr); const auto child=fork(); need(child>=0,"cannot fork refusal fixture");
    if(child==0) { test(); _exit(99); }
    need(wait_tool(child)==37,"expected handoff refusal was not observed"); printf("URE_FORMAT_CHECK %s 0\n",name);
}
void pending() {
    Fd fd(open("/tmp/ure-format-pending",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600)); need(fd.value>=0,"pending marker creation failed");
    write_exact(fd.value,"PENDING\n",8,0); need(fsync(fd.value)==0,"pending marker fsync failed");
}
void mounted_refusals(const Maps& maps,const Table& view,int raw) {
    const char* directory="/tmp/ure-mounted-view";
    need(mkdir(directory,0700)==0,"cannot create disposable mount directory");
    need(mount(node,directory,"ext4",MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC,"noload")==0,"disposable read-only mount failed");
    char before[65]{},after[65]{}; digest_view(before);
    refusal("mounted-view-handoff-refused",[&]{
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl);},[&](const Fd& fd,int count){verify(maps,view,fd,count,true);},
            [&](const Fd&){fail("mounted effect must not execute",99);},[&](const Fd&){fail("mounted readback must not execute",99);}); });
    const char* ext_args[]={"-q","-F","-t","ext4","-L","linux",nullptr};
    const char* ntfs_args[]={"-Q","-L","Windows",nullptr};
    const char* executables[]={"/system/bin/mke2fs","/system/bin/mkfs.ntfs"};
    const char* logs[]={"/tmp/mounted-ext4.log","/tmp/mounted-ntfs.log"};
    const char* const* arguments[]={ext_args,ntfs_args};
    for(unsigned i=0;i<2;++i) {
        { auto anchor=open_view(false); need(tool(executables[i],arguments[i],anchor.value,logs[i])==1,"mounted formatter did not return operational refusal"); }
        FILE* output=fopen(logs[i],"r"); char text[8192]{};
        need(output && fread(text,1,sizeof(text)-1,output)>0 && !ferror(output) && feof(output),"mounted refusal output unavailable or oversized"); fclose(output);
        printf("URE_MOUNTED_TOOL_DIAGNOSTIC %s\n%s",executables[i],text);
        need(strstr(text,"is mounted") && (i==0?strstr(text,"will not make"):strstr(text,"Refusing to make")),"mounted refusal diagnostic differs");
        digest_view(after); need(strcmp(before,after)==0,"mounted formatter refusal changed filesystem bytes"); sentinels(raw);
        printf("URE_FORMAT_CHECK mounted-%s-tool-refused-byte-identical 0\n",i==0?"ext4":"ntfs");
    }
    need(umount(directory)==0 && rmdir(directory)==0,"disposable mount cleanup failed");
    digest_view(after); need(strcmp(before,after)==0,"mount cleanup changed filesystem bytes"); sentinels(raw);
}
void run() {
    need(getuid()==0,"fixture requires isolated guest root");
    FILE* command=fopen("/proc/cmdline","r"); char cmdline[4096]{}; need(command && fgets(cmdline,sizeof(cmdline),command),"guest command line unavailable"); fclose(command);
    need(strstr(cmdline,"ure_formatter_fixture=1")!=nullptr,"disposable guest marker absent");
    struct stat st{}; Fd raw(open("/dev/vda",O_RDWR|O_CLOEXEC)); int sectors=0;
    need(raw.value>=0 && fstat(raw.value,&st)==0 && S_ISBLK(st.st_mode) && bytes(raw.value)==capacity &&
        ioctl(raw.value,BLKSSZGET,&sectors)==0 && sectors==4096,"dedicated 4K fixture disk differs");
    need(access("/sys/class/block/vda/device",F_OK)==0 && access("/dev/vdb",F_OK)!=0,"unexpected fixture block topology");
    Maps maps; const auto view=maps.create("ure-format-fixture","URE-FORMAT-FIXTURE-PRIMARY",st.st_rdev,node);
    const auto foreign=maps.create("ure-format-foreign","URE-FORMAT-FIXTURE-FOREIGN",st.st_rdev,foreign_node);
    fill(raw.value,true); fill(raw.value,false); pending();
    const char* f2fs=access("/system/bin/make_f2fs",X_OK)==0?"/system/bin/make_f2fs":"/system/bin/mkfs.f2fs";
    const char* f2fs_args[]={"-f","-g","android","-t","0","-w","4096","-l","userdata",nullptr};
    char original[65]{},after[65]{}; digest_view(original);
    { auto old_claim=open_view(true); auto anchor=open_view(false);
        need(tool(f2fs,f2fs_args,anchor.value)==255,"old retained exclusive claim did not produce pinned F2FS operational refusal"); }
    digest_view(after); need(strcmp(original,after)==0,"old-claim refusal changed view bytes"); sentinels(raw.value);
    printf("URE_FORMAT_CHECK old-exclusive-real-tool-refused-byte-identical 0\n");
    { auto held=open_view(true); refusal("foreign-exclusive-claim-refused",[&]{
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl);},[&](const Fd& fd,int count){verify(maps,view,fd,count,true);},
            [&](const Fd&){fail("effect must not execute",99);},[&](const Fd&){fail("readback must not execute",99);}); }); }
    { auto held=open_view(false); refusal("foreign-nonexclusive-open-count-refused",[&]{
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl);},[&](const Fd& fd,int count){verify(maps,view,fd,count,true);},
            [&](const Fd&){fail("effect must not execute",99);},[&](const Fd&){fail("readback must not execute",99);}); }); }
    refusal("foreign-mapper-descriptor-refused",[&]{
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl,true,excl?node:foreign_node);},
            [&](const Fd& fd,int count){verify(maps,view,fd,count,true);},[&](const Fd&){fail("effect must not execute",99);},[&](const Fd&){}); });
    refusal("mapper-UUID-mismatch-refused",[&]{ auto changed=view; strcpy(changed.uuid,"URE-UNRELATED-UUID");
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl);},[&](const Fd& fd,int count){verify(maps,changed,fd,count,true);},
            [&](const Fd&){fail("effect must not execute",99);},[&](const Fd&){}); });
    refusal("failed-formatter-stops-sequence",[&]{
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl);},[&](const Fd& fd,int count){verify(maps,view,fd,count,true);},
            [&](const Fd& fd){const char* options[]={nullptr}; const auto status=tool("/system/bin/false",options,fd.value);
                if(status!=1)fail("false tool did not return its expected failure status",99); fail("intentional formatter failure");},
            [&](const Fd&){fail("readback must not execute",99);}); });
    { Fd marker(open("/tmp/ure-format-pending",O_RDONLY|O_CLOEXEC)); char text[8]{}; read_exact(marker.value,text,8,0); need(memcmp(text,"PENDING\n",8)==0,"failure cleared pending marker"); }
    digest_view(after); need(strcmp(original,after)==0,"refusal fixtures changed view bytes"); sentinels(raw.value);
    const auto fat300=maps.create("ure-format-fat300","URE-FORMAT-FIXTURE-FAT300",st.st_rdev,fat_node,300*mib);
    struct Format { const char* fs; const char* mkfs; const char* const* mkargs; const char* checker; const char* const* checkargs; };
    const char* f2fs_check[]={"--dry-run",nullptr}; const char* ext_args[]={"-q","-F","-t","ext4","-L","linux",nullptr}; const char* ext_check[]={"-f","-n",nullptr};
    const char* fat_args[]={"-F","32","-s","1","-n","ESP",nullptr}; const char* fat_check[]={"-n",nullptr};
    const char* ntfs_args[]={"-Q","-L","Windows",nullptr}; const char* ntfs_check[]={"-n",nullptr};
    const char* btrfs_args[]={"-f","-L","linux",nullptr}; const char* btrfs_check[]={"check","--readonly",nullptr};
    const Format formats[]={{"f2fs",f2fs,f2fs_args,"/system/bin/fsck.f2fs",f2fs_check},
        {"ext4","/system/bin/mke2fs",ext_args,"/system/bin/e2fsck",ext_check},
        {"fat32","/system/bin/mkfs.fat",fat_args,"/system/bin/fsck.fat",fat_check},
        {"ntfs","/system/bin/mkfs.ntfs",ntfs_args,"/system/bin/fsck.ntfs",ntfs_check},
        {"btrfs","/system/bin/mkfs.btrfs",btrfs_args,"/system/bin/btrfs",btrfs_check}};
    bool btrfs=false;
    for(const auto& format:formats) {
        if(strcmp(format.fs,"btrfs")==0 && (access(format.mkfs,X_OK)!=0 || access(format.checker,X_OK)!=0)) { printf("URE_FORMAT_SKIP btrfs-tools-not-packaged\n"); continue; }
        need(access(format.mkfs,X_OK)==0 && access(format.checker,X_OK)==0,"required fixture tool missing"); fill(raw.value,false);
        const bool is_fat=strcmp(format.fs,"fat32")==0; const auto& selected=is_fat?fat300:view; const auto* path=is_fat?fat_node:node;
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl,true,path);},[&](const Fd& fd,int count){verify(maps,selected,fd,count,true);},
            [&](const Fd& fd){need(tool(format.mkfs,format.mkargs,fd.value)==0,"actual formatter failed"); need(fsync(fd.value)==0,"formatted view fsync failed");},
            [&](const Fd& fd){signature(fd.value,format.fs);});
        sentinels(raw.value); if(is_fat)zero_tail(raw.value,300*mib); char before_check[65]{},after_check[65]{}; digest_view(before_check,path);
        ure::dualboot_view_handoff([&](bool excl){return open_view(excl,false,path);},[&](const Fd& fd,int count){verify(maps,selected,fd,count,false);},
            [&](const Fd& fd){need(tool(format.checker,format.checkargs,fd.value)==0,"actual read-only checker failed");},
            [&](const Fd& fd){signature(fd.value,format.fs);});
        digest_view(after_check,path); need(strcmp(before_check,after_check)==0,"read-only checker changed filesystem bytes"); sentinels(raw.value); if(is_fat)zero_tail(raw.value,300*mib);
        printf("URE_FORMAT_CHECK %s-format-readonly-checker-bounded-neighbors 0\n",format.fs);
        if(strcmp(format.fs,"ext4")==0)mounted_refusals(maps,view,raw.value);
        if(strcmp(format.fs,"btrfs")==0)btrfs=true;
    }
    maps.remove(fat300,fat_node); maps.remove(foreign,foreign_node); maps.remove(view,node); sentinels(raw.value);
    printf("URE_FORMAT_HANDOFF_RESULT {\"schema_version\":1,\"passed\":true,\"validation_kind\":\"generic-kernel-bionic-dm-view-handoff\",\"logical_sector_bytes\":4096,\"fat32_view_bytes\":314572800,\"fat32_cluster_geometry_verified\":true,\"bounded_view_bytes\":%llu,\"neighbor_bytes_verified\":%llu,\"f2fs_android_feature\":true,\"ext4\":true,\"fat32\":true,\"ntfs\":true,\"btrfs\":%s,\"read_only_checker_anchor\":true,\"read_only_checker_sha256_unchanged\":true,\"old_exclusive_refusal_byte_identical\":true,\"foreign_claim_refused\":true,\"foreign_open_count_refused\":true,\"foreign_mapper_refused\":true,\"UUID_mismatch_refused\":true,\"failed_formatter_pending_marker_retained\":true,\"gpt_writer_present\":false,\"production_quarantine_test\":false,\"shipping_cli_positive_execute\":false,\"encrypted_userdata\":false,\"physical_device\":false}\n",
        static_cast<unsigned long long>(extent),static_cast<unsigned long long>(2*edge),btrfs?"true":"false");
}
}
int main(int argc,char**) { need(argc==1,"fixture accepts no device arguments"); run(); return 0; }
