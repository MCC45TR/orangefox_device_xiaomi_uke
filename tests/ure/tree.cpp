// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <array>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/xattr.h>
#include <unistd.h>

namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F action,const std::string& code) {
    try { action(); } catch(const ure::Error& e) { check(e.code==code,"Unexpected refusal "+e.code+", wanted "+code); return; }
    throw std::runtime_error("Expected refusal "+code);
}
void put(const ure::fs::path& path,const std::string& data) { std::ofstream file(path,std::ios::binary); file<<data; check(file.good(),"Fixture write failed"); }
struct Workspace {
    ure::fs::path path;
    Workspace() { std::array<char,64> name{}; std::string pattern="/tmp/ure-tree-test-XXXXXX"; std::copy(pattern.begin(),pattern.end(),name.begin()); const auto* p=::mkdtemp(name.data()); check(p,"mkdtemp failed"); path=p; }
    ~Workspace() { std::error_code error; ure::fs::remove_all(path,error); }
};
ure::Value load(const ure::fs::path& store) { return ure::json_file(store/"plan.json"); }
void reseal(const ure::fs::path& store,ure::Value plan) { plan.removeMember("plan_sha256"); plan["plan_sha256"]=ure::sha256(ure::json(plan)); ure::save_json(store/"plan.json",plan,true); }
}
int main() {
    try {
        Workspace work; const auto source=work.path/"source",store=work.path/"store",restored=work.path/"restored";
        ure::fs::create_directories(source/"home"/"user"/"nested"); put(source/"home"/"user"/"nested"/"data","home contents\n");
        const std::string unusual=std::string("binary-\xff",8)+"\nname"; put(source/"home"/"user"/unusual,"opaque filename");
        check(::link((source/"home"/"user"/"nested"/"data").c_str(),(source/"home"/"user"/"hardlink").c_str())==0,"Cannot create hardlink");
        check(::symlink("../../outside",(source/"home"/"user"/"symlink").c_str())==0,"Cannot create symlink");
        check(::mkfifo((source/"home"/"user"/"pipe").c_str(),0640)==0,"Cannot create FIFO");
        ure::Fd sparse(::open((source/"home"/"user"/"sparse").c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
        check(sparse.get()>=0 && ::ftruncate(sparse.get(),8*1024*1024)==0 && ::pwrite(sparse.get(),"tail",4,7*1024*1024)==4,"Cannot create sparse fixture");
        const std::string attribute("\0\xffvalue",7); check(::fsetxattr(sparse.get(),"user.binary",attribute.data(),attribute.size(),0)==0,"Cannot set binary attribute");
        const struct timespec times[2]{{0,UTIME_OMIT},{1700000000,123456789}};
        check(::utimensat(AT_FDCWD,(source/"home"/"user"/"nested").c_str(),times,0)==0,"Cannot set directory mtime");
        ure::Root root(source); const auto planned=ure::backup_tree_plan(root,"home/user","global-os3.0.303.0",store);
        check(planned["state"]=="PLANNED" && planned["entries"].asUInt()==8,"Unexpected tree plan");
        const auto hash=planned["plan_sha256"].asString();
        reject([&] { ure::backup_tree_verify(store); },"path-unavailable");
        reject([&] { ure::backup_tree_capture(root,store,"wrong"); },"confirmation-required");
        check(ure::backup_tree_capture(root,store,hash)["verified"]==true,"Tree capture failed");
        check(ure::backup_tree_verify(store)["verified"]==true,"Offline tree verify failed");
        check(ure::backup_tree_capture(root,store,hash)["verified"]==true,"Idempotent tree resume failed");
        check(ure::backup_tree_restore(store,restored,hash)["verified"]==true,"Tree restore failed");
        ure::Root recovered(restored); check(recovered.read("nested/data")=="home contents\n","Restored contents differ");
        check(recovered.stat("nested/data").st_ino==recovered.stat("hardlink").st_ino,"Hardlink identity lost");
        check(ure::fs::read_symlink(restored/"symlink")=="../../outside","Symlink target lost");
        std::ifstream opaque(restored/unusual,std::ios::binary); const std::string opaque_text{std::istreambuf_iterator<char>(opaque),std::istreambuf_iterator<char>()};
        check(opaque_text=="opaque filename","Opaque filename lost");
        struct stat pipe{}; check(::lstat((restored/"pipe").c_str(),&pipe)==0 && S_ISFIFO(pipe.st_mode),"FIFO type lost");
        const auto dir=recovered.stat("nested"); check(dir.st_mtim.tv_sec==1700000000 && dir.st_mtim.tv_nsec==123456789,"Directory mtime lost");
        auto sparse_result=recovered.open("sparse",O_RDONLY); std::array<char,32> buffer{};
        check(::fgetxattr(sparse_result.get(),"user.binary",buffer.data(),buffer.size())==static_cast<ssize_t>(attribute.size()) && std::string(buffer.data(),attribute.size())==attribute,"Binary xattr lost");
        check(recovered.stat("sparse").st_size==8*1024*1024 && recovered.stat("sparse").st_blocks<1000,"Sparse representation lost");
        reject([&] { ure::backup_tree_restore(store,restored,hash); },"existing-target");
        reject([&] { ure::backup_tree_plan(root,"home/user","global-os3.0.303.0",source/"home"/"user"/"recursive"); },"recursive-backup");
        put(source/"home"/"user"/"nested"/"data","changed content\n");
        reject([&] { ure::backup_tree_capture(root,store,hash); },"stale-source");
        check(ure::backup_tree_verify(store)["verified"]==true,"Independent backup depends on changed source");
        const auto original_plan=load(store); auto malformed=original_plan; malformed["entries"]=original_plan["entries"].asUInt()+1; reseal(store,malformed);
        // A coherent state binding is required before a forged plan can be inspected.
        auto state=ure::json_file(store/"state.json"); state["plan_sha256"]=load(store)["plan_sha256"]; ure::save_json(store/"state.json",state,true);
        reject([&] { ure::backup_tree_verify(store); },"invalid-tree");
        ure::save_json(store/"plan.json",original_plan,true); state["plan_sha256"]=hash; ure::save_json(store/"state.json",state,true);
        const auto first_page=ure::json_file(store/"entries-0.json"); std::string blob;
        for(const auto& e:first_page)if(e["kind"]=="file" && e["bytes"].asUInt64()>0) { blob="data-"+e["sha256"].asString()+".bin"; break; }
        check(!blob.empty(),"Missing fixture blob"); put(store/blob,"corruption");
        reject([&] { ure::backup_tree_verify(store); },"tree-corrupt");
        reject([&] { ure::backup_tree_restore(store,work.path/"unsafe-output",hash); },"tree-corrupt");
        check(!ure::fs::exists(work.path/"unsafe-output"),"Corrupt backup created destination");
        const auto many=work.path/"many",many_store=work.path/"many-store"; ure::fs::create_directory(many);
        for(unsigned i=0;i<300;++i)put(many/("file-"+std::to_string(i)),"content"+std::to_string(i));
        ure::Root many_root(many); const auto paged=ure::backup_tree_plan(many_root,".","global-os3.0.303.0",many_store);
        check(paged["entries"].asUInt()==301 && paged["page_count"].asUInt()>=2,"Manifest pagination missing");
        check(ure::backup_tree_capture(many_root,many_store,paged["plan_sha256"].asString())["verified"]==true,"Paged capture failed");
        check(ure::backup_tree_verify(many_store)["verified"]==true,"Paged verify failed");
        std::cout<<"Tree metadata, hardlinks, opaque names, sparse data, capture/resume, pagination, independent verification, restore and refusals passed; synthetic directories only.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
