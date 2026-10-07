// SPDX-License-Identifier: Apache-2.0
// Host-only controls for the actual liblp reader/utility translation units.
// The opener admits only ordinary read-only fixture files. No device is opened.
#include <liblp/liblp.h>
#include <liblp/partition_opener.h>
#include <android-base/logging.h>
#include <ext4_utils/ext4_utils.h>
#include "utility.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace lp = android::fs_mgr;
namespace fs = std::filesystem;
namespace {
[[noreturn]] void fail(const char* reason) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); }
void check(bool value,const char* reason) { if(!value)fail(reason); }
class ReadOnlyFixture final : public lp::IPartitionOpener {
 public:
    explicit ReadOnlyFixture(std::string path):path_(std::move(path)) {}
    android::base::unique_fd Open(const std::string& path,int flags) const override {
        check(path==path_ && flags==O_RDONLY,"Non-fixture or writable open attempted");
        android::base::unique_fd fd(open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
        struct stat st{};
        check(fd.get()>=0 && fstat(fd.get(),&st)==0 && S_ISREG(st.st_mode),"Expected a regular fixture");
        return fd;
    }
    bool GetInfo(const std::string&,lp::BlockDeviceInfo*) const override { fail("Unexpected device geometry request"); }
    std::string GetDeviceString(const std::string&) const override { fail("Unexpected device mapping request"); }
 private:
    std::string path_;
};
template<class T> void append(std::vector<std::uint8_t>& bytes,const T& object) {
    const auto* p=reinterpret_cast<const std::uint8_t*>(&object); bytes.insert(bytes.end(),p,p+sizeof(object));
}
std::vector<std::uint8_t> fixture(unsigned suffix_kind=0) {
    static_assert(__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__);
    std::vector<std::uint8_t> image(1024*1024,0),tables;
    LpMetadataPartition partition{};
    std::strcpy(partition.name,suffix_kind ? "linux" : "linux_a");
    partition.attributes=LP_PARTITION_ATTR_READONLY;
    if(suffix_kind==1)partition.attributes|=LP_PARTITION_ATTR_SLOT_SUFFIXED;
    partition.num_extents=1;
    LpMetadataExtent extent{}; extent.num_sectors=4096; extent.target_type=LP_TARGET_TYPE_LINEAR; extent.target_data=2048;
    LpMetadataPartitionGroup group{}; std::strcpy(group.name,"default");
    if(suffix_kind==2)group.flags=LP_GROUP_SLOT_SUFFIXED;
    LpMetadataBlockDevice device{};
    device.first_logical_sector=2048; device.alignment=4096; device.size=128*1024*1024; std::strcpy(device.partition_name,"super");
    if(suffix_kind==3)device.flags=LP_BLOCK_DEVICE_SLOT_SUFFIXED;
    LpMetadataHeader header{};
    header.magic=LP_METADATA_HEADER_MAGIC; header.major_version=LP_METADATA_MAJOR_VERSION;
    header.minor_version=LP_METADATA_MINOR_VERSION_MAX; header.header_size=sizeof(header);
    header.flags=LP_HEADER_FLAG_VIRTUAL_AB_DEVICE;
    header.partitions={0,1,sizeof(partition)}; append(tables,partition);
    header.extents={static_cast<std::uint32_t>(tables.size()),1,sizeof(extent)}; append(tables,extent);
    header.groups={static_cast<std::uint32_t>(tables.size()),1,sizeof(group)}; append(tables,group);
    header.block_devices={static_cast<std::uint32_t>(tables.size()),1,sizeof(device)}; append(tables,device);
    header.tables_size=static_cast<std::uint32_t>(tables.size());
    lp::SHA256(tables.data(),tables.size(),header.tables_checksum);
    lp::SHA256(&header,sizeof(header),header.header_checksum);
    LpMetadataGeometry geometry{};
    geometry.magic=LP_METADATA_GEOMETRY_MAGIC; geometry.struct_size=sizeof(geometry);
    geometry.metadata_max_size=65536; geometry.metadata_slot_count=3; geometry.logical_block_size=4096;
    lp::SHA256(&geometry,sizeof(geometry),geometry.checksum);
    std::memcpy(image.data()+4096,&geometry,sizeof(geometry));
    std::memcpy(image.data()+8192,&geometry,sizeof(geometry));
    for(unsigned slot=0;slot<3;++slot)for(unsigned copy=0;copy<2;++copy) {
        const auto start=12288+slot*65536+copy*3*65536;
        std::memcpy(image.data()+start,&header,sizeof(header));
        std::memcpy(image.data()+start+sizeof(header),tables.data(),tables.size());
    }
    return image;
}
unsigned controls=0;
void control(const fs::path& root,const char* name,const std::vector<std::uint8_t>& image,
             std::uint32_t slot,bool accepted,const std::string& partition_name={}) {
    const auto path=root/(std::string(name)+".img");
    const int raw=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    check(raw>=0,"Cannot create private fixture");
    android::base::unique_fd output(raw);
    std::size_t done=0;
    while(done<image.size()) {
        const auto size=write(output.get(),image.data()+done,image.size()-done);
        if(size<0 && errno==EINTR)continue;
        check(size>0,"Fixture write failed"); done+=static_cast<std::size_t>(size);
    }
    output.reset();
    ReadOnlyFixture opener(path.string());
    auto metadata=lp::ReadMetadata(opener,path.string(),slot);
    check(static_cast<bool>(metadata)==accepted,"Unexpected metadata acceptance");
    if(accepted)check(metadata->partitions.size()==1 && lp::GetPartitionName(metadata->partitions[0])==partition_name,"Unexpected implicit slot mapping");
    ++controls; std::printf("PASS: %s\n",name); std::fflush(stdout);
}
} // namespace

// These functions are deliberately unavailable. The real reader's fixture
// opener is injected above; its unrelated physical-device paths are not tested.
extern "C" u64 get_block_device_size(int) { fail("Block-device helper attempted"); }
namespace android::fs_mgr {
android::base::unique_fd PartitionOpener::Open(const std::string&,int) const { fail("Default physical opener attempted"); }
bool PartitionOpener::GetInfo(const std::string&,BlockDeviceInfo*) const { fail("Default physical geometry attempted"); }
std::string PartitionOpener::GetDeviceString(const std::string&) const { fail("Default physical mapping attempted"); }
} // namespace android::fs_mgr

int main(int argc,char** argv) {
    check(argc==2 || argc==3,"Pass a new private fixture directory and optional copied metadata file");
    android::base::SetMinimumLogSeverity(android::base::FATAL);
    const fs::path root(argv[1]);
    check(fs::is_directory(root) && !fs::is_symlink(root) && fs::is_empty(root),"Fixture directory must be new and empty");
    auto image=fixture();
    control(root,"ordinary-record-0",image,0,true,"linux_a");
    control(root,"ordinary-record-1",image,1,true,"linux_a");
    control(root,"ordinary-record-2",image,2,true,"linux_a");
    control(root,"physical-suffix-a",fixture(1),0,true,"linux_a");
    control(root,"physical-suffix-b",fixture(1),1,true,"linux_b");
    for(unsigned kind=1;kind<=3;++kind)control(root,("ambiguous-extra-record-"+std::to_string(kind)).c_str(),fixture(kind),2,false);
    control(root,"record-out-of-range",image,3,false);
    control(root,"record-integer-limit",image,std::numeric_limits<std::uint32_t>::max(),false);
    auto damaged=image; damaged[12288+2*65536+12]^=1;
    control(root,"valid-backup-record-2",damaged,2,true,"linux_a");
    damaged[12288+5*65536+12]^=1;
    control(root,"both-record-checksums-invalid",damaged,2,false);
    damaged=image; damaged[4096+8]^=1;
    control(root,"valid-backup-geometry",damaged,2,true,"linux_a");
    damaged[8192+8]^=1;
    control(root,"both-geometry-checksums-invalid",damaged,2,false);
    check(controls==14,"Control count changed");
    if(argc==3) {
        ReadOnlyFixture opener(argv[2]);
        for(unsigned slot=0;slot<3;++slot) {
            const auto metadata=lp::ReadMetadata(opener,argv[2],slot);
            check(static_cast<bool>(metadata),"Copied metadata record refused");
            std::printf("PASS: copied metadata record %u (%zu partitions)\n",slot,metadata->partitions.size());
        }
    }
    std::puts("PASS: 14 liblp host controls; no block-device access or target writes.");
}
