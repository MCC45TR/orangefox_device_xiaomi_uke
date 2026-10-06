#pragma once
#include <fixture-external.hpp>
#include <vector>
namespace android::fs_mgr {
struct FstabEntry { std::string mount_point,blk_device; };
using Fstab=std::vector<FstabEntry>;
inline bool ReadDefaultFstab(Fstab* result) { *result={{"/misc",fixture::misc_path}}; return true; }
}
