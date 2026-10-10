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
    int directory=-1, cases=0;
    const auto check=[&](bool result) { assert(result); ++cases; };
    const auto save=[&](const char* name,const std::string& bytes) {
        std::ofstream stream(root/name,std::ios::binary|std::ios::trunc);
        stream.write(bytes.data(),bytes.size()); assert(stream.good());
    };
    const auto reset=[&]() {
        if(directory>=0) ::close(directory);
        fs::remove_all(root); fs::create_directories(root);
        save("version","1"); save("secdiscardable",std::string(16384,'s'));
        save("encrypted_key",std::string(12,'n')+std::string(48,'c'));
        save("keymaster_key_blob",std::string(32,'b'));
        directory=::open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        assert(directory>=0); io_fixture::reset(directory); fixture::reset();
        storage_binding_info.state=StorageBindingInfo::State::UNINITIALIZED;
        storage_binding_info.seed.clear();
    };
    KeyBuffer key;
    const auto failure=[&](MetadataKeyBinding binding=MetadataKeyBinding::Unbound) {
        key.assign(16,'p');
        check(!retrieveMetadataKeyReadOnly(directory,binding,&key) && key.empty());
    };
    reset();
    check(retrieveMetadataKeyReadOnly(directory,MetadataKeyBinding::Unbound,&key) && key==KeyBuffer(32,7));
    check(storage_binding_info.state==StorageBindingInfo::State::UNINITIALIZED);
    check(io_fixture::opens==4 && fixture::count("begin")==1 && fixture::count("finish")==1);
    check(!retrieveMetadataKeyReadOnly(directory,MetadataKeyBinding::Unbound,nullptr));
    check(!retrieveMetadataKeyReadOnly(-1,MetadataKeyBinding::Unbound,&key) && key.empty());
    reset(); failure(MetadataKeyBinding::Unknown); check(io_fixture::opens==0);
    reset(); failure(static_cast<MetadataKeyBinding>(9)); check(io_fixture::opens==0);
    reset(); io_fixture::directory_owner=false; failure();
    reset(); io_fixture::file_owner=false; failure();
    reset(); io_fixture::directory_readonly=false; failure();
    reset(); io_fixture::file_readonly=false; failure();
    reset(); io_fixture::directory_stat_error=true; failure();
    reset(); io_fixture::statvfs_error=true; failure();
    reset();
    const int regular=::open((root/"version").c_str(),O_RDONLY);
    check(!retrieveMetadataKeyReadOnly(regular,MetadataKeyBinding::Unbound,&key) && key.empty());
    ::close(regular);
    for(const char* name : {"version","secdiscardable","encrypted_key","keymaster_key_blob"}) {
        reset(); fs::remove(root/name); failure();
    }
    reset(); fs::rename(root/"version",root/"version.real");
    fs::create_symlink("version.real",root/"version"); failure();
    reset(); fs::create_hard_link(root/"keymaster_key_blob",root/"duplicate"); failure();
    reset(); fs::remove(root/"version"); fs::create_directory(root/"version"); failure();
    reset(); fs::remove(root/"version"); assert(::mkfifo((root/"version").c_str(),0600)==0); failure();
    for(const std::string& version : {std::string{},std::string("2"),std::string("1\n")}) {
        reset(); save("version",version); failure();
    }
    for(size_t size : {size_t(0),size_t(16383),size_t(16385)}) {
        reset(); save("secdiscardable",std::string(size,'s')); failure();
    }
    for(size_t size : {size_t(0),size_t(28),size_t(65537)}) {
        reset(); save("encrypted_key",std::string(size,'c')); failure();
    }
    for(size_t size : {size_t(0),size_t(65537)}) {
        reset(); save("keymaster_key_blob",std::string(size,'b')); failure();
    }
    reset(); save("keymaster_key_blob",std::string(65536,'b'));
    check(retrieveMetadataKeyReadOnly(directory,MetadataKeyBinding::Unbound,&key));
    reset(); save("keymaster_key_blob_upgraded","pending"); failure();
    reset(); fs::create_symlink("missing",root/"keymaster_key_blob_upgraded"); failure();
    reset(); io_fixture::pending_error=true; failure();
    for(int field : {1,2,3,4}) {
        reset(); io_fixture::changed_field=field; failure();
    }
    for(int error : {1,2}) {
        reset(); io_fixture::read_error=error; failure();
    }
    reset(); io_fixture::maximum_read=3;
    check(retrieveMetadataKeyReadOnly(directory,MetadataKeyBinding::Unbound,&key));
    for(int interruptions : {1,8}) {
        reset(); io_fixture::interrupted=interruptions;
        check(retrieveMetadataKeyReadOnly(directory,MetadataKeyBinding::Unbound,&key));
    }
    reset(); io_fixture::interrupted=9; failure(); check(io_fixture::reads<=12);
    reset(); auto device=std::static_pointer_cast<fixture::Device>(fixture::connected);
    device->operation->status=-30; failure();
    check(fixture::count("finish")==1 && fixture::count("abort")==1);
    reset(); device=std::static_pointer_cast<fixture::Device>(fixture::connected);
    device->begin_status=-62; failure();
    check(fixture::count("begin")==1 && fixture::count("finish")==0);
    reset(); failure(MetadataKeyBinding::Bound);
    check(storage_binding_info.state==StorageBindingInfo::State::UNINITIALIZED && fixture::checks==0);
    reset(); storage_binding_info.state=StorageBindingInfo::State::IN_USE;
    storage_binding_info.seed.assign(32,'z');
    check(retrieveMetadataKeyReadOnly(directory,MetadataKeyBinding::Bound,&key));
    device=std::static_pointer_cast<fixture::Device>(fixture::connected);
    std::vector<uint8_t> bound_id(64,'h'); bound_id.insert(bound_id.end(),32,'z');
    check(device->last_app_id==bound_id);
    check(storage_binding_info.state==StorageBindingInfo::State::IN_USE &&
          storage_binding_info.seed==std::vector<uint8_t>(32,'z'));
    failure(MetadataKeyBinding::Unbound);
    reset(); storage_binding_info.state=StorageBindingInfo::State::IN_USE;
    failure(MetadataKeyBinding::Bound);
    reset(); storage_binding_info.state=StorageBindingInfo::State::IN_USE;
    storage_binding_info.seed.assign(65473,'z'); failure(MetadataKeyBinding::Bound);
    reset(); storage_binding_info.state=StorageBindingInfo::State::NOT_USED;
    storage_binding_info.seed.assign(32,'z'); failure(MetadataKeyBinding::Bound);
    check(storage_binding_info.state==StorageBindingInfo::State::NOT_USED);
    reset(); storage_binding_info.state=StorageBindingInfo::State::NOT_USED;
    check(retrieveMetadataKeyReadOnly(directory,MetadataKeyBinding::Unbound,&key));
    check(storage_binding_info.state==StorageBindingInfo::State::NOT_USED);
    ::close(directory); fs::remove_all(root);
    std::cout << cases << " extracted metadata-reader controls passed.\n";
}
