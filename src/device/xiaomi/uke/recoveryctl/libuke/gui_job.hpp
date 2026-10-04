// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"
#include <functional>

namespace ure {
struct GuiJobShared;
class GuiJobRequest {
    Value inputs_;
    std::vector<Fd> descriptors_;
public:
    GuiJobRequest()=default;
    GuiJobRequest(Value inputs,std::vector<Fd> descriptors):inputs_(std::move(inputs)),descriptors_(std::move(descriptors)) {}
    const Value& inputs() const { return inputs_; }
    int descriptor(std::size_t index) const;
};
class GuiJobControl {
    std::shared_ptr<GuiJobShared> state_;
public:
    explicit GuiJobControl(std::shared_ptr<GuiJobShared> state):state_(std::move(state)) {}
    bool cancellation_requested() const noexcept;
    // Callers explicitly mark a checkpoint as safe for cooperative stopping.
    // A stop is not a storage rollback or permission to retire an owner.
    void checkpoint(const std::string& phase,std::uint64_t completed=0,std::uint64_t total=0,bool safe_to_stop=false);
};
class GuiJobExecutor {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    using Work=std::function<Value(const GuiJobRequest&,GuiJobControl&)>;
    GuiJobExecutor();
    ~GuiJobExecutor();
    GuiJobExecutor(const GuiJobExecutor&)=delete;
    GuiJobExecutor& operator=(const GuiJobExecutor&)=delete;
    // Inputs are copied and descriptors duplicated before the task starts.
    // Work must capture its other inputs by value and never access GUI state.
    std::string start(const Value& inputs,std::uint64_t view_epoch,
        const std::vector<int>& descriptors,bool cooperative_cancellation,Work work);
    Value status() const;
    Value request_cancel(const std::string& job_id);
    // A completion belonging to an old view remains inspectable but cannot
    // apply its selections/review/output to the new view.
    Value collect(std::uint64_t current_view_epoch);
    // Join owned work; no detached worker can outlive its roots or GUI owner.
    // A noninterruptible backend may delay shutdown; never fabricate cleanup.
    void shutdown();
};
} // namespace ure
