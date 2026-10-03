// SPDX-License-Identifier: Apache-2.0
// Logical-byte oracles and hostile Android sparse fixtures; regular files only.
#include "uke.h"
#include "stock_payloads.h"
#include <algorithm>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {
constexpr std::uint64_t leaf=4*1024*1024;
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal: "+error.code+", expected "+code); return; }
    throw std::runtime_error("Missing refusal: "+code);
}
void put(std::string& data,std::size_t offset,std::uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)data.at(offset+i)=static_cast<char>((value>>(8*i))&255);
}
std::string number(std::uint64_t value) { std::string out(8,'\0'); put(out,0,value,8); return out; }
std::uint32_t crc(std::string_view data) { std::uint32_t out=UINT32_MAX;
    for(const auto byte:data) { out^=static_cast<unsigned char>(byte); for(unsigned i=0;i<8;++i)out=(out>>1)^((out&1U) ? 0xedb88320U : 0U); } return out^UINT32_MAX;
}
void put_bytes(int fd,std::string_view data,std::uint64_t offset=0) {
    check(::pwrite(fd,data.data(),data.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(data.size()),"Cannot write fixture bytes");
}
ure::Fd fresh(const ure::fs::path& path) {
    ure::Fd fd(::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600)); check(fd.get()>=0,"Cannot create fixture"); return fd;
}
std::string naive_tree(std::string_view data) {
    std::string out="ure-image-range-sha256-tree-v1"; out+=number(data.size());
    for(std::size_t at=0;at<data.size();) { const auto size=std::min<std::size_t>(leaf,data.size()-at); out+=number(at)+number(size)+ure::sha256(data.substr(at,size)); at+=size; }
    return ure::sha256(out);
}
std::pair<std::string,std::string> sparse_fixture(unsigned zero_blocks=1,unsigned fill_blocks=2) {
    std::string file(28,'\0'),expanded(512,'R'); expanded+=std::string(512ULL*zero_blocks,'\0');
    for(std::uint64_t i=0;i<128ULL*fill_blocks;++i)expanded+="ABCD";
    put(file,0,0xed26ff3aU,4); put(file,4,1,2); put(file,8,28,2); put(file,10,12,2);
    put(file,12,512,4); put(file,16,1ULL+zero_blocks+fill_blocks,4); put(file,20,4,4); put(file,24,crc(expanded),4);
    auto chunk=[&](unsigned type,unsigned blocks,std::string_view payload) { std::string header(12,'\0'); put(header,0,type,2); put(header,4,blocks,4); put(header,8,12+payload.size(),4); file+=header; file+=payload; };
    chunk(0xcac1,1,std::string(512,'R')); chunk(0xcac3,zero_blocks,{}); chunk(0xcac2,fill_blocks,"ABCD");
    auto checksum=number(crc(expanded)); checksum.resize(4); chunk(0xcac4,0,checksum); return {file,expanded};
}
}
int main(int argc,char** argv) {
    ure::fs::path work;
    try {
        char name[]="/tmp/ure-image-ranges-XXXXXX"; const auto created=::mkdtemp(name); check(created!=nullptr,"Cannot create test workspace"); work=created;
        auto holes=fresh(work/"holes.img"),allocated=fresh(work/"allocated.img");
        std::string content(2*leaf+12345,'\0'); content.replace(17,10,"first-data"); content.replace(leaf+43,9,"last-data");
        check(::ftruncate(holes.get(),static_cast<off_t>(content.size()))==0,"Cannot size hole image"); put_bytes(holes.get(),"first-data",17); put_bytes(holes.get(),"last-data",leaf+43); put_bytes(allocated.get(),content);
        check(ure::sha256(holes.get())==ure::sha256(allocated.get()),"Ordinary SHA-256 differs for equivalent logical content");
        check(::lseek(holes.get(),37,SEEK_SET)==37,"Cannot set descriptor position");
        for(const auto offset:{0U,1U,4097U}) {
            const auto bytes=content.size()-offset; const auto expected=naive_tree(std::string_view(content).substr(offset));
            check(ure::storage_image_range_digest(holes.get(),offset,bytes)==expected && ure::storage_image_range_digest(allocated.get(),offset,bytes)==expected,"Canonical image tree differs from logical-byte oracle");
            auto copy=fresh(work/("copy-"+std::to_string(offset))); ure::storage_copy_image_range(holes.get(),copy.get(),offset,bytes);
            check(ure::sha256(copy.get())==ure::sha256(std::string_view(content).substr(offset)),"Private image copy differs from original logical bytes");
        }
        check(::lseek(holes.get(),0,SEEK_CUR)==37,"Image helper changed descriptor position");
        const auto before=ure::sha256(holes.get());
        reject([&]{ure::storage_image_range_digest(holes.get(),0,0);},"invalid-image-range");
        reject([&]{ure::storage_image_range_digest(holes.get(),content.size(),1);},"invalid-image-range");
        reject([&]{ure::storage_image_range_digest(holes.get(),UINT64_MAX,1);},"invalid-image-range");
        reject([&]{ure::storage_copy_image_range(holes.get(),allocated.get(),0,4096);},"unsafe-stage-image");
        auto public_file=fresh(work/"public.img"); check(::fchmod(public_file.get(),0644)==0,"Cannot change fixture mode");
        reject([&]{ure::storage_copy_image_range(holes.get(),public_file.get(),0,4096);},"unsafe-stage-image");
        auto linked=fresh(work/"linked.img"); check(::link((work/"linked.img").c_str(),(work/"alias.img").c_str())==0,"Cannot create hardlink");
        reject([&]{ure::storage_copy_image_range(holes.get(),linked.get(),0,4096);},"unsafe-stage-image");
        ure::Fd device(::open("/dev/zero",O_RDONLY)); reject([&]{ure::storage_image_range_digest(device.get(),0,4096);},"invalid-image");
        const auto [file,expanded]=sparse_fixture(); auto source=fresh(work/"source.sparse"); put_bytes(source.get(),file);
        const auto info=ure::stock_image_inspect(source.get()); check(info["expanded_bytes"].asUInt64()==expanded.size() && info["dont_care_bytes"].asUInt64()==512 && info["crc_chunks"].asUInt()==1,"Sparse inspection lost reviewed geometry/checksum");
        check(info["expanded_digest"].asString()==naive_tree(expanded),"Reviewed decoded sparse digest differs from byte oracle");
        auto missing_policy=fresh(work/"missing-policy.img"); reject([&]{ure::stock_image_expand(source.get(),missing_policy.get(),false);},"sparse-zero-policy-required");
        auto output=fresh(work/"expanded.img"); ure::stock_image_expand(source.get(),output.get(),true);
        check(ure::storage_read(output.get(),0,expanded.size())==expanded,"RAW/FILL/DONT_CARE sparse expansion differs from byte oracle");
        check(ure::sha256(source.get())==ure::sha256(file) && ure::sha256(holes.get())==before,"Read-only source was changed");
        const auto [large_file,large_bytes]=sparse_fixture(16385,8194); auto large_source=fresh(work/"large.sparse"); put_bytes(large_source.get(),large_file);
        const auto large_info=ure::stock_image_inspect(large_source.get()); auto large_out=fresh(work/"large-expanded.img"); ure::stock_image_expand(large_source.get(),large_out.get(),true);
        check(large_info["expanded_digest"].asString()==naive_tree(large_bytes) && ure::sha256(large_out.get())==ure::sha256(large_bytes),"Partial leaves and repeated cached sparse leaves differ from the logical-byte oracle");
        unsigned sequence=0;
        auto malformed=[&](const std::string& data) { auto fd=fresh(work/("bad-"+std::to_string(sequence++))); put_bytes(fd.get(),data); reject([&]{ure::stock_image_inspect(fd.get());},"invalid-sparse-image"); };
        auto bad=file; bad.back()^=1; malformed(bad); bad=file; put(bad,24,1,4); malformed(bad);
        malformed(file+"trailing"); malformed(file.substr(0,file.size()-1));
        bad=file; put(bad,6,1,2); malformed(bad); bad=file; put(bad,12,UINT32_MAX,4); malformed(bad);
        bad=file; put(bad,16,UINT32_MAX,4); malformed(bad); bad=file; put(bad,20,UINT32_MAX,4); malformed(bad);
        bad=file; put(bad,28,0x1234,2); malformed(bad); bad=file; put(bad,32,0,4); malformed(bad);
        bad=file; put(bad,36,UINT32_MAX,4); malformed(bad); bad=file; put(bad,8,4097,2); malformed(bad);
        auto raw=fresh(work/"raw.img"); const std::string raw_bytes(4096,'B'); put_bytes(raw.get(),raw_bytes); auto raw_out=fresh(work/"raw-out.img");
        check(ure::stock_image_expand(raw.get(),raw_out.get(),false)["encoding"]=="raw" && ure::sha256(raw_out.get())==ure::sha256(raw_bytes),"Raw stock expansion differs");
        auto unaligned=fresh(work/"unaligned.img"); put_bytes(unaligned.get(),"small"); reject([&]{ure::stock_image_inspect(unaligned.get());},"invalid-image");
        if(argc>=2) {
            ure::Root input(argv[1]); auto metadata=input.open("metadata.img",O_RDONLY); const auto observed=ure::stock_image_inspect(metadata.get());
            check(ure::sha256(metadata.get())=="999f892b89a9b4dcbcdc54e4d1e5dd85f4edc1cefe2606dcb960824e97e81c18" && observed["encoding"]=="android-sparse-v1" && observed["expanded_bytes"].asUInt64()==64*1024*1024,"Pinned OEM metadata source or expansion differs");
            auto stock_out=fresh(work/"metadata-expanded.img"); const auto result=ure::stock_image_expand(metadata.get(),stock_out.get(),true);
            check(ure::json(result)==ure::json(observed) && ure::storage_bytes(stock_out.get())==64*1024*1024 && observed["expanded_digest"].asString()==ure::storage_image_range_digest(stock_out.get(),0,64*1024*1024),"OEM metadata expansion lost logical-byte binding");
        }
        if(argc==3) {
            const auto catalog=ure::parse_json(ure::bounded_read(argv[2],65536)); check(catalog["payloads"].size()==std::size(ure::stock_source::global),"Source catalog and native pin counts differ");
            for(const auto& pin:ure::stock_source::global) {
                const ure::Value* observed=nullptr; for(const auto& row:catalog["payloads"])if(row["filename"]==pin.filename) { check(observed==nullptr,"Duplicate source catalog pin"); observed=&row; }
                check(observed!=nullptr && (*observed)["source_bytes"].asUInt64()==pin.source_bytes && (*observed)["expanded_bytes"].asUInt64()==pin.expanded_bytes &&
                    (*observed)["source_sha256"]==pin.sha256 && (*observed)["encoding"]==pin.encoding,"Native stock payload pin differs from independent archive catalog");
            }
        }
        ure::fs::remove_all(work); std::cout<<"PASS canonical image ranges, private sparse expansion and malformed-input refusals\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; if(!work.empty())ure::fs::remove_all(work); return 1; }
}
