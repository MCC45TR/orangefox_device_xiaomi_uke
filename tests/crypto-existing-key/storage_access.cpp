// SPDX-License-Identifier: Apache-2.0
// Real bounded reader with inert KeyMint/secret-decrypt boundaries.
#undef open
#undef openat
#undef fstat
#undef fstatvfs
#undef fstatat
#undef pread
#include <iostream>
using namespace android::vold;
namespace fs=std::filesystem;
int main(int argc,char** argv) {
    assert(argc==2);
    const fs::path root=argv[1];
    fs::create_directories(root);
    const auto save=[&](const char* name,const std::string& value) {
        std::ofstream output(root/name,std::ios::binary); output<<value; assert(output.good());
    };
    save("version","1"); save("secdiscardable",std::string(16384,'s'));
    save("encrypted_key",std::string(12,'n')+std::string(48,'c'));
    save("keymaster_key_blob",std::string(32,'b'));
    KeyBuffer key;
    io_fixture::reset(-1); fixture::reset();
    assert(retrieveExistingKeyReadOnly(root,KeyAuthentication(""),&key) && key==KeyBuffer(32,7));
    assert(fixture::count("begin")==1);
    const auto keymint_calls=fixture::calls;
    assert(retrieveExistingKeyReadOnly(root,KeyAuthentication("right"),&key) && key==KeyBuffer(32,9));
    assert(fixture::calls==keymint_calls);
    assert(!retrieveExistingKeyReadOnly(root,KeyAuthentication("wrong"),&key) && key.empty());
    io_fixture::directory_readonly=false;
    assert(!retrieveExistingKeyReadOnly(root,KeyAuthentication("right"),&key));
    io_fixture::directory_readonly=true; io_fixture::file_readonly=false;
    assert(!retrieveExistingKeyReadOnly(root,KeyAuthentication("right"),&key));
    io_fixture::file_readonly=true;
    save("keymaster_key_blob_upgraded","pending");
    assert(!retrieveExistingKeyReadOnly(root,KeyAuthentication("right"),&key));
    fs::remove(root/"keymaster_key_blob_upgraded");
    fs::rename(root/"encrypted_key",root/"encrypted_key.real");
    fs::create_symlink("encrypted_key.real",root/"encrypted_key");
    assert(!retrieveExistingKeyReadOnly(root,KeyAuthentication("right"),&key));
    fs::remove(root/"encrypted_key"); fs::rename(root/"encrypted_key.real",root/"encrypted_key");
    save("version","2");
    assert(!retrieveExistingKeyReadOnly(root,KeyAuthentication("right"),&key));
    key.assign(32,9);
    assert(!retrieveExistingKeyReadOnly(root,KeyAuthentication(std::string(65537,'s')),&key) && key.empty());
    fs::create_directory_symlink(root,root.string()+"-link");
    assert(!retrieveExistingKeyReadOnly(root.string()+"-link",KeyAuthentication("right"),&key));
    fs::remove(root.string()+"-link");
    std::cout<<"PASS actual existing storage-key reader: KeyMint/secret dispatch, unauthenticated-output clearing, read-only admission, pending upgrade, version and symlink refusals.\n";
}
