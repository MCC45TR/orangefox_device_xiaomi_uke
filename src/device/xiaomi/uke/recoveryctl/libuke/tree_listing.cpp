// SPDX-License-Identifier: Apache-2.0
#include "tree_listing.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace ure {
namespace {
// Successful per-name checks must not allocate temporary error strings. This
// matters for both native allocator churn and sanitizer quarantine pressure.
void listing_require(bool condition,const char* code,const char* message) {
    if(!condition)throw Error(code,message);
}
}
void TreeListingBudget::acquire_memory(std::size_t bytes) {
    listing_require(memory<=memory_limit && bytes<=memory_limit-memory,"tree-listing-memory-limit","Tree enumeration exceeds its aggregate working-memory budget");
    memory+=bytes; peak_memory=std::max(peak_memory,memory);
}
void TreeListingBudget::acquire_scratch(std::uint64_t bytes) {
    listing_require(scratch<=scratch_limit && bytes<=scratch_limit-scratch,"tree-listing-scratch-limit","Tree enumeration exceeds its aggregate scratch budget");
    scratch+=bytes; peak_scratch=std::max(peak_scratch,scratch);
}
void TreeListingBudget::admit_child() {
    listing_require(admitted_entries<entry_limit,"size-limit","Tree exceeds one million entries before name allocation"); ++admitted_entries;
}
TreeListingMemory::TreeListingMemory(TreeListingBudget& budget,std::size_t bytes):budget_(&budget),bytes_(bytes) { budget.acquire_memory(bytes); }
TreeListingMemory::~TreeListingMemory() { budget_->memory-=bytes_; }
namespace {
constexpr std::size_t run_entries=1024, buffer_bytes=65536;
struct Name {
    std::array<char,256> bytes{};
    std::uint16_t size=0;
    std::string_view view() const { return {bytes.data(),size}; }
};
Fd scratch_file(int directory) {
    struct stat parent{};
    listing_require(::fstat(directory,&parent)==0 && S_ISDIR(parent.st_mode) && parent.st_uid==::geteuid() && (parent.st_mode&07777)==0700,
        "unsafe-tree-store","Enumeration scratch requires the retained private tree store");
    Fd file(::openat(directory,".",O_TMPFILE|O_EXCL|O_RDWR|O_CLOEXEC,0600));
    if(file.get()<0) {
        const auto error=errno;
        listing_require(error==EOPNOTSUPP || error==EINVAL || error==EISDIR || error==ENOSYS,
            "tree-listing-space","Cannot create anonymous enumeration scratch");
        const auto name="enumeration-"+operation_id()+".tmp";
        file=Fd(::openat(directory,name.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
        listing_require(file.get()>=0,"tree-listing-space","Cannot create private enumeration scratch");
        listing_require(::unlinkat(directory,name.c_str(),0)==0,"tree-listing-space","Cannot detach private enumeration scratch");
    }
    struct stat identity{};
    listing_require(::fstat(file.get(),&identity)==0 && S_ISREG(identity.st_mode) && identity.st_nlink==0 && identity.st_uid==::geteuid() &&
        (identity.st_mode&07777)==0600 && identity.st_size==0,"unsafe-tree-store","Enumeration scratch identity is unsafe");
    return file;
}
void read_exact(int fd,char* bytes,std::size_t amount,std::uint64_t offset) {
    std::size_t done=0;
    while(done<amount) {
        const auto count=::pread(fd,bytes+done,amount-done,static_cast<off_t>(offset+done));
        if(count<0 && errno==EINTR)continue;
        listing_require(count>0,"tree-listing-corrupt","Enumeration scratch ended before its record boundary"); done+=static_cast<std::size_t>(count);
    }
}
struct Run { SortedTreeDirectory file; Name head; bool active=false; };
}
void SortedTreeDirectory::append(std::string_view bytes) {
    budget_->acquire_scratch(bytes.size()); bytes_+=bytes.size();
    struct statvfs available{};
    listing_require(::fstatvfs(file_.get(),&available)==0 && available.f_frsize>0 &&
        available.f_bavail>=(16ULL*1024*1024+bytes.size()+available.f_frsize-1)/available.f_frsize,
        "tree-listing-space","Enumeration scratch must retain at least 16 MiB of free storage");
    std::size_t done=0;
    while(done<bytes.size()) {
        const auto count=::write(file_.get(),bytes.data()+done,bytes.size()-done);
        if(count<0 && errno==EINTR)continue;
        listing_require(count>0,"tree-listing-space","Cannot write enumeration scratch"); done+=static_cast<std::size_t>(count);
    }
}
SortedTreeDirectory::SortedTreeDirectory(int directory,int scratch_directory,TreeListingBudget& budget,bool admit_children) {
    // Delegate resource ownership to a local result until construction succeeds;
    // a throwing constructor must refund all scratch charges and close every fd.
    SortedTreeDirectory result; result.budget_=&budget; result.file_=scratch_file(scratch_directory);
    constexpr std::size_t max_runs=(TreeListingBudget::directory_limit+run_entries-1)/run_entries;
    TreeListingMemory memory(budget,sizeof(Name)*run_entries+(sizeof(Run)+sizeof(std::size_t))*max_runs+buffer_bytes+65536);
    auto chunk=std::make_unique<std::array<Name,run_entries>>();
    std::vector<Run> runs; runs.reserve(max_runs);
    std::array<char,buffer_bytes> encoded{};
    const auto emit=[&](SortedTreeDirectory& output,const Name& name,std::size_t& used) {
        if(used+2+name.size>encoded.size()) { output.append(std::string_view(encoded.data(),used)); used=0; }
        encoded[used++]=static_cast<char>(name.size&255U); encoded[used++]=static_cast<char>(name.size>>8);
        std::memcpy(encoded.data()+used,name.bytes.data(),name.size); used+=name.size;
    };
    std::size_t filled=0;
    const auto finish_run=[&]() {
        if(!filled)return;
        std::sort(chunk->begin(),chunk->begin()+static_cast<std::ptrdiff_t>(filled),[](const Name& a,const Name& b) { return a.view()<b.view(); });
        Run run; run.file.budget_=&budget; run.file.file_=scratch_file(scratch_directory); std::size_t used=0;
        for(std::size_t index=0;index<filled;++index)emit(run.file,(*chunk)[index],used);
        if(used)run.file.append(std::string_view(encoded.data(),used));
        runs.push_back(std::move(run)); filled=0;
    };
    int opened=::openat(directory,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_NOATIME);
    if(opened<0 && errno==EPERM)opened=::openat(directory,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    listing_require(opened>=0,"io-error","Cannot retain directory listing");
    DIR* stream=::fdopendir(opened);
    if(!stream)::close(opened);
    listing_require(stream,"io-error","Cannot enumerate tree directory");
    std::unique_ptr<DIR,int(*)(DIR*)> held(stream,closedir);
    unsigned count=0;
    while(true) {
        errno=0; const auto* record=::readdir(stream);
        if(!record) { listing_require(errno==0,"io-error","Tree listing failed"); break; }
        const auto size=::strnlen(record->d_name,256);
        listing_require(size>0 && size<=255,"invalid-tree","Invalid directory entry name");
        const std::string_view name(record->d_name,size); if(name=="." || name=="..")continue;
        listing_require(count<TreeListingBudget::directory_limit,"size-limit","A tree directory exceeds 100000 children");
        if(admit_children)budget.admit_child(); // Before copying any name into the chunk.
        ++count; auto& target=(*chunk)[filled++]; target.size=static_cast<std::uint16_t>(size); std::memcpy(target.bytes.data(),name.data(),size);
        if(filled==run_entries)finish_run();
    }
    finish_run(); held.reset(); chunk.reset();
    std::string next_name; next_name.reserve(255);
    const auto advance=[&](Run& run) {
        run.active=run.file.next(next_name);
        if(run.active) { run.head.size=static_cast<std::uint16_t>(next_name.size()); std::memcpy(run.head.bytes.data(),next_name.data(),next_name.size()); }
    };
    std::vector<std::size_t> heap; heap.reserve(max_runs);
    for(std::size_t index=0;index<runs.size();++index) { advance(runs[index]); if(runs[index].active)heap.push_back(index); }
    const auto later=[&](std::size_t a,std::size_t b) { return runs[a].head.view()>runs[b].head.view(); };
    std::make_heap(heap.begin(),heap.end(),later);
    std::size_t used=0; Name previous; bool have_previous=false;
    for(unsigned emitted=0;emitted<count;++emitted) {
        listing_require(!heap.empty(),"tree-listing-corrupt","Enumeration merge ended early");
        std::pop_heap(heap.begin(),heap.end(),later); const auto index=heap.back(); heap.pop_back(); auto& smallest=runs[index];
        listing_require(!have_previous || previous.view()<smallest.head.view(),"stale-source","Directory entries changed or repeat during enumeration");
        emit(result,smallest.head,used); previous=smallest.head; have_previous=true; advance(smallest);
        if(smallest.active) { heap.push_back(index); std::push_heap(heap.begin(),heap.end(),later); }
    }
    if(used)result.append(std::string_view(encoded.data(),used));
    *this=std::move(result);
}
SortedTreeDirectory::~SortedTreeDirectory() { if(budget_)budget_->scratch-=bytes_; }
SortedTreeDirectory::SortedTreeDirectory(SortedTreeDirectory&& other) noexcept
    :budget_(other.budget_),file_(std::move(other.file_)),bytes_(other.bytes_),cursor_(other.cursor_) { other.budget_=nullptr; other.bytes_=0; }
SortedTreeDirectory& SortedTreeDirectory::operator=(SortedTreeDirectory&& other) noexcept {
    if(this!=&other) { if(budget_)budget_->scratch-=bytes_; file_=std::move(other.file_); budget_=other.budget_; bytes_=other.bytes_; cursor_=other.cursor_; other.budget_=nullptr; other.bytes_=0; }
    return *this;
}
bool SortedTreeDirectory::next(std::string& name) {
    if(cursor_==bytes_)return false;
    listing_require(cursor_<bytes_ && bytes_-cursor_>=2,"tree-listing-corrupt","Invalid enumeration cursor");
    std::array<char,2> header{}; read_exact(file_.get(),header.data(),header.size(),cursor_);
    const auto size=static_cast<unsigned char>(header[0])+(static_cast<unsigned>(static_cast<unsigned char>(header[1]))<<8);
    listing_require(size>0 && size<=255 && size<=bytes_-cursor_-2,"tree-listing-corrupt","Invalid enumeration record size");
    std::array<char,256> bytes{}; read_exact(file_.get(),bytes.data(),size,cursor_+2); cursor_+=size+2;
    const std::string_view value(bytes.data(),size);
    listing_require(value!="." && value!=".." && value.find('/')==value.npos && value.find('\0')==value.npos,"tree-listing-corrupt","Invalid enumeration record name");
    name.assign(value); return true;
}
} // namespace ure
