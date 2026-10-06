// External HIDL types only; all actual HAL behavior is compiled from upstream.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <hidl/Status.h>
namespace android::hardware::boot::V1_0 {
enum class BoolResult:std::int32_t { FALSE=0,TRUE=1,INVALID_SLOT=-1 };
struct CommandResult { bool success{}; std::string errMsg; };
}
namespace android::hardware::boot::V1_1 {
// Exact values from the production boot/1.1/types.hal.
enum class MergeStatus:std::int32_t { NONE=0,UNKNOWN=1,SNAPSHOTTED=2,MERGING=3,CANCELLED=4 };
class IBootControl {
public:
    using markBootSuccessful_cb=std::function<void(const V1_0::CommandResult&)>;
    using setActiveBootSlot_cb=markBootSuccessful_cb;
    using setSlotAsUnbootable_cb=markBootSuccessful_cb;
    using getSuffix_cb=std::function<void(const std::string&)>;
    virtual ~IBootControl()=default;
    virtual Return<std::uint32_t> getNumberSlots()=0;
    virtual Return<std::uint32_t> getCurrentSlot()=0;
    virtual Return<void> markBootSuccessful(markBootSuccessful_cb)=0;
    virtual Return<void> setActiveBootSlot(std::uint32_t,setActiveBootSlot_cb)=0;
    virtual Return<void> setSlotAsUnbootable(std::uint32_t,setSlotAsUnbootable_cb)=0;
    virtual Return<V1_0::BoolResult> isSlotBootable(std::uint32_t)=0;
    virtual Return<V1_0::BoolResult> isSlotMarkedSuccessful(std::uint32_t)=0;
    virtual Return<void> getSuffix(std::uint32_t,getSuffix_cb)=0;
    virtual Return<bool> setSnapshotMergeStatus(MergeStatus)=0;
    virtual Return<MergeStatus> getSnapshotMergeStatus()=0;
};
}
