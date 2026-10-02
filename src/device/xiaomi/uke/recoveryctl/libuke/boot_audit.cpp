// SPDX-License-Identifier: Apache-2.0
// Inspect installed assets without loading modules, running hooks or changing boot selection.
#include "uke.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>
#include <zlib.h>
#include <zstd.h>
#ifdef __ANDROID__
extern "C" {
#include <Xz.h>
#include <7zCrc.h>
#include <XzCrc64.h>
}
#include <mutex>
#else
#include <lzma.h>
#endif

namespace ure {
namespace {
constexpr std::size_t asset_limit=96*1024*1024;
std::uint64_t integer(std::string_view bytes,std::size_t at,unsigned size,bool big=false) {
    require(size<=8 && at<=bytes.size() && size<=bytes.size()-at,"invalid-binary","Binary integer exceeds its container");
    std::uint64_t value=0;
    for(unsigned i=0;i<size;++i) { const unsigned shift=(big ? size-i-1 : i)*8;
        value|=static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[at+i]))<<shift; }
    return value;
}
std::string trim(std::string text) {
    const auto first=text.find_first_not_of(" \r\n\t"),last=text.find_last_not_of(" \r\n\t");
    return first==text.npos ? "" : text.substr(first,last-first+1);
}
std::vector<std::string> names(const Root& root,const std::string& path,std::size_t limit=4096) {
    if(!root.exists_resolved(path))return {};
    auto fd=root.open_resolved(path,O_RDONLY|O_DIRECTORY);
    DIR* stream=::fdopendir(::dup(fd.get())); require(stream,"io-error","Cannot enumerate installed assets");
    std::unique_ptr<DIR,int(*)(DIR*)> guard(stream,::closedir); std::vector<std::string> result;
    while(auto* entry=::readdir(stream)) { std::string name=entry->d_name; if(name=="." || name=="..")continue;
        require(result.size()<limit,"size-limit","Installed-asset directory exceeds its bound"); result.push_back(name); }
    std::sort(result.begin(),result.end()); return result;
}
std::string decoded(std::string_view bytes,std::size_t limit) {
    if(bytes.starts_with("\x28\xb5\x2f\xfd")) {
        std::unique_ptr<ZSTD_DStream,decltype(&ZSTD_freeDStream)> stream(ZSTD_createDStream(),ZSTD_freeDStream);
        require(stream && !ZSTD_isError(ZSTD_initDStream(stream.get())) && !ZSTD_isError(ZSTD_DCtx_setParameter(stream.get(),ZSTD_d_windowLogMax,24)),
            "invalid-compression","Cannot initialize bounded Zstd inspection");
        ZSTD_inBuffer input{bytes.data(),bytes.size(),0}; std::array<char,65536> buffer{}; std::string output; std::size_t remaining=1;
        while(input.pos<input.size || remaining) {
            const auto before=input.pos; ZSTD_outBuffer block{buffer.data(),buffer.size(),0}; remaining=ZSTD_decompressStream(stream.get(),&block,&input);
            require(!ZSTD_isError(remaining) && (input.pos>before || block.pos>0),"invalid-compression","Corrupt or truncated Zstd asset");
            require(output.size()+block.pos<=limit,"size-limit","Zstd expansion exceeds its inspection budget"); output.append(buffer.data(),block.pos);
        }
        return output;
    }
    if(bytes.size()>=2 && integer(bytes,0,2)==0x8b1f) {
        z_stream stream{}; require(inflateInit2(&stream,15+32)==Z_OK,"invalid-compression","Cannot initialize gzip inspection");
        struct End { z_stream* stream; ~End() { inflateEnd(stream); } } end{&stream};
        require(bytes.size()<=UINT_MAX,"size-limit","Compressed asset exceeds its bound");
        stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data())); stream.avail_in=static_cast<uInt>(bytes.size());
        std::array<char,65536> buffer{}; std::string output; int status=Z_OK;
        for(unsigned frames=0;;++frames) {
            require(frames<128,"size-limit","Too many gzip members");
            while(status==Z_OK) { stream.next_out=reinterpret_cast<Bytef*>(buffer.data()); stream.avail_out=static_cast<uInt>(buffer.size());
                status=inflate(&stream,Z_NO_FLUSH); const auto count=buffer.size()-stream.avail_out;
                require(output.size()+count<=limit,"size-limit","Decompressed asset exceeds its inspection budget"); output.append(buffer.data(),count); }
            require(status==Z_STREAM_END,"invalid-compression","Corrupt or truncated gzip member");
            if(stream.avail_in==0)return output;
            auto* next=stream.next_in; const auto remaining=stream.avail_in;
            require(remaining>=2 && next[0]==0x1f && next[1]==0x8b && inflateReset2(&stream,15+32)==Z_OK,"invalid-compression","Garbage after a complete gzip member");
            stream.next_in=next; stream.avail_in=remaining; status=Z_OK;
        }
    }
    if(bytes.starts_with("\xfd" "7zXZ\0")) {
#ifdef __ANDROID__
        struct Allocator {
            ISzAlloc interface; std::size_t allocated=0;
            struct alignas(std::max_align_t) Header { std::size_t size; };
            static void* allocate(ISzAllocPtr pointer,std::size_t size) {
                auto* self=const_cast<Allocator*>(reinterpret_cast<const Allocator*>(pointer));
                if(size>64*1024*1024-self->allocated || size>SIZE_MAX-sizeof(Header))return nullptr;
                auto* header=static_cast<Header*>(std::malloc(sizeof(Header)+size)); if(!header)return nullptr;
                header->size=size; self->allocated+=size; return header+1;
            }
            static void release(ISzAllocPtr pointer,void* value) {
                if(!value)return;
                auto* self=const_cast<Allocator*>(reinterpret_cast<const Allocator*>(pointer)); auto* header=static_cast<Header*>(value)-1;
                self->allocated-=header->size; std::free(header);
            }
        } allocator{{Allocator::allocate,Allocator::release},0};
        static std::once_flag crc; std::call_once(crc,[] { CrcGenerateTable(); Crc64GenerateTable(); });
        CXzUnpacker stream{}; XzUnpacker_Construct(&stream,&allocator.interface); XzUnpacker_Init(&stream);
        struct End { CXzUnpacker* stream; ~End() { XzUnpacker_Free(stream); } } end{&stream};
        std::array<Byte,65536> buffer{}; std::string output; std::size_t offset=0;
        for(;;) { SizeT packed=bytes.size()-offset,unpacked=buffer.size(); ECoderStatus status=CODER_STATUS_NOT_SPECIFIED;
            const auto result=XzUnpacker_Code(&stream,buffer.data(),&unpacked,reinterpret_cast<const Byte*>(bytes.data()+offset),&packed,1,CODER_FINISH_ANY,&status);
            require(result==SZ_OK && output.size()+unpacked<=limit,"invalid-compression","Corrupt XZ asset or decoder memory/expansion limit exceeded");
            output.append(reinterpret_cast<const char*>(buffer.data()),unpacked); offset+=packed;
            if(offset==bytes.size() && XzUnpacker_IsStreamWasFinished(&stream))return output;
            require(packed || unpacked,"invalid-compression","Truncated XZ stream"); }
#else
        lzma_stream stream=LZMA_STREAM_INIT;
        require(lzma_stream_decoder(&stream,64*1024*1024,LZMA_CONCATENATED)==LZMA_OK,"invalid-compression","Cannot initialize XZ inspection");
        struct End { lzma_stream* stream; ~End() { lzma_end(stream); } } end{&stream};
        stream.next_in=reinterpret_cast<const std::uint8_t*>(bytes.data()); stream.avail_in=bytes.size(); std::string output;
        std::array<std::uint8_t,65536> buffer{}; lzma_ret status=LZMA_OK;
        while(status==LZMA_OK) { stream.next_out=buffer.data(); stream.avail_out=buffer.size(); status=lzma_code(&stream,LZMA_FINISH);
            const auto count=buffer.size()-stream.avail_out; require(output.size()+count<=limit,"size-limit","XZ expansion exceeds its inspection budget");
            output.append(reinterpret_cast<const char*>(buffer.data()),count); }
        require(status==LZMA_STREAM_END && stream.avail_in==0,"invalid-compression","Corrupt XZ asset"); return output;
#endif
    }
    require(bytes.size()<=limit,"size-limit","Asset exceeds its inspection budget"); return std::string(bytes);
}
std::string architecture(unsigned machine) { return machine==183 ? "aarch64" : machine==62 || machine==0x8664 ? "x86_64" : machine==0xaa64 ? "aarch64" : "other"; }
Value elf(std::string_view bytes) {
    require(bytes.size()>=64 && bytes.starts_with("\x7f" "ELF") && bytes[4]==2 && bytes[5]==1 && bytes[6]==1,
        "invalid-module","Expected a little-endian ELF64 module");
    const auto count=integer(bytes,60,2),offset=integer(bytes,40,8),size=integer(bytes,58,2),strings=integer(bytes,62,2);
    require(integer(bytes,16,2)==1 && size==64 && count>0 && count<=4096 && strings<count && offset<=bytes.size() && count*size<=bytes.size()-offset,
        "invalid-module","ELF section table is invalid or unsupported");
    const auto table=static_cast<std::size_t>(offset+strings*size);
    const auto str_offset=integer(bytes,table+24,8),str_size=integer(bytes,table+32,8);
    require(str_offset<=bytes.size() && str_size<=bytes.size()-str_offset,"invalid-module","ELF string table exceeds its file");
    Value out; out["architecture"]=architecture(static_cast<unsigned>(integer(bytes,18,2))); bool found=false;
    for(std::uint64_t i=0;i<count;++i) {
        const auto section=static_cast<std::size_t>(offset+i*size),name=integer(bytes,section,4);
        require(name<str_size,"invalid-module","ELF section name exceeds its table");
        const auto string_at=static_cast<std::size_t>(str_offset+name); const auto end=bytes.find('\0',string_at);
        require(end!=bytes.npos && end<str_offset+str_size,"invalid-module","Unterminated ELF section name");
        if(bytes.substr(string_at,end-string_at)!=".modinfo")continue;
        require(!found,"invalid-module","Duplicate module information section"); found=true;
        const auto first=integer(bytes,section+24,8),length=integer(bytes,section+32,8);
        require(first<=bytes.size() && length<=bytes.size()-first && length<=1024*1024,"invalid-module","Module metadata exceeds its bound");
        const auto metadata=bytes.substr(static_cast<std::size_t>(first),static_cast<std::size_t>(length)); std::size_t pos=0;
        while(pos<metadata.size()) { const auto stop=metadata.find('\0',pos); require(stop!=metadata.npos,"invalid-module","Unterminated module metadata");
            const auto field=metadata.substr(pos,stop-pos); if(field.starts_with("vermagic=")) { require(!out.isMember("vermagic"),"invalid-module","Duplicate vermagic"); out["vermagic"]=std::string(field.substr(9)); }
            if(field.starts_with("depends=")) { require(field.size()<=16384,"invalid-module","Module dependency metadata exceeds its bound"); out["depends"]=std::string(field.substr(8)); }
            pos=stop+1; }
    }
    require(found && out["vermagic"].isString(),"invalid-module","Module vermagic is absent"); return out;
}
Value dtb(std::string_view bytes) {
    require(bytes.size()>=40 && integer(bytes,0,4,true)==0xd00dfeed,"invalid-dtb","FDT header is absent");
    const auto total=integer(bytes,4,4,true),structure=integer(bytes,8,4,true),strings=integer(bytes,12,4,true);
    const auto strings_size=integer(bytes,32,4,true),structure_size=integer(bytes,36,4,true);
    require(total<=bytes.size() && total>=40 && structure>=40 && strings>=40 && structure<=total && structure_size<=total-structure &&
        strings<=total && strings_size<=total-strings && integer(bytes,20,4,true)>=17 && integer(bytes,24,4,true)<=17,
        "invalid-dtb","FDT bounds or version are invalid");
    Value out; out["compatible"]=Value(Json::arrayValue); std::size_t position=static_cast<std::size_t>(structure); unsigned depth=0; bool ended=false;
    const auto stop=static_cast<std::size_t>(structure+structure_size);
    for(unsigned tokens=0;tokens<100000 && position<stop;++tokens) {
        const auto token=integer(bytes,position,4,true); position+=4;
        if(token==1) { ++depth; require(depth<=64,"invalid-dtb","FDT depth exceeds its bound"); const auto end=bytes.find('\0',position);
            require(end!=bytes.npos && end<stop,"invalid-dtb","FDT node name is truncated"); position=(end+4)&~std::size_t(3); }
        else if(token==2) { require(depth>0,"invalid-dtb","Unbalanced FDT nodes"); --depth; }
        else if(token==3) {
            require(depth>0 && position+8<=stop,"invalid-dtb","FDT property outside a node");
            const auto length=integer(bytes,position,4,true),name=integer(bytes,position+4,4,true); position+=8;
            require(name<strings_size && position<=stop && length<=stop-position,"invalid-dtb","FDT property exceeds its container");
            const auto name_at=static_cast<std::size_t>(strings+name),name_end=bytes.find('\0',name_at);
            require(name_end!=bytes.npos && name_end<strings+strings_size,"invalid-dtb","Unterminated FDT property name");
            if(depth==1 && bytes.substr(name_at,name_end-name_at)=="compatible") {
                const auto value=bytes.substr(position,static_cast<std::size_t>(length)); std::size_t at=0;
                while(at<value.size()) { const auto end=value.find('\0',at); require(end!=value.npos && end>at && out["compatible"].size()<64,"invalid-dtb","Invalid compatible list");
                    out["compatible"].append(std::string(value.substr(at,end-at))); at=end+1; }
            } position=(position+static_cast<std::size_t>(length)+3)&~std::size_t(3);
        } else if(token==4)continue;
        else if(token==9) { require(depth==0,"invalid-dtb","Unclosed FDT nodes"); ended=true; break; }
        else throw Error("invalid-dtb","Unknown FDT token");
    }
    require(ended && !out["compatible"].empty(),"invalid-dtb","FDT did not finish or lacks root compatibility"); out["structure_valid"]=true; return out;
}
Value initrd(std::string_view bytes,unsigned depth=0,std::size_t limit=asset_limit) {
    require(depth<=4,"size-limit","Initramfs compression depth exceeds its bound");
    const auto data=decoded(bytes,limit); Value out; out["module_releases"]=Value(Json::arrayValue);
    std::set<std::string> releases; std::size_t position=0; unsigned files=0; bool trailer=false;
    auto hexadecimal=[&](std::size_t at) { require(at+8<=data.size(),"invalid-initramfs","Truncated cpio integer"); std::uint32_t number=0;
        const auto parsed=std::from_chars(data.data()+at,data.data()+at+8,number,16); require(parsed.ec==std::errc() && parsed.ptr==data.data()+at+8,"invalid-initramfs","Invalid cpio integer"); return number; };
    while(position<data.size()) {
        while(position<data.size() && data[position]=='\0')++position;
        if(position==data.size())break;
        if(data.compare(position,6,"070701")!=0 && data.compare(position,6,"070702")!=0 && position>0 && trailer) {
            const auto tail=initrd(std::string_view(data).substr(position),depth+1,limit-position);
            for(const auto& version:tail["module_releases"])releases.insert(version.asString());
            files+=tail["files"].asUInt(); require(files<=100000,"size-limit","Combined initramfs file count exceeds its bound");
            out["compressed_tail_bytes"]=tail["expanded_bytes"]; break;
        }
        require(position+110<=data.size() && (data.compare(position,6,"070701")==0 || data.compare(position,6,"070702")==0),"invalid-initramfs","Expected a complete newc cpio archive");
        const auto length=hexadecimal(position+54),name_size=hexadecimal(position+94);
        require(name_size>0 && name_size<=4096 && ++files<=100000 && name_size<=data.size()-position-110,"invalid-initramfs","Cpio name exceeds its bound");
        const auto name_at=position+110; require(data[name_at+name_size-1]=='\0',"invalid-initramfs","Unterminated cpio path");
        const auto name=data.substr(name_at,name_size-1); auto normalized=name; if(normalized.starts_with("./"))normalized.erase(0,2);
        if(normalized!=".")components(normalized);
        const auto content=(name_at+name_size+3)&~std::size_t(3);
        require(content<=data.size() && length<=data.size()-content,"invalid-initramfs","Cpio data is truncated");
        for(const auto* prefix:{"usr/lib/modules/","lib/modules/"})if(normalized.starts_with(prefix)) {
            const auto remainder=normalized.substr(std::strlen(prefix)); const auto slash=remainder.find('/'); const auto version=remainder.substr(0,slash);
            if(identifier(version))releases.insert(version); }
        if(data.compare(position,6,"070702")==0) { std::uint32_t sum=0; for(std::size_t i=0;i<length;++i)sum+=static_cast<unsigned char>(data[content+i]);
            require(sum==hexadecimal(position+102),"invalid-initramfs","Cpio data checksum differs"); }
        trailer=name=="TRAILER!!!"; if(trailer)require(length==0,"invalid-initramfs","Cpio trailer contains data");
        position=(content+length+3)&~std::size_t(3);
    }
    require(trailer,"invalid-initramfs","Cpio archive lacks its trailer"); out["archive_valid"]=true; out["files"]=files;
    for(const auto& version:releases)out["module_releases"].append(version);
    out["expanded_bytes"]=Json::UInt64(data.size()); return out;
}
Value kernel(std::string_view bytes) {
    Value out;
    if(bytes.size()>=64 && integer(bytes,56,4)==0x644d5241) {
        const auto size=integer(bytes,16,8); require(size==0 || size<=bytes.size(),"invalid-kernel","ARM64 Image is truncated");
        out["format"]="arm64-Image"; out["architecture"]="aarch64"; out["declared_image_bytes"]=Json::UInt64(size);
    } else if(bytes.size()>0x206 && bytes.substr(0x202,4)=="HdrS") { out["format"]="x86-bzImage"; out["architecture"]="x86_64"; }
    else throw Error("unrecognized-kernel","Unrecognized kernel header; an existing filename is insufficient");
    out["header_valid"]=true; out["embedded_version_validated"]=false; return out;
}
Value pe(std::string_view bytes) {
    require(bytes.size()>=64 && bytes.starts_with("MZ"),"invalid-uki","PE DOS header is missing");
    const auto pe_at=integer(bytes,60,4); require(pe_at<=bytes.size() && bytes.size()-pe_at>=24 && bytes.substr(static_cast<std::size_t>(pe_at),4)==std::string_view("PE\0\0",4),"invalid-uki","PE signature is invalid");
    const auto count=integer(bytes,static_cast<std::size_t>(pe_at)+6,2),optional=integer(bytes,static_cast<std::size_t>(pe_at)+20,2),table=pe_at+24+optional;
    require(count>0 && count<=96 && optional>=112 && table<=bytes.size() && count*40<=bytes.size()-table && integer(bytes,static_cast<std::size_t>(pe_at)+24,2)==0x20b,
        "invalid-uki","PE section table or optional header is invalid");
    Value out; out["architecture"]=architecture(static_cast<unsigned>(integer(bytes,static_cast<std::size_t>(pe_at)+4,2)));
    out["sections"]=Value(Json::arrayValue); std::map<std::string,std::string_view> sections; std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;
    for(std::uint64_t i=0;i<count;++i) {
        const auto at=static_cast<std::size_t>(table+i*40); auto name=bytes.substr(at,8); name=name.substr(0,name.find('\0'));
        const auto virtual_size=integer(bytes,at+8,4),size=integer(bytes,at+16,4),offset=integer(bytes,at+20,4);
        const bool payload=name==".linux" || name==".osrel" || name==".initrd" || name==".dtb" || name==".uname" || name==".cmdline";
        require(!name.empty() && !sections.contains(std::string(name)) && offset<=bytes.size() && size<=bytes.size()-offset && (!payload || virtual_size<=size),
            "invalid-uki","Duplicate, truncated or unsupported PE section");
        if(size) { require(offset>=table+count*40,"invalid-uki","PE section overlaps its headers"); ranges.emplace_back(offset,offset+size); }
        sections[std::string(name)]=bytes.substr(static_cast<std::size_t>(offset),static_cast<std::size_t>(std::min(virtual_size,size))); out["sections"].append(std::string(name));
    }
    std::sort(ranges.begin(),ranges.end()); for(std::size_t i=1;i<ranges.size();++i)require(ranges[i-1].second<=ranges[i].first,"invalid-uki","Overlapping PE sections");
    require(sections.contains(".linux") && sections.contains(".osrel"),"invalid-uki","UKI lacks Linux or os-release sections");
    out["kernel"]=kernel(sections.at(".linux")); require(out["architecture"]==out["kernel"]["architecture"],"invalid-uki","EFI and kernel architectures differ");
    if(sections.contains(".initrd"))out["initramfs"]=initrd(sections.at(".initrd"));
    if(sections.contains(".dtb"))out["device_tree"]=dtb(sections.at(".dtb"));
    for(const auto& field:{".uname",".cmdline",".osrel"})if(sections.contains(field)) {
        auto text=sections.at(field); while(!text.empty() && text.back()=='\0')text.remove_suffix(1);
        require(text.size()<=65536 && text.find('\0')==text.npos && utf8(text),"invalid-uki","Invalid embedded UKI text"); out[field]=std::string(text);
    }
    out["secure_boot_signature_verified"]=false; out["container_valid"]=true; return out;
}
Value asset(const Root& root,const std::string& path,const std::string& kind) {
    Value out; out["path"]=path; out["valid"]=false;
    try { auto fd=root.open_resolved(path,O_RDONLY|O_NONBLOCK); struct stat before{},after{};
        require(::fstat(fd.get(),&before)==0 && S_ISREG(before.st_mode) && before.st_size>0 && static_cast<std::uint64_t>(before.st_size)<=asset_limit,"invalid-asset","Asset is not a bounded regular file");
        const auto data=storage_read(fd.get(),0,static_cast<std::size_t>(before.st_size));
        out["metadata"]=kind=="kernel" ? kernel(decoded(data,asset_limit)) : kind=="initrd" ? initrd(data) : kind=="dtb" ? dtb(data) : kind=="uki" ? pe(data) : elf(decoded(data,32*1024*1024));
        out["sha256"]=sha256(data); out["bytes"]=Json::UInt64(data.size());
        require(::fstat(fd.get(),&after)==0 && before.st_size==after.st_size && before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
            before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec,"stale-asset","Installed asset changed during inspection"); out["valid"]=true;
    } catch(const Error& error) { out["error_code"]=error.code; out["reason"]=error.what(); } return out;
}
void finding(Value& report,const std::string& severity,const std::string& code,const std::string& subject,const std::string& detail) {
    require(report["findings"].size()<8192,"size-limit","Boot findings exceed their bound"); Value item;
    item["severity"]=severity; item["code"]=code; item["subject"]=subject; item["detail"]=detail; report["findings"].append(item);
}
void root_options(Value& report,const std::string& subject,const std::string& options,const std::string& fstab_root) {
    std::istringstream input(options); std::string word,root; unsigned roots=0;
    while(input>>word)if(word.starts_with("root=")) { ++roots; root=word.substr(5); }
    if(roots>1)finding(report,"error","root-option-count",subject,"Boot command line contains conflicting root selections");
    else if(roots==0)finding(report,"warning","root-autodiscovery-unverified",subject,"No explicit root option; initramfs or GPT autodiscovery must be verified separately");
    else if(root.starts_with("PARTUUID=") && !uuid(root.substr(9)))finding(report,"error","invalid-root-partuuid",subject,"Boot root PARTUUID is malformed");
    else if(!fstab_root.empty() && root!=fstab_root)finding(report,"warning","root-fstab-difference",subject,"Boot and fstab root selectors differ; equivalence needs actual block identity evidence");
    if(root.starts_with("/dev/sd") || root.starts_with("/dev/nvme"))finding(report,"warning","unstable-root-selector",subject,"A kernel device name can change between boots");
}
void module_architecture(Value& report,const std::string& subject,const std::string& release,const std::string& expected) {
    if(expected.empty())return;
    const auto userspace=report["userspace_architecture"].asString();
    if((userspace=="aarch64" || userspace=="x86_64") && userspace!=expected)
        finding(report,"error","userspace-kernel-architecture",subject,"Installed userspace architecture "+userspace+" differs from the selected kernel image");
    for(const auto& installed:report["kernels"])if(installed["release"]==release)
        for(const auto& observed:installed["module_architectures"].getMemberNames())if(observed!=expected)
            finding(report,"error","module-architecture-mismatch",subject,"Installed module architecture "+observed+" differs from its selected kernel image");
}
void uki_consistency(Value& report,const std::string& subject,const Value& metadata,const std::set<std::string>& releases,const std::string& fstab_root) {
    const auto release=trim(metadata.get(".uname","").asString());
    if(!release.empty() && !releases.contains(release))finding(report,"error","uki-module-release",subject,"UKI release lacks matching installed modules");
    for(const auto& embedded:metadata["initramfs"]["module_releases"])if(!release.empty() && embedded!=release)
        finding(report,"error","uki-initramfs-release",subject,"Embedded initramfs and UKI kernel release differ");
    module_architecture(report,subject,release,metadata["kernel"]["architecture"].asString());
    std::istringstream lines(metadata.get(".osrel","").asString()); std::string line,id;
    while(std::getline(lines,line))if(line.starts_with("ID=")) { id=trim(line.substr(3)); if(id.size()>1 && (id.front()=='\"' || id.front()=='\''))id=id.substr(1,id.size()-2); }
    if(!id.empty() && report["distribution"]["ID"].isString() && id!=report["distribution"]["ID"].asString())
        finding(report,"error","uki-distribution-mismatch",subject,"UKI os-release identifies a different installed distribution");
    root_options(report,subject,metadata.get(".cmdline","").asString(),fstab_root);
}
void audit_entries(Value& report,const Root& context,const std::string& boot,const std::string& origin,const std::string& fstab_root,const std::set<std::string>& releases) {
    for(const auto& name:names(context,boot+"loader/entries"))if(identifier(name) && name.ends_with(".conf")) {
        Value entry; entry["id"]=name; entry["origin"]=origin; entry["assets"]=Value(Json::arrayValue); bool valid=true;
        try {
            std::istringstream lines(context.read_resolved(boot+"loader/entries/"+name,65536)); std::string line; std::map<std::string,std::string> fields; std::vector<std::string> initrds;
            while(std::getline(lines,line)) { require(line.size()<=4096,"invalid-bls","BLS line exceeds its bound"); line=trim(line); if(line.empty() || line[0]=='#')continue;
                const auto space=line.find_first_of(" \t"); require(space!=line.npos,"invalid-bls","BLS field lacks a value"); const auto key=line.substr(0,space),value=trim(line.substr(space+1));
                if(key=="initrd") { require(initrds.size()<32,"invalid-bls","Too many initrds"); initrds.push_back(value); }
                else if(key=="linux" || key=="efi" || key=="devicetree" || key=="options" || key=="version" || key=="title") {
                    require(!fields.contains(key) && !value.empty(),"invalid-bls","Duplicate or empty BLS field"); fields[key]=value; }
            }
            require(fields.contains("linux")!=fields.contains("efi"),"invalid-bls","BLS must select exactly one Linux or EFI image");
            auto inspect=[&](std::string path,const std::string& kind) { if(path.starts_with('/'))path.erase(0,1); components(path);
                auto result=asset(context,boot+path,kind); valid&=result["valid"].asBool(); entry["assets"].append(result);
                if(!result["valid"].asBool())finding(report,"error",result["error_code"].asString(),origin+"/"+name,result["reason"].asString());
                return result; };
            if(fields.contains("linux")) {
                const auto image=inspect(fields["linux"],"kernel");
                if(image["valid"]==true && fields.contains("version"))module_architecture(report,origin+"/"+name,fields["version"],image["metadata"]["architecture"].asString());
            } else { const auto image=inspect(fields["efi"],"uki"); if(image["valid"]==true)uki_consistency(report,origin+"/"+name,image["metadata"],releases,fstab_root); }
            for(const auto& path:initrds) { const auto result=inspect(path,"initrd");
                for(const auto& release:result["metadata"]["module_releases"])if(fields.contains("version") && release!=fields["version"])
                    finding(report,"error","initramfs-version-mismatch",origin+"/"+name,"Initramfs includes modules for a different kernel release"); }
            if(fields.contains("devicetree"))inspect(fields["devicetree"],"dtb");
            if(fields.contains("version") && !releases.contains(fields["version"]))finding(report,"error","missing-kernel-modules",origin+"/"+name,"Entry version has no installed module directory");
            root_options(report,origin+"/"+name,fields["options"],fstab_root); for(const auto& [key,value]:fields)entry[key]=value;
        } catch(const Error& error) { valid=false; finding(report,"error",error.code,origin+"/"+name,error.what()); }
        entry["assets_valid"]=valid; entry["boot_validated"]=false; report["boot_entries"].append(entry);
    }
    for(const auto& name:names(context,boot+"EFI/Linux"))if(identifier(name) && name.ends_with(".efi")) {
        auto result=asset(context,boot+"EFI/Linux/"+name,"uki"); result["origin"]=origin;
        if(!result["valid"].asBool())finding(report,"error",result["error_code"].asString(),name,result["reason"].asString());
        else uki_consistency(report,name,result["metadata"],releases,fstab_root);
        report["ukis"].append(result);
    }
}
} // namespace
Value linux_boot_audit(const Root& root,const Root* esp) {
    const auto installed=linux_detect(root,esp);
    Value out; out["schema"]=1; out["format"]="ure-linux-boot-audit"; out["distribution"]=installed["distribution"];
    out["userspace_architecture"]=installed["architecture"];
    out["root_identity"]=descriptor_identity(root.fd()); out["read_only"]=true; out["private_record"]=true;
    out["findings"]=Value(Json::arrayValue); out["kernels"]=Value(Json::arrayValue); out["boot_entries"]=Value(Json::arrayValue); out["ukis"]=Value(Json::arrayValue);
    out["boot_validated"]=false; out["physical_test_record"]=false; out["module_load_test"]=false; std::string fstab_root;
    if(root.exists_resolved("etc/fstab")) { std::istringstream lines(root.read_resolved("etc/fstab")); std::string line;
        while(std::getline(lines,line)) { line=trim(line); if(line.empty() || line[0]=='#')continue; std::istringstream row(line); std::string source,mount;
            row>>source>>mount; if(mount=="/") { if(!fstab_root.empty())finding(out,"error","duplicate-fstab-root","etc/fstab","More than one root mount is declared"); fstab_root=source; } }
    }
    out["fstab_root_selector"]=fstab_root; out["root_block_equivalence_validated"]=false; std::set<std::string> releases;
    for(const auto& release:names(root,"usr/lib/modules",1024))if(identifier(release))releases.insert(release);
    for(const auto& release:names(root,"lib/modules",1024))if(identifier(release))releases.insert(release);
    for(const auto& release:releases) {
        Value item; item["release"]=release; item["modules"]=Value(Json::arrayValue); item["module_architectures"]=Value(Json::objectValue); item["dependency_paths_checked"]=0;
        const auto base=(root.exists_resolved("usr/lib/modules/"+release) ? "usr/lib/modules/" : "lib/modules/")+release+"/";
        std::set<std::string> modules;
        if(root.exists_resolved(base+"modules.dep")) {
            std::istringstream lines(root.read_resolved(base+"modules.dep",4*1024*1024)); std::string line;
            while(std::getline(lines,line)) { const auto colon=line.find(':');
                require(colon!=line.npos && line.size()<=16384,"invalid-module-index","Malformed module dependency index");
                const auto selected=line.substr(0,colon); components(selected); modules.insert(selected); std::istringstream dependencies(line.substr(colon+1)); std::string path;
                while(dependencies>>path) { components(path); if(!root.exists_resolved(base+path))finding(out,"error","missing-module-dependency",release,path);
                    item["dependency_paths_checked"]=item["dependency_paths_checked"].asUInt()+1; }
                require(modules.size()<=16384 && item["dependency_paths_checked"].asUInt()<=100000,"size-limit","Module index exceeds its bound");
            }
        } else finding(out,"error","missing-module-index",release,"modules.dep is absent; rebuild the index using the installed distribution");
        unsigned checked=0; for(const auto& path:modules) {
            ++checked;
            auto result=asset(root,base+path,"module");
            if(!result["valid"].asBool())finding(out,"error",result["error_code"].asString(),release+"/"+path,result["reason"].asString());
            else { const auto magic=result["metadata"]["vermagic"].asString();
                if(magic.substr(0,magic.find(' '))!=release)finding(out,"error","module-vermagic-mismatch",release+"/"+path,"Module release differs from its installed directory");
                const auto arch=result["metadata"]["architecture"].asString(); item["module_architectures"][arch]=item["module_architectures"][arch].asUInt()+1; }
            if(item["modules"].size()<256)item["modules"].append(result);
        }
        item["module_metadata_checked"]=checked; item["module_metadata_truncated"]=false; item["module_records_truncated"]=modules.size()>256;
        if(modules.size()>256)finding(out,"warning","module-report-sample-limit",release,"All indexed module binaries were inspected; the report retains 256 representative records and all detected failures");
        for(const auto* index:{"modules.alias","modules.symbols","modules.builtin"})if(!root.exists_resolved(base+index))finding(out,"warning","missing-module-index-file",release,index);
        for(const auto& path:{"boot/vmlinuz-"+release,"boot/Image-"+release,base+"vmlinuz"})if(root.exists_resolved(path))item["image"]=asset(root,path,"kernel");
        if(root.exists_resolved(base+"pkgbase")) { const auto package=trim(root.read_resolved(base+"pkgbase",128));
            if(identifier(package) && root.exists_resolved("boot/vmlinuz-"+package))item["image"]=asset(root,"boot/vmlinuz-"+package,"kernel"); }
        if(!item.isMember("image"))finding(out,"warning","kernel-image-location",release,"No conventional image was found; inspect the corresponding BLS or UKI entry");
        else if(item["image"]["valid"]!=true)finding(out,"error","invalid-kernel-image",release,item["image"]["reason"].asString());
        else {
            const auto arch=item["image"]["metadata"]["architecture"].asString();
            for(const auto& observed:item["module_architectures"].getMemberNames())if(observed!=arch)
                finding(out,"error","module-architecture-mismatch",release,"Installed module architecture "+observed+" differs from its kernel image");
            const auto userspace=out["userspace_architecture"].asString();
            if((userspace=="aarch64" || userspace=="x86_64") && userspace!=arch)
                finding(out,"error","userspace-kernel-architecture",release,"Installed userspace and kernel architectures differ");
        }
        for(const auto& path:{"boot/initramfs-"+release+".img","boot/initrd.img-"+release})if(root.exists_resolved(path))item["initramfs"]=asset(root,path,"initrd");
        if(item.isMember("initramfs")) {
            if(item["initramfs"]["valid"]!=true)finding(out,"error",item["initramfs"]["error_code"].asString(),release,item["initramfs"]["reason"].asString());
            else for(const auto& embedded:item["initramfs"]["metadata"]["module_releases"])if(embedded!=release)
                finding(out,"error","initramfs-version-mismatch",release,"Conventional initramfs includes modules for a different kernel release");
        }
        out["kernels"].append(item);
    }
    audit_entries(out,root,"boot/","root-boot",fstab_root,releases);
    if(esp) { out["esp_identity"]=descriptor_identity(esp->fd()); audit_entries(out,*esp,"","esp",fstab_root,releases); }
    if(out["boot_entries"].empty() && out["ukis"].empty())finding(out,"warning","no-managed-boot-entry","boot","No BLS or UKI entries were found; GRUB and custom loaders need their own routing audit");
    unsigned errors=0,warnings=0; for(const auto& issue:out["findings"]) { errors+=issue["severity"]=="error"; warnings+=issue["severity"]=="warning"; }
    out["error_count"]=errors; out["warning_count"]=warnings; out["managed_assets_found"]=!out["kernels"].empty() || !out["boot_entries"].empty() || !out["ukis"].empty();
    out["metadata_consistent"]=errors==0 && out["managed_assets_found"]==true; out["device_tree_platform_match_validated"]=false;
    out["coverage"]="bounded binary/container, indexed dependency, release and root-selector inspection; signatures, kernel execution and hardware are not validated";
    require(json(descriptor_identity(root.fd()))==json(out["root_identity"]),"stale-root","Selected root changed during audit"); return out;
}
} // namespace ure
