// SPDX-License-Identifier: Apache-2.0
// Regular-image range digests and private Android sparse expansion. A kernel
// hole describes logical zero bytes; it never authorizes skipped UFS writes.
#include "uke.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <openssl/evp.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <zlib.h>

namespace ure {
namespace {
constexpr std::uint64_t leaf_bytes=4*1024*1024,maximum=512ULL*1024*1024*1024;
struct Digest {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> value{EVP_MD_CTX_new(),EVP_MD_CTX_free};
    Digest() { require(value && EVP_DigestInit_ex(value.get(),EVP_sha256(),nullptr)==1,"hash-error","Cannot initialize image digest"); }
    void add(std::string_view data) { require(EVP_DigestUpdate(value.get(),data.data(),data.size())==1,"hash-error","Cannot hash image bytes"); }
    std::string finish() { std::array<unsigned char,32> bytes{}; unsigned count=0;
        require(EVP_DigestFinal_ex(value.get(),bytes.data(),&count)==1 && count==bytes.size(),"hash-error","Cannot finish image digest");
        constexpr char hex[]="0123456789abcdef"; std::string out; for(const auto byte:bytes) { out+=hex[byte>>4]; out+=hex[byte&15]; } return out; }
};
void write(int fd,std::uint64_t offset,std::string_view bytes) {
    while(!bytes.empty()) { const auto written=::pwrite(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset));
        if(written<0 && errno==EINTR)continue;
        require(written>0,"io-error","Cannot write private image bytes"); offset+=static_cast<std::uint64_t>(written); bytes.remove_prefix(static_cast<std::size_t>(written)); }
}
std::uint64_t regular(int fd) { struct stat st{};
    require(::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size>=0,"invalid-image","Image helpers require a regular file"); return static_cast<std::uint64_t>(st.st_size); }
void bounds(int fd,std::uint64_t offset,std::uint64_t bytes) {
    const auto capacity=regular(fd); require(bytes>0 && bytes<=maximum && offset<=capacity && bytes<=capacity-offset,"invalid-image-range","Image range exceeds its regular file");
}
struct Position {
    int fd; off_t old;
    explicit Position(int descriptor) : fd(descriptor),old(::lseek(fd,0,SEEK_CUR)) { require(old>=0,"io-error","Image descriptor is not seekable"); }
    ~Position() { ::lseek(fd,old,SEEK_SET); }
};
bool hole(int fd,std::uint64_t offset,std::uint64_t bytes) {
    const auto next=::lseek(fd,static_cast<off_t>(offset),SEEK_DATA);
    if(next<0) { if(errno==ENXIO)return true; if(errno==EINVAL || errno==ENOTSUP)return false; throw Error("io-error","Cannot inspect image data extents"); }
    require(static_cast<std::uint64_t>(next)>=offset,"io-error","Kernel image data extent moved backward");
    return static_cast<std::uint64_t>(next)>=offset+bytes;
}
std::string number(std::uint64_t value) { std::string out(8,'\0'); for(unsigned i=0;i<8;++i)out[i]=static_cast<char>((value>>(8*i))&255); return out; }
std::string zeros(std::uint64_t bytes) { Digest digest; const std::string zero(65536,'\0');
    while(bytes) { const auto amount=static_cast<std::size_t>(std::min<std::uint64_t>(bytes,zero.size())); digest.add(std::string_view(zero).substr(0,amount)); bytes-=amount; } return digest.finish(); }
// Hash the decoded logical stream before a journal exists. This binds the
// eventual replacement to the reviewed source, including sparse zero policy.
class LogicalTree {
    Digest tree_;
    std::unique_ptr<Digest> leaf_;
    std::uint64_t total_,position_=0,in_leaf_=0;
    std::map<std::string,std::string> repeated_;
    std::uint64_t next_size() const { return std::min(leaf_bytes,total_-position_); }
    void append(const std::string& hash,std::uint64_t bytes) {
        tree_.add(number(position_)); tree_.add(number(bytes)); tree_.add(hash); position_+=bytes;
    }
public:
    explicit LogicalTree(std::uint64_t total) : total_(total) { tree_.add("ure-image-range-sha256-tree-v1"); tree_.add(number(total)); }
    void add(std::string_view data) {
        while(!data.empty()) {
            require(position_<total_ && data.size()<=total_-position_-in_leaf_,"invalid-sparse-image","Decoded stream exceeds its logical size");
            if(!leaf_)leaf_=std::make_unique<Digest>();
            const auto bytes=static_cast<std::size_t>(std::min<std::uint64_t>(data.size(),next_size()-in_leaf_));
            leaf_->add(data.substr(0,bytes)); in_leaf_+=bytes; data.remove_prefix(bytes);
            if(in_leaf_==next_size()) { const auto size=in_leaf_; append(leaf_->finish(),size); leaf_.reset(); in_leaf_=0; }
        }
    }
    void repeat(std::string_view pattern,std::uint64_t bytes) {
        require(!pattern.empty() && bytes%pattern.size()==0 && position_<=total_ && in_leaf_<=total_-position_ && bytes<=total_-position_-in_leaf_,
            "invalid-sparse-image","Decoded fill exceeds its logical size");
        std::string buffer(65536,'\0'); for(std::size_t i=0;i<buffer.size();++i)buffer[i]=pattern[i%pattern.size()];
        while(bytes) {
            const auto size=next_size();
            if(in_leaf_==0 && bytes>=size) {
                const auto key=std::string(pattern)+number(size); auto found=repeated_.find(key);
                if(found==repeated_.end()) {
                    Digest digest; for(std::uint64_t at=0;at<size;) { const auto amount=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),size-at)); digest.add(std::string_view(buffer).substr(0,amount)); at+=amount; }
                    if(repeated_.size()>=8)repeated_.erase(repeated_.begin());
                    found=repeated_.emplace(key,digest.finish()).first;
                }
                append(found->second,size); bytes-=size;
            } else { const auto amount=static_cast<std::size_t>(std::min<std::uint64_t>({buffer.size(),bytes,size-in_leaf_})); add(std::string_view(buffer).substr(0,amount)); bytes-=amount; }
        }
    }
    std::string finish() { require(position_==total_ && !leaf_ && in_leaf_==0,"invalid-sparse-image","Decoded logical digest is incomplete"); return tree_.finish(); }
};
void fresh(int fd,std::uint64_t bytes) { struct stat st{};
    require(::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size==0 && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 &&
        bytes>0 && bytes<=maximum && ::ftruncate(fd,static_cast<off_t>(bytes))==0,"unsafe-stage-image","Destination must be a fresh private single-link image"); }
std::uint64_t le(std::string_view data,std::size_t offset,unsigned count) {
    require(offset<=data.size() && count<=8 && count<=data.size()-offset,"invalid-sparse-image","Sparse integer exceeds its input");
    std::uint64_t value=0; for(unsigned i=0;i<count;++i)value|=static_cast<std::uint64_t>(static_cast<unsigned char>(data[offset+i]))<<(i*8); return value;
}
uLong repeat_crc(uLong current,std::string_view pattern,std::uint64_t bytes) {
    require(!pattern.empty() && bytes%pattern.size()==0,"invalid-sparse-image","Sparse fill is not aligned to its pattern");
    auto repeat=bytes/pattern.size(); std::uint64_t length=pattern.size();
    uLong block=::crc32(0,reinterpret_cast<const Bytef*>(pattern.data()),static_cast<uInt>(pattern.size()));
    while(repeat) { if(repeat&1U)current=::crc32_combine(current,block,static_cast<z_off_t>(length)); repeat>>=1;
        if(repeat) { block=::crc32_combine(block,block,static_cast<z_off_t>(length)); length*=2; } }
    return current;
}
Value sparse(int source,int destination,bool zero_holes) {
    const auto source_bytes=regular(source); require(source_bytes>=28 && source_bytes<=maximum,"invalid-sparse-image","Sparse input size is invalid");
    const auto header=storage_read(source,0,28);
    const auto file_header=le(header,8,2),chunk_header=le(header,10,2),block=le(header,12,4),blocks=le(header,16,4),chunks=le(header,20,4),checksum=le(header,24,4);
    require(le(header,0,4)==0xed26ff3aU && le(header,4,2)==1 && le(header,6,2)==0 && file_header>=28 && file_header<=4096 && chunk_header>=12 && chunk_header<=4096 &&
        block>=512 && block<=1024*1024 && block%512==0 && blocks>0 && blocks<=maximum/block && chunks>0 && chunks<=262144 && file_header<=source_bytes,
        "invalid-sparse-image","Unsupported or overflowing Android sparse header");
    const auto expanded=blocks*block; LogicalTree logical(expanded); if(destination>=0)fresh(destination,expanded);
    std::uint64_t input=file_header,output=0,discarded=0; uLong crc=0; unsigned crc_chunks=0;
    for(std::uint64_t index=0;index<chunks;++index) {
        require(input<=source_bytes && chunk_header<=source_bytes-input,"invalid-sparse-image","Truncated sparse chunk header");
        const auto header_bytes=storage_read(source,input,static_cast<std::size_t>(chunk_header)); const auto type=le(header_bytes,0,2),count=le(header_bytes,4,4),total=le(header_bytes,8,4);
        require(le(header_bytes,2,2)==0 && total>=chunk_header && total<=source_bytes-input && count<=(expanded-output)/block,
            "invalid-sparse-image","Sparse chunk length or output boundary is invalid");
        const auto bytes=count*block; input+=chunk_header;
        if(type==0xcac1) {
            require(bytes>0 && total-chunk_header==bytes,"invalid-sparse-image","RAW sparse chunk length differs");
            for(std::uint64_t at=0;at<bytes;) { const auto data=storage_read(source,input+at,static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-at)));
                crc=::crc32(crc,reinterpret_cast<const Bytef*>(data.data()),static_cast<uInt>(data.size()));
                logical.add(data);
                if(destination>=0 && std::any_of(data.begin(),data.end(),[](char byte) { return byte!=0; }))write(destination,output+at,data);
                at+=data.size(); }
        } else if(type==0xcac2) {
            require(bytes>0 && total-chunk_header==4,"invalid-sparse-image","FILL sparse chunk length differs"); const auto pattern=storage_read(source,input,4);
            crc=repeat_crc(crc,pattern,bytes);
            logical.repeat(pattern,bytes);
            if(destination>=0 && pattern!=std::string(4,'\0')) { std::string data(65536,'\0'); for(std::size_t i=0;i<data.size();++i)data[i]=pattern[i%4];
                for(std::uint64_t at=0;at<bytes;) { const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(data.size(),bytes-at)); write(destination,output+at,std::string_view(data).substr(0,size)); at+=size; } }
        } else if(type==0xcac3) {
            require(bytes>0 && total==chunk_header,"invalid-sparse-image","DONT_CARE sparse chunk has an unexpected payload");
            require(destination<0 || zero_holes,"sparse-zero-policy-required","Review zero filling of Android sparse DONT_CARE ranges explicitly");
            crc=repeat_crc(crc,std::string_view("\0",1),bytes); discarded+=bytes;
            logical.repeat(std::string_view("\0",1),bytes);
        } else if(type==0xcac4) {
            require(count==0 && total-chunk_header==4 && le(storage_read(source,input,4),0,4)==crc,"invalid-sparse-image","Sparse CRC chunk differs from expanded data"); ++crc_chunks;
        } else throw Error("invalid-sparse-image","Unknown Android sparse chunk type");
        input+=total-chunk_header; output+=bytes;
    }
    require(input==source_bytes && output==expanded && (checksum==0 || checksum==crc),"invalid-sparse-image","Sparse total blocks, trailing bytes or checksum differ");
    if(destination>=0)require(::fsync(destination)==0,"io-error","Cannot synchronize expanded stock image");
    Value result; result["encoding"]="android-sparse-v1"; result["source_bytes"]=Json::UInt64(source_bytes); result["expanded_bytes"]=Json::UInt64(expanded);
    result["chunks"]=Json::UInt64(chunks); result["dont_care_bytes"]=Json::UInt64(discarded); result["crc32"]=Json::UInt(crc); result["crc_chunks"]=crc_chunks;
    result["expanded_digest_algorithm"]="ure-image-range-sha256-tree-v1"; result["expanded_digest"]=logical.finish();
    result["structure_verified"]=true; result["declared_checksums_verified"]=true; result["physical_test_record"]=false; return result;
}
} // namespace

std::string storage_image_range_digest(int fd,std::uint64_t offset,std::uint64_t bytes) {
    bounds(fd,offset,bytes); Position position(fd); Digest tree; tree.add("ure-image-range-sha256-tree-v1"); tree.add(number(bytes));
    std::map<std::uint64_t,std::string> zero_leaves;
    for(std::uint64_t at=0;at<bytes;) {
        const auto count=std::min<std::uint64_t>(leaf_bytes,bytes-at); std::string hash;
        if(hole(fd,offset+at,count)) { auto found=zero_leaves.find(count);
            if(found==zero_leaves.end())found=zero_leaves.emplace(count,zeros(count)).first;
            hash=found->second;
        } else { Digest leaf;
            for(std::uint64_t read=0;read<count;) { const auto data=storage_read(fd,offset+at+read,static_cast<std::size_t>(std::min<std::uint64_t>(65536,count-read))); leaf.add(data); read+=data.size(); }
            hash=leaf.finish();
        }
        // Allocation is absent: allocated zeros and kernel-reported holes
        // produce the same ordinary SHA-256 leaf and canonical tree root.
        tree.add(number(at)); tree.add(number(count)); tree.add(hash); at+=count;
    }
    return tree.finish();
}
void storage_copy_image_range(int source,int destination,std::uint64_t offset,std::uint64_t bytes) {
    bounds(source,offset,bytes); fresh(destination,bytes); Position position(source);
    struct file_clone_range clone{}; clone.src_fd=source; clone.src_offset=offset; clone.src_length=bytes;
    if(::ioctl(destination,FICLONERANGE,&clone)!=0) {
        require(::ftruncate(destination,0)==0 && ::ftruncate(destination,static_cast<off_t>(bytes))==0,"io-error","Cannot reset failed private clone");
        for(std::uint64_t at=0;at<bytes;) { const auto count=std::min<std::uint64_t>(leaf_bytes,bytes-at);
            if(!hole(source,offset+at,count))for(std::uint64_t read=0;read<count;) {
                const auto data=storage_read(source,offset+at+read,static_cast<std::size_t>(std::min<std::uint64_t>(65536,count-read)));
                if(std::any_of(data.begin(),data.end(),[](char byte) { return byte!=0; }))write(destination,at+read,data);
                read+=data.size();
            }
            at+=count;
        }
    }
    require(::fsync(destination)==0,"io-error","Cannot synchronize private image range");
}
Value stock_image_inspect(int source) {
    const auto bytes=regular(source); require(bytes>0 && bytes<=maximum,"invalid-image","Stock image size is invalid");
    if(bytes>=4 && le(storage_read(source,0,4),0,4)==0xed26ff3aU)return sparse(source,-1,false);
    require(bytes%4096==0,"invalid-image","Raw stock image must align to its programming sectors");
    Value result; result["encoding"]="raw"; result["source_bytes"]=Json::UInt64(bytes); result["expanded_bytes"]=Json::UInt64(bytes); result["dont_care_bytes"]=Json::UInt64(0);
    result["expanded_digest_algorithm"]="ure-image-range-sha256-tree-v1"; result["expanded_digest"]=storage_image_range_digest(source,0,bytes);
    result["structure_verified"]=true; result["physical_test_record"]=false; return result;
}
Value stock_image_expand(int source,int destination,bool zero_holes) {
    const auto bytes=regular(source); require(bytes>0 && bytes<=maximum,"invalid-image","Stock image size is invalid");
    if(bytes>=4 && le(storage_read(source,0,4),0,4)==0xed26ff3aU)return sparse(source,destination,zero_holes);
    auto result=stock_image_inspect(source); storage_copy_image_range(source,destination,0,bytes); return result;
}
} // namespace ure
