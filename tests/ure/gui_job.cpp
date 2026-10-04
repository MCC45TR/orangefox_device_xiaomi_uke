// SPDX-License-Identifier: Apache-2.0
// Portable executor contract only. This is not actual GUI/frame acceptance.
#include "gui_job.hpp"
#include "lifecycle_policy.hpp"
#include "operation_lease.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <mutex>
#include <poll.h>
#include <thread>
#include <unistd.h>
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
struct Gate {
    std::mutex mutex; std::condition_variable event;
    std::atomic<bool> entered{false},release{false}; std::atomic<int> descriptor{-1};
};
ure::Value collect(ure::GuiJobExecutor& executor,std::uint64_t epoch) {
    for(unsigned attempt=0;attempt<1000;++attempt) {
        const auto result=executor.collect(epoch); if(result["ready"]==true)return result; ::poll(nullptr,0,2);
    }
    throw std::runtime_error("Owned executor did not finish its bounded fixture");
}
template<class Call> void rejected(Call call,const char* expected) {
    bool refused=false; try { call(); } catch(const ure::Error& error) { refused=error.code==expected; } check(refused,"Unexpected executor admission/refusal code");
}
}
int main(int argc,char** argv) {
    auto pattern=(ure::fs::path(argc>1 ? argv[1] : ".")/"ure-gui-job-XXXXXX").string();
    std::vector<char> text(pattern.begin(),pattern.end()); text.push_back('\0'); const auto* temporary=::mkdtemp(text.data()); if(!temporary)return 1;
    const ure::fs::path work(temporary);
    try {
        ure::Fd source(::open((work/"source").c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600));
        check(source.get()>=0 && ::write(source.get(),"immutable descriptor\n",21)==21,"Cannot create job descriptor fixture");
        ure::GuiJobExecutor executor; auto gate=std::make_shared<Gate>(); ure::Value input; input["selection"]="original";
        const auto id=executor.start(input,12,{source.get()},true,[gate](const ure::GuiJobRequest& request,ure::GuiJobControl& control) {
            const auto fd=request.descriptor(0); gate->descriptor=fd;
            check((::fcntl(fd,F_GETFD)&FD_CLOEXEC)!=0,"Owned descriptor can leak across exec");
            gate->entered=true; gate->event.notify_all();
            for(;;) {
                control.checkpoint("blocked-native-call",0,0,false);
                std::unique_lock<std::mutex> lock(gate->mutex); if(gate->release)break; gate->event.wait_for(lock,std::chrono::milliseconds(2));
            }
            char contents[22]{}; check(::pread(fd,contents,21,0)==21,"Frozen descriptor did not survive caller closure");
            ure::Value result; result["selection"]=request.inputs()["selection"]; result["contents"]=contents;
            result["cancel_observed"]=control.cancellation_requested(); return result;
        });
        input["selection"]="changed"; source=ure::Fd();
        for(unsigned attempt=0;!gate->entered && attempt<1000;++attempt)::poll(nullptr,0,2);
        check(gate->entered,"Worker never entered its separate native call");
        check(ure::runtime_active_jobs()==1,"Executor did not retain global activity ownership");
        for(const auto* transition:{"mount","unmount","reboot","shutdown"}) {
            ure::LegacyLifecycleGuard lifecycle(transition); check(!lifecycle.active(),"Blocked GUI work allowed a stock lifecycle transition");
        }
        rejected([&] { static_cast<void>(ure::LifecycleLease::acquire("reboot")); },"gui-lifecycle-busy");
        rejected([&] { static_cast<void>(executor.start(input,13,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-busy");
        rejected([&] { static_cast<void>(executor.request_cancel(std::string(32,'0'))); },"gui-job-mismatch");
        std::uint64_t max_status=0;
        for(unsigned count=0;count<500;++count) {
            const auto started=ure::monotonic_ms(); const auto status=executor.status(); max_status=std::max(max_status,ure::monotonic_ms()-started);
            check(status["active"]==true && status["job_id"]==id && status["physical_test_record"]==false,"Blocked work lost its owned state");
        }
        const auto started=ure::monotonic_ms(); const auto ack=executor.request_cancel(id); const auto cancel_ms=ure::monotonic_ms()-started;
        check(max_status<250 && cancel_ms<250 && ack["acknowledgement"]=="ADVISORY_FLAG_ONLY" && ack["backend_cleanup_verified"]==false,
            "Status/cancel waited for the blocked worker or claimed backend cleanup");
        check(executor.status()["active"]==true,"Advisory cancellation pretended to interrupt native I/O");
        gate->release=true; gate->event.notify_all(); const auto result=collect(executor,13);
        check(result["state"]=="RETURNED" && result["apply_to_current_view"]==false && result["output"]["selection"]=="original" &&
            result["output"]["contents"]=="immutable descriptor\n" && result["output"]["cancel_observed"]==true && result["backend_cleanup_verified"]==false,
            "Stale completion changed the active view, reread live inputs or invented cancellation cleanup");
        check(::fcntl(gate->descriptor,F_GETFD)<0 && errno==EBADF,"Job exposed completion before releasing its descriptor");
        check(ure::runtime_active_jobs()==0,"Job exposed completion before retiring runtime activity");
        check(executor.collect(13)["ready"]==false,"Completion was delivered twice");
        auto cooperative=std::make_shared<Gate>();
        const auto cancellable=executor.start(input,14,{},true,[cooperative](const auto&,auto& control) {
            cooperative->entered=true;
            for(;;) { control.checkpoint("readonly-checkpoint",0,0,true); ::poll(nullptr,0,2); }
            return ure::Value();
        });
        for(unsigned attempt=0;!cooperative->entered && attempt<1000;++attempt)::poll(nullptr,0,2);
        check(cooperative->entered,"Cooperative fixture did not enter"); static_cast<void>(executor.request_cancel(cancellable));
        check(collect(executor,14)["state"]=="STOPPED_AT_CHECKPOINT","Caller-reviewed checkpoint did not stop owned work");
        static_cast<void>(executor.start(input,15,{},false,[](const auto&,auto&) { ure::Value result; result["large"]=std::string(300*1024,'R'); return result; }));
        const auto large=collect(executor,15); check(large["state"]=="OUTPUT_LIMIT" && large["output"]["backend_not_started"]==false &&
            large["backend_cleanup_verified"]==false,"Output overflow claimed the backend was not run or cleaned");
        static_cast<void>(executor.start(input,15,{},false,[](const auto&,auto&) { return ure::Value(); }));
        const auto missing=collect(executor,15); check(missing["state"]=="FAILED" && missing["output"]["error"]["code"]=="invalid-gui-job-result",
            "A null backend output broke or silently consumed completion parsing");
        ure::Value nonfinite; nonfinite["number"]=std::numeric_limits<double>::quiet_NaN();
        rejected([&] { static_cast<void>(executor.start(nonfinite,16,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-value-limit");
        ure::Value malformed; malformed["text"]=std::string("\xf0\x9f",2);
        rejected([&] { static_cast<void>(executor.start(malformed,16,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-value-limit");
        malformed=ure::Value(Json::objectValue); malformed[std::string("\xc0\xaf",2)]=true;
        rejected([&] { static_cast<void>(executor.start(malformed,16,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-value-limit");
        ure::Value oversized; oversized["nul"]=std::string(4*1024*1024,'\0');
        rejected([&] { static_cast<void>(executor.start(oversized,16,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-value-limit");
        oversized=ure::Value(Json::objectValue); oversized["escaped"]=std::string(4*1024*1024,'\x01');
        rejected([&] { static_cast<void>(executor.start(oversized,16,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-value-limit");
        ure::Value nesting; auto* cursor=&nesting;
        for(unsigned depth=0;depth<40;++depth)cursor=&(*cursor)["child"];
        rejected([&] { static_cast<void>(executor.start(nesting,16,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-value-limit");
        rejected([&] { static_cast<void>(executor.start(input,16,{-1},false,[](const auto&,auto&) { return ure::Value(); })); },"invalid-job-descriptor");
        auto shutdown=std::make_shared<Gate>();
        const auto noninterruptible=executor.start(input,17,{},false,[shutdown](const auto&,auto&) {
            shutdown->entered=true; while(!shutdown->release)::poll(nullptr,0,2); ure::Value result; result["native_returned"]=true; return result;
        });
        for(unsigned attempt=0;!shutdown->entered && attempt<1000;++attempt)::poll(nullptr,0,2);
        check(shutdown->entered,"Noninterruptible fixture did not enter"); rejected([&] { static_cast<void>(executor.request_cancel(noninterruptible)); },"gui-job-cancel-unavailable");
        std::atomic<bool> joined{false},second_joined{false}; std::thread owner([&] { executor.shutdown(); joined=true; });
        std::thread second_owner([&] { executor.shutdown(); second_joined=true; });
        ::poll(nullptr,0,20); const bool retained=!joined && !second_joined && executor.status()["active"]==true;
        shutdown->release=true; owner.join(); second_owner.join(); check(retained,"Concurrent shutdown detached or discarded blocked native work");
        check(joined && executor.status()["active"]==false,"Owner shutdown did not join native work");
        check(ure::runtime_active_jobs()==0,"Joined teardown left registered work");
        check(collect(executor,17)["output"]["native_returned"]==true,"Shutdown lost its inspectable completion");
        rejected([&] { static_cast<void>(executor.start(input,18,{},false,[](const auto&,auto&) { return ure::Value(); })); },"gui-job-busy");
        ure::fs::remove_all(work);
        std::cout<<"PASS owned executor contract: frozen inputs/fds, short status/cancel locks (max_status_ms="<<max_status<<", cancel_ms="<<cancel_ms<<
            "), exact job matching, stale-view refusal, explicit checkpoint stop, bounded NUL/depth/output, single completion and joined noninterruptible shutdown. Actual GUI integration and storage cleanup acceptance remain open.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\nPrivate executor fixture retained: "<<work<<'\n'; return 1; }
}
