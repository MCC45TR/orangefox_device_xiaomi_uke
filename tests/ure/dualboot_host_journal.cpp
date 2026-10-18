// SPDX-License-Identifier: Apache-2.0
// Disposable files and inherited sockets only; no host block device is opened.
#define main prior_device_tests_main
#include "dualboot_device.cpp"
#undef main
#define UKE_JOURNAL_RELAY_TEST
#include "../../src/device/xiaomi/uke/recoveryctl/journal-relay.cpp"
#include <sys/socket.h>
#include <sys/wait.h>
#include <chrono>
#include <thread>

namespace {
void timeout_trials() {
    int pipes[2]; check(::socketpair(AF_UNIX,SOCK_STREAM,0,pipes)==0,"Cannot create stalled transport sockets");
    ure::Fd sender(pipes[0]),receiver(pipes[1]); const int budget=4096;
    check(::setsockopt(sender.get(),SOL_SOCKET,SO_SNDBUF,&budget,sizeof(budget))==0,"Cannot bound transport buffer");
    const auto flags=::fcntl(sender.get(),F_GETFL); const auto start=ure::monotonic_ms();
    reject([&]{ure::dualboot_pipe_write(sender.get(),std::string(1024*1024,'Q'),100);},"host-journal-disconnected");
    check(ure::monotonic_ms()-start<1000 && ::fcntl(sender.get(),F_GETFL)==flags,"Stalled write exceeded its deadline or changed descriptor flags");
    for(unsigned mode=0;mode<3;++mode) {
        int slow[2]; check(::socketpair(AF_UNIX,SOCK_STREAM,0,slow)==0,"Cannot create slow transport sockets");
        const auto child=::fork(); check(child>=0,"Cannot fork slow checkpoint peer");
        if(child==0) {
            ::close(slow[0]);
            (void)ure::dualboot_pipe_read(slow[1],1);
            for(const auto* part:{"A","B","\n"}) { ure::dualboot_pipe_write(slow[1],part); std::this_thread::sleep_for(std::chrono::milliseconds(70)); }
            ::close(slow[1]); ::_exit(0);
        }
        ::close(slow[1]); ure::Fd peer(slow[0]); const auto begun=ure::monotonic_ms();
        ure::dualboot_pipe_write(peer.get(),"x");
        reject([&]{
            if(mode==0)(void)ure::dualboot_pipe_line(peer.get(),16,100);
            else if(mode==1)(void)ure::dualboot_pipe_read(peer.get(),3,100);
            else (void)ure::dualboot_pipe_completion(peer.get(),100);
        },"host-journal-disconnected");
        check(ure::monotonic_ms()-begun<1000,"Partial traffic renewed a whole-message deadline");
        int status=0; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"Slow transport fixture failed");
    }
}
void relay_trial(const ure::Value& snapshot,const std::string& hash,bool bad_completion) {
    auto pattern=(ure::fs::current_path()/"relay-XXXXXX").string(); const auto* made=::mkdtemp(pattern.data());
    check(made,"Cannot create host filesystem fixture"); const ure::fs::path directory=made;
    int pipes[2]; check(::socketpair(AF_UNIX,SOCK_STREAM,0,pipes)==0,"Cannot create receiver sockets");
    const auto child=::fork(); check(child>=0,"Cannot fork checkpoint receiver");
    if(child==0) {
        ::close(pipes[0]); ::dup2(pipes[1],STDIN_FILENO); ::dup2(pipes[1],STDOUT_FILENO); ::close(pipes[1]);
        try { ::_exit(ure::journal_relay(directory/"received",hash)); }
        catch(const ure::Error& error) { ::_exit(error.code=="incomplete-host-operation" ? 42 : 43); }
        catch(...) { ::_exit(44); }
    }
    ::close(pipes[1]); ure::Fd peer(pipes[0]); const auto data=ure::json(snapshot);
    ure::dualboot_pipe_write(peer.get(),"UKE-JOURNAL-1 "+std::to_string(data.size())+"\n"+data);
    check(ure::dualboot_pipe_line(peer.get(),256)=="UKE-SYNCED-1 "+snapshot["session"].asString()+" 1 "+hash+" "+ure::sha256(data),"Receiver acknowledged a different checkpoint");
    const auto saved=ure::private_directory(directory/"received"/"checkpoint-1",false);
    check(saved.exists("plan.json") && saved.exists("before-primary_header.bin") && saved.exists("after-backup_header.bin"),"Receiver acknowledged before publishing all recovery files");
    ure::Value result; result["plan_sha256"]=hash; result["state"]=bad_completion ? "BEFORE_GPT_VERIFIED" : "COMMITTED_REBOOT_REQUIRED";
    ure::dualboot_pipe_write(peer.get(),"UKE-JOURNAL-DONE-1\n"+ure::json(ure::envelope(result))); ::shutdown(peer.get(),SHUT_WR);
    int status=0; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==(bad_completion ? 42 : 0),"Receiver accepted an incomplete final state");
    check(ure::fs::exists(directory/"received"/"completion.json")==!bad_completion,"Receiver completion record differs");
    ure::fs::remove_all(directory);
}
void protocol_trial(const ure::Root& store,const ure::Value& state,bool wrong,bool disconnect) {
    int pipes[2]; check(::socketpair(AF_UNIX,SOCK_STREAM,0,pipes)==0,"Cannot create checkpoint sockets");
    const auto child=::fork(); check(child>=0,"Cannot fork checkpoint producer");
    if(child==0) {
        ::close(pipes[0]); ::dup2(pipes[1],STDIN_FILENO); ::dup2(pipes[1],STDOUT_FILENO); ::close(pipes[1]);
        try { ure::DualbootHostJournal host(true); host.checkpoint(store,state); ::_exit(0); }
        catch(const ure::Error& error) { std::cerr<<"Producer: "<<error.code<<": "<<error.what()<<'\n'; ::_exit(error.code==(disconnect ? "host-journal-disconnected" : "invalid-host-acknowledgement") ? 42 : 43); }
        catch(...) { ::_exit(44); }
    }
    ::close(pipes[1]); ure::Fd peer(pipes[0]);
    const auto header=ure::dualboot_pipe_line(peer.get(),128); check(header.starts_with("UKE-JOURNAL-1 "),"Missing checkpoint frame");
    const auto data=ure::dualboot_pipe_read(peer.get(),std::stoul(header.substr(14))); const auto snapshot=ure::parse_json(data);
    check(snapshot["plan_sha256"]==state["plan_sha256"] && snapshot["files"].size()==12,"Incomplete producer checkpoint");
    if(disconnect)peer=ure::Fd();
    else ure::dualboot_pipe_write(peer.get(),wrong ? "UKE-SYNCED-1 wrong\n" : "UKE-SYNCED-1 "+snapshot["session"].asString()+" 1 "+state["plan_sha256"].asString()+" "+ure::sha256(data)+"\n");
    int status=0; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==(wrong||disconnect ? 42 : 0),"Checkpoint acknowledgement did not gate the producer");
}
}
int main() {
    try {
        Workspace work; const auto disk=work.path/"layout.img"; fixture(disk); auto plan=preview(disk);
        plan["request"]["userdata_initialization"]="android-first-boot";
        plan["device_commands"]=ure::dualboot_device_commands(plan); reseal(plan); ure::device_plan(plan);
        check(plan["device_commands"][0]["tool"]=="native-bounded-zero" && plan["device_commands"][0]["erase_bytes"]==16384 &&
            plan["device_commands"][0]["view_backing"]=="original-userdata-raw-partition" && plan["device_commands"][0]["userdata_filesystem_created"]==false,
            "Deferred userdata preview accidentally formats an encryption map");
        check(ure::dualboot_preview(plan).find("f2fs (Android initialization pending)")!=std::string::npos,"Deferred preview presented userdata as a prepared F2FS filesystem");
        for(const auto* mode:{"format","","android-first-boot-unsafe"}) {
            auto bad=plan["request"]; bad["userdata_initialization"]=mode;
            reject([&]{(void)ure::dualboot_layout_request(bad);},"invalid-userdata-policy");
        }
        for(const auto* field:{"userdata_policy","userdata_filesystem"}) {
            auto bad=plan["request"]; bad[field]=std::string(field)=="userdata_policy" ? "preserve" : "ext4";
            reject([&]{(void)ure::dualboot_layout_request(bad);},"invalid-userdata-policy");
        }
        const auto raw=work.path/"raw-header"; ure::Fd raw_fd(::open(raw.c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
        const std::string sentinel(131072,'Q'); ure::write_exact(raw_fd.get(),0,sentinel); ure::erase_android_userdata_header(raw_fd.get());
        check(ure::storage_read(raw_fd.get(),0,16384)==std::string(16384,'\0') && ure::storage_read(raw_fd.get(),16384,sentinel.size()-16384)==sentinel.substr(16384),
            "Raw userdata erase crossed its exact 16 KiB boundary");
        check(::ftruncate(raw_fd.get(),8192)==0,"Cannot shorten header fixture");
        reject([&]{ure::erase_android_userdata_header(raw_fd.get());},"invalid-dualboot-range");
        const auto store=ure::private_directory(work.path/"ram-stage",true);
        store.save_record("plan.json",plan); ure::Value state; state["schema"]=1; state["format"]="ure-live-dualboot-journal";
        state["plan_sha256"]=plan["plan_sha256"]; state["phase"]="BEFORE_GPT_VERIFIED"; store.save_record("state.json",state);
        auto target=ure::storage_image(disk,4096); ure::Value generated;
        const auto after=ure::gpt_layout_regions(target,plan["gpt"]["layout"]["request"],"fixture",generated);
        for(const auto& region:after)for(const auto* version:{"before","after"}) {
            const auto bytes=std::string(version)=="before" ? ure::storage_read(target.descriptor.get(),region.offset,region.bytes.size()) : region.bytes;
            auto fd=store.open(std::string(version)+"-"+region.name+".bin",O_WRONLY|O_CREAT|O_EXCL,0600); ure::write_exact(fd.get(),0,bytes); ure::sync_fd(fd.get());
        }
        const auto host=ure::private_directory(work.path/"pc-journal",true); const std::string session="unit-test-session";
        auto snapshot=ure::dualboot_checkpoint(store,state,session,1); const auto hash=plan["plan_sha256"].asString();
        ure::persist_checkpoint(host,snapshot,ure::json(snapshot),hash,session,1);
        const auto reopened=ure::private_directory(work.path/"pc-journal"/"checkpoint-1",false);
        check(reopened.read("plan.json",1024*1024)==store.read("plan.json",1024*1024) && reopened.read("state.json",1024*1024)==store.read("state.json",1024*1024),"Synced PC journal did not reopen exactly");
        check(!reopened.exists("transport.json") && host.exists("checkpoint-1-transport.json"),"PC receipt polluted the recovery checkpoint payload");
        check(ure::dualboot_checkpoint(reopened,state,"new-session",1)["files"].size()==12,"Reimported PC checkpoint could not be retransmitted");
        reopened.save_record("transport.json",snapshot);
        const auto legacy_receipt=reopened.read("transport.json",ure::dualboot_checkpoint_limit);
        check(ure::dualboot_checkpoint(reopened,state,"new-session",1)["files"].size()==12,"Legacy checkpoint receipt prevented reimport");
        auto advanced=state; advanced["phase"]="GPT_RESTORE"; reopened.save_record("state.json",advanced,true);
        reject([&]{(void)ure::dualboot_checkpoint(reopened,advanced,"new-session",1);},"invalid-host-checkpoint");
        check(ure::dualboot_checkpoint(reopened,advanced,"new-session",2)["phase"]=="GPT_RESTORE" &&
            reopened.read("transport.json",ure::dualboot_checkpoint_limit)==legacy_receipt,"New state changed the retained legacy receipt");
        reopened.save_record("state.json",state,true);
        auto corrupt=snapshot; corrupt["files"][0]["sha256"]=std::string(64,'0'); reopened.save_record("transport.json",corrupt,true);
        reject([&]{(void)ure::dualboot_checkpoint(reopened,state,"new-session",1);},"invalid-host-checkpoint");
        reopened.save_record("transport.json",snapshot,true);
        for(unsigned trial=0;trial<6;++trial) {
            auto bad=snapshot;
            if(trial==0)bad["sequence"]=2;
            if(trial==1)bad["session"]="another-session";
            if(trial==2)bad["files"][0]["name"]="../plan.json";
            if(trial==3)bad["files"][0]["hex"]="00";
            if(trial==4)bad["files"].append(bad["files"][0]);
            if(trial==5)bad["files"][1]=bad["files"][0];
            reject([&]{ure::persist_checkpoint(host,bad,ure::json(bad),hash,session,1);},"invalid-host-checkpoint");
        }
        auto wrong_schema=snapshot;
        for(auto& row:wrong_schema["files"])if(row["name"]=="state.json") {
            auto bytes=ure::checkpoint_bytes(row); const auto position=bytes.find("\"schema\" : 1");
            check(position!=std::string::npos,"Cannot locate state schema fixture"); const auto digit=position+11;
            bytes[digit]='2'; auto hex=row["hex"].asString(); hex.replace(digit*2,2,"32"); row["hex"]=hex; row["sha256"]=ure::sha256(bytes);
        }
        reject([&]{ure::persist_checkpoint(host,wrong_schema,ure::json(wrong_schema),hash,session,1);},"invalid-host-checkpoint");
        protocol_trial(store,state,false,false); protocol_trial(store,state,true,false); protocol_trial(store,state,false,true);
        timeout_trials();
        state["phase"]="COMMITTED_REBOOT_REQUIRED"; store.save_record("state.json",state,true);
        const auto terminal=ure::dualboot_checkpoint(store,state,session,1);
        relay_trial(terminal,hash,false); relay_trial(terminal,hash,true);
        auto unexpected=store.open("unreviewed",O_WRONLY|O_CREAT|O_EXCL,0600);
        reject([&]{(void)ure::dualboot_checkpoint(store,state,session,2);},"invalid-host-checkpoint");
        std::cout<<"Deferred userdata header bounds, durable PC checkpoint reimport, legacy receipt preservation and bounded acknowledgement/disconnect gates passed.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
