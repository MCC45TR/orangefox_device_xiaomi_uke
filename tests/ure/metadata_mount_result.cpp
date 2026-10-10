// SPDX-License-Identifier: Apache-2.0
// The production completion block is extracted by the host runner.
#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#define LOG(level) std::clog
static bool mounted=false;
static unsigned mounts=0;
static std::vector<std::pair<std::string,std::string>> properties;
static bool mount_via_fs_mgr(const char* path,const char* block,bool encrypt) {
    assert(std::string(path)=="/data" && std::string(block)=="/dev/block/dm-test" && !encrypt);
    ++mounts; return mounted;
}
namespace android::base {
bool SetProperty(const std::string& name,const std::string& value) {
    properties.emplace_back(name,value); return true;
}
}
static bool complete() {
    const std::string mount_point="/data",crypto_blkdev="/dev/block/dm-test";
    const bool needs_encrypt=false;
#include "metadata-mount-completion.inc"
int main() {
    assert(!complete() && mounts==1 && properties.empty());
    mounted=true;
    assert(complete() && mounts==2);
    assert((properties==std::vector<std::pair<std::string,std::string>>{
        {"ro.crypto.fs_crypto_blkdev","/dev/block/dm-test"},
        {"ro.crypto.metadata.enabled","true"}}));
    std::cout<<"PASS metadata mount failure leaves success properties unpublished; successful completion preserves both properties.\n";
}
