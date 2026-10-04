// SPDX-License-Identifier: Apache-2.0
#include "gui_job.hpp"
#include "job_registry.hpp"
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace ure {
namespace {
constexpr std::size_t input_limit=16*1024*1024,output_limit=256*1024;
void bounded(const Value& value,unsigned depth,std::size_t& nodes,std::size_t& bytes,std::size_t limit) {
    require(depth<=16 && ++nodes<=131072,"gui-job-value-limit","Job values exceed their node/depth budget");
    require(!value.isDouble() || std::isfinite(value.asDouble()),"gui-job-value-limit","Nonfinite job numbers cannot be serialized as a different value");
    auto account=[&](std::size_t amount) { require(amount<=limit-bytes,"gui-job-value-limit","Job values exceed their byte budget"); bytes+=amount; };
    auto text=[&](const char* first,const char* last) {
        require(utf8(std::string_view(first,static_cast<std::size_t>(last-first))),"gui-job-value-limit","Job strings and keys require complete valid UTF-8 text");
        account(2);
        for(;first!=last;++first) { const auto byte=static_cast<unsigned char>(*first); account(byte<32 ? 6 : byte=='"' || byte=='\\' ? 2 : 1); }
    };
    if(value.isString()) {
        const char* first=nullptr; const char* last=nullptr;
        require(value.getString(&first,&last),"gui-job-value-limit","Cannot inspect the complete job string");
        text(first,last);
    }
    else if(value.isObject() || value.isArray()) {
        account(2);
        for(auto it=value.begin();it!=value.end();++it) {
            account(2);
            if(value.isObject()) {
                const char* end=nullptr; const char* begin=it.memberName(&end); text(begin,end); account(1);
            }
            bounded(*it,depth+1,nodes,bytes,limit);
        }
    } else account(32);
}
std::string encode(const Value& value,std::size_t limit) {
    std::size_t nodes=0,bytes=0; bounded(value,0,nodes,bytes,limit);
    Json::StreamWriterBuilder writer; writer["indentation"]=""; writer["emitUTF8"]=true;
    auto text=Json::writeString(writer,value);
    require(text.size()<=limit,"gui-job-value-limit","Encoded job values exceed their byte budget"); return text;
}
}
struct GuiJobShared {
    mutable std::mutex mutex;
    std::atomic<bool> cancel{false};
    std::string id,worker_state="QUEUED",phase="queued",output;
    std::uint64_t epoch=0,completed=0,total=0,last_progress=0;
    bool cooperative=false,done=false,pending=true,active=true,collecting=false;
};
int GuiJobRequest::descriptor(std::size_t index) const {
    require(index<descriptors_.size(),"invalid-job-descriptor","Select a descriptor captured by this exact job"); return descriptors_[index].get();
}
bool GuiJobControl::cancellation_requested() const noexcept { return state_->cancel.load(); }
std::string GuiJobControl::job_id() const { return state_->id; }
void GuiJobControl::checkpoint(const std::string& phase,std::uint64_t completed,std::uint64_t total,bool safe_to_stop) {
    require(identifier(phase) && phase.size()<=64 && (!total || completed<=total),"invalid-job-progress","Job progress must be bounded and internally consistent");
    if(safe_to_stop && cancellation_requested())throw Error("gui-job-checkpoint-stop","The job stopped at its caller-reviewed checkpoint; inspect backend cleanup separately");
    const auto now=monotonic_ms(); std::lock_guard<std::mutex> lock(state_->mutex);
    if(state_->done || (state_->phase==phase && state_->last_progress && now-state_->last_progress<50))return;
    state_->phase=phase; state_->completed=completed; state_->total=total; state_->last_progress=now;
}
struct GuiJobExecutor::Impl {
    mutable std::mutex mutex;
    std::shared_ptr<GuiJobShared> current;
    std::thread worker;
    std::thread::id worker_id;
    std::condition_variable startup_finished;
    bool starting=false,stopping=false,joining=false;
};
GuiJobExecutor::GuiJobExecutor():impl_(std::make_unique<Impl>()) { initialize_runtime_registry_lifetime(); }
GuiJobExecutor::~GuiJobExecutor() { shutdown(); }
std::string GuiJobExecutor::start(const Value& inputs,std::uint64_t epoch,const std::vector<int>& descriptors,bool cooperative,Work work) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        require(!impl_->starting && !impl_->stopping,"gui-job-busy","Job startup or shutdown is already active");
        if(impl_->current) { std::lock_guard<std::mutex> state(impl_->current->mutex);
            require(impl_->current->done && !impl_->current->pending,"gui-job-busy","Collect the current job's completion before starting another"); }
    }
    require(inputs.isObject() && descriptors.size()<=64 && static_cast<bool>(work),"invalid-gui-job","A job needs immutable inputs, bounded descriptors and owned work");
    // Validate before copying: a caller cannot force a second unbounded value
    // allocation. The canonical size check also accounts for JSON escaping.
    static_cast<void>(encode(inputs,input_limit));
    Value frozen=inputs; std::vector<Fd> owned;
    for(const auto descriptor:descriptors) {
        Fd retained(::fcntl(descriptor,F_DUPFD_CLOEXEC,3)); require(retained.get()>=0,"invalid-job-descriptor","Cannot retain the exact job descriptor"); owned.push_back(std::move(retained));
    }
    auto selected=std::make_shared<GuiJobShared>(); selected->id=operation_id(); selected->epoch=epoch; selected->cooperative=cooperative;
    auto activity=RuntimeActivityLease::acquire(selected->id.c_str());
    require(activity.acquired() && activity.valid(),activity.error()[0] ? activity.error() : "gui-registry-domain-changed",
        "The global runtime registry must admit this job before any worker starts");
    std::thread previous;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        require(!impl_->starting && !impl_->stopping,"gui-job-busy","Job startup or shutdown is already active");
        if(impl_->current) { std::lock_guard<std::mutex> state(impl_->current->mutex);
            require(impl_->current->done && !impl_->current->pending,"gui-job-busy","Collect the current job's completion before starting another"); }
        impl_->starting=true; previous=std::move(impl_->worker); impl_->current=selected;
    }
    if(previous.joinable())previous.join();
    try {
        std::thread launched([selected,activity=std::move(activity),request=GuiJobRequest(std::move(frozen),std::move(owned)),task=std::move(work)]() mutable {
            std::string output,state="RETURNED"; bool started=false;
            try {
                GuiJobControl control(selected);
                control.checkpoint("before-backend",0,0,true);
                require(activity.valid(),"gui-registry-domain-changed","The job's runtime registry changed before backend admission");
                { std::lock_guard<std::mutex> lock(selected->mutex); selected->worker_state="RUNNING"; }
                started=true; auto result=task(request,control);
                require(result.isObject(),"invalid-gui-job-result","A backend must return an explicit object result");
                output=encode(result,output_limit);
            } catch(const Error& error) {
                state=error.code=="gui-job-checkpoint-stop" ? "STOPPED_AT_CHECKPOINT" : error.code=="gui-job-value-limit" ? "OUTPUT_LIMIT" : "FAILED";
                const auto message=std::string(error.what()).substr(0,4096);
                Value failure; failure["error"]["code"]=identifier(error.code) && error.code.size()<=128 ? error.code : "invalid-backend-error";
                failure["error"]["message"]=utf8(message) ? message : "Backend error text exceeds the safe UTF-8 publication contract; inspect its private journal";
                failure["backend_not_started"]=!started; failure["backend_cleanup_verified"]=false; output=json(failure);
            } catch(...) {
                state="FAILED"; Value failure; failure["error"]["code"]="unexpected-gui-job-failure";
                failure["backend_cleanup_verified"]=false; output=json(failure);
            }
            // Destroy callback captures and captured descriptors before making
            // completion observable. Worker completion alone is never a proof
            // of a backend's mount, storage or durable-owner cleanup.
            task={}; request=GuiJobRequest{}; activity=RuntimeActivityLease{};
            std::lock_guard<std::mutex> lock(selected->mutex);
            selected->worker_state=state; selected->phase="finished"; selected->output=std::move(output);
            selected->done=true; selected->active=false;
        });
        std::lock_guard<std::mutex> lock(impl_->mutex); impl_->worker=std::move(launched); impl_->worker_id=impl_->worker.get_id(); impl_->starting=false; impl_->startup_finished.notify_all();
    } catch(...) {
        std::lock_guard<std::mutex> lock(impl_->mutex); impl_->starting=false; impl_->startup_finished.notify_all();
        std::lock_guard<std::mutex> state(selected->mutex); selected->worker_state="START_FAILED"; selected->done=true; selected->active=false;
        Value failure; failure["error"]["code"]="gui-job-start-failed"; selected->output=json(failure); throw;
    }
    return selected->id;
}
Value GuiJobExecutor::status() const {
    std::shared_ptr<GuiJobShared> selected; bool stopping=false;
    { std::lock_guard<std::mutex> lock(impl_->mutex); selected=impl_->current; stopping=impl_->stopping; }
    Value result; result["schema"]=1; result["stopping"]=stopping; result["physical_test_record"]=false;
    result["local_registered_jobs"]=Json::UInt64(runtime_active_jobs());
    result["lifecycle_exclusion_kind"]="COOPERATING_RUNTIME_FLOCK";
    if(!selected) { result["state"]="IDLE"; result["active"]=false; return result; }
    std::lock_guard<std::mutex> lock(selected->mutex);
    result["job_id"]=selected->id; result["view_epoch"]=Json::UInt64(selected->epoch); result["state"]=selected->worker_state;
    result["active"]=selected->active; result["result_pending"]=selected->done && selected->pending;
    result["phase"]=selected->phase; result["completed"]=Json::UInt64(selected->completed); result["total"]=Json::UInt64(selected->total);
    result["cooperative_cancel_available"]=selected->cooperative; result["cancel_requested"]=selected->cancel.load();
    result["backend_cleanup_verified"]=false; result["output_limit_bytes"]=Json::UInt64(output_limit);
    return result;
}
Value GuiJobExecutor::request_cancel(const std::string& id) {
    std::shared_ptr<GuiJobShared> selected; { std::lock_guard<std::mutex> lock(impl_->mutex); selected=impl_->current; }
    require(selected!=nullptr,"gui-job-missing","There is no owned job to cancel");
    std::lock_guard<std::mutex> lock(selected->mutex);
    require(selected->id==id,"gui-job-mismatch","Cancellation must identify the exact current job");
    require(!selected->done && selected->cooperative,"gui-job-cancel-unavailable","This backend stage does not accept cooperative cancellation");
    selected->cancel=true; Value ack; ack["state"]="CANCEL_REQUESTED"; ack["job_id"]=id;
    ack["acknowledgement"]="ADVISORY_FLAG_ONLY"; ack["backend_cleanup_verified"]=false; return ack;
}
Value GuiJobExecutor::collect(std::uint64_t epoch) {
    std::shared_ptr<GuiJobShared> selected; { std::lock_guard<std::mutex> lock(impl_->mutex); selected=impl_->current; }
    Value result; result["ready"]=false; if(!selected)return result;
    std::string encoded;
    {
        std::lock_guard<std::mutex> lock(selected->mutex); if(!selected->done || !selected->pending || selected->collecting)return result;
        selected->collecting=true;
        result["job_id"]=selected->id; result["view_epoch"]=Json::UInt64(selected->epoch); result["state"]=selected->worker_state;
        result["apply_to_current_view"]=selected->epoch==epoch; result["cancel_requested"]=selected->cancel.load();
        encoded=selected->output;
    }
    try { result["output"]=parse_json(encoded); }
    catch(...) { std::lock_guard<std::mutex> lock(selected->mutex); selected->collecting=false; throw; }
    {
        std::lock_guard<std::mutex> lock(selected->mutex); selected->pending=false; selected->collecting=false; selected->output.clear();
    }
    result["ready"]=true; result["backend_cleanup_verified"]=false; return result;
}
void GuiJobExecutor::shutdown(bool request_cancellation) {
    std::thread owned;
    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        impl_->startup_finished.wait(lock,[&] { return !impl_->starting; });
        require(impl_->worker_id!=std::this_thread::get_id(),"gui-job-self-join","Owned work cannot join its own supervisor");
        if(impl_->joining) { impl_->startup_finished.wait(lock,[&] { return !impl_->joining; }); return; }
        require(!impl_->worker.joinable() || impl_->worker.get_id()!=std::this_thread::get_id(),"gui-job-self-join","Owned work cannot destroy or join its own supervisor");
        impl_->stopping=true;
        if(request_cancellation && impl_->current)impl_->current->cancel=true;
        owned=std::move(impl_->worker);
        impl_->joining=owned.joinable();
    }
    if(owned.joinable())owned.join();
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->worker_id={}; impl_->joining=false; impl_->startup_finished.notify_all(); }
}
} // namespace ure
