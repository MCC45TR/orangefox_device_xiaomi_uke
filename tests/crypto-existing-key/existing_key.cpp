#include "fake.hpp"
#include "ExistingKeyMint.h"
#include <iostream>
using android::vold::ExistingKeyMint;
using android::vold::KeyBuffer;
namespace km=aidl::android::hardware::security::keymint;

int main() {
    int cases=0;
    const auto check=[&](bool result) { assert(result); ++cases; };
    const std::string blob(32, 'b'), app_id(64, 'a'), nonce(12, 'n'), body(48, 'c');
    auto device=fixture::reset();
    ExistingKeyMint client;
    check(bool(client) && fixture::checks==1);
    KeyBuffer key(16, 's');
    check(client.decryptMetadata(blob,app_id,nonce,body,&key));
    check(key==KeyBuffer(32,7) && fixture::count("begin")==1 &&
          fixture::count("characteristics")==1 && fixture::count("finish")==1 &&
          fixture::count("abort")==0 && fixture::cleansed>=6);
    check(client.errorCode()==km::ErrorCode::OK);
    const auto previous=key;
    const std::string_view alias(key.data(),key.size());
    check(!client.decryptMetadata(alias,app_id,nonce,body,&key) && key==previous);
    check(!client.decryptMetadata(blob,alias,nonce,body,&key) && key==previous);
    check(!client.decryptMetadata(blob,app_id,alias.substr(0,12),body,&key) && key==previous);
    check(!client.decryptMetadata(blob,app_id,nonce,alias,&key) && key==previous);
    const int before=fixture::count("begin");
    check(!client.decryptMetadata(blob,app_id,nonce,body,nullptr));
    for (const auto& input : {std::string{}, std::string(65537,'b')})
        check(!client.decryptMetadata(input,app_id,nonce,body,&key) && key.empty());
    for (const auto& input : {std::string{}, std::string(65537,'a')})
        check(!client.decryptMetadata(blob,input,nonce,body,&key) && key.empty());
    for (const auto& input : {std::string(11,'n'),std::string(13,'n')})
        check(!client.decryptMetadata(blob,app_id,input,body,&key) && key.empty());
    for (const auto& input : {std::string{},std::string(16,'c'),std::string(529,'c')})
        check(!client.decryptMetadata(blob,app_id,nonce,input,&key) && key.empty());
    check(fixture::count("begin")==before);
    for (int failure : {-62,-30,1}) {
        device=fixture::reset(); device->begin_status=failure;
        ExistingKeyMint failed;
        check(!failed.decryptMetadata(blob,app_id,nonce,body,&key) && key.empty());
        check(fixture::count("begin")==1 && fixture::count("finish")==0 &&
              fixture::count("abort")==1);
        if (failure==-62) check(failed.errorCode()==km::ErrorCode::KEY_REQUIRES_UPGRADE);
    }
    device=fixture::reset(); device->missing_operation=true;
    ExistingKeyMint missing;
    check(!missing.decryptMetadata(blob,app_id,nonce,body,&key) && key.empty() &&
          fixture::count("finish")==0);
    check(missing.errorCode()==km::ErrorCode::UNKNOWN_ERROR);
    device=fixture::reset(); device->operation->status=-30;
    ExistingKeyMint unauthenticated;
    check(!unauthenticated.decryptMetadata(blob,app_id,nonce,body,&key) && key.empty());
    check(fixture::count("finish")==1 && fixture::count("abort")==1 && fixture::cleansed>=5);
    for (size_t size : {size_t(0),size_t(31),size_t(513)}) {
        device=fixture::reset(); device->operation->output.resize(size);
        ExistingKeyMint malformed;
        check(!malformed.decryptMetadata(blob,app_id,nonce,body,&key) && key.empty());
    }
    for (int version : {1,5}) {
        device=fixture::reset(); device->version=version; ExistingKeyMint other;
        check(other.errorCode()==km::ErrorCode::HARDWARE_TYPE_UNAVAILABLE);
        check(!bool(other) && !other.decryptMetadata(blob,app_id,nonce,body,&key));
        check(fixture::count("hardware")==0 && fixture::count("begin")==0);
    }
    device=fixture::reset(); device->security=km::SecurityLevel::SOFTWARE;
    ExistingKeyMint software;
    check(software.errorCode()==km::ErrorCode::HARDWARE_TYPE_UNAVAILABLE);
    check(!bool(software) && !software.decryptMetadata(blob,app_id,nonce,body,&key));
    fixture::reset(); fixture::connected=nullptr; ExistingKeyMint absent;
    check(!bool(absent) && fixture::checks==1);
    device=fixture::reset(); device->version_status=1; ExistingKeyMint failed_version;
    check(!bool(failed_version) && fixture::count("hardware")==0);
    device=fixture::reset(); device->hardware_status=-30; ExistingKeyMint failed_hardware;
    check(!bool(failed_hardware));

    for (int failure : {-62,-30,1}) {
        device=fixture::reset(); device->characteristics_status=failure;
        ExistingKeyMint failed;
        check(!failed.decryptMetadata(blob,app_id,nonce,body,&key) && key.empty());
        check(fixture::count("characteristics")==1 && fixture::count("begin")==0 &&
              fixture::count("finish")==0 && fixture::count("abort")==0);
        if (failure==-62) check(failed.errorCode()==km::ErrorCode::KEY_REQUIRES_UPGRADE);
    }
    const auto refused=[&](std::vector<km::KeyCharacteristics> characteristics) {
        device=fixture::reset(); device->characteristics=std::move(characteristics);
        ExistingKeyMint failed;
        check(!failed.decryptMetadata(blob,app_id,nonce,body,&key) && key.empty() &&
              fixture::count("characteristics")==1 && fixture::count("begin")==0 &&
              fixture::count("finish")==0);
    };
    refused({});
    refused(std::vector<km::KeyCharacteristics>(3, fixture::metadata_characteristics()[0]));
    auto characteristics=fixture::metadata_characteristics();
    characteristics[0].securityLevel=km::SecurityLevel::SOFTWARE; refused(characteristics);
    characteristics=fixture::metadata_characteristics();
    characteristics.push_back(characteristics[0]); refused(characteristics);
    characteristics=fixture::metadata_characteristics();
    characteristics[0].authorizations.resize(65); refused(characteristics);
    for (size_t index=0; index<8; ++index) {
        characteristics=fixture::metadata_characteristics();
        characteristics[0].authorizations.erase(characteristics[0].authorizations.begin()+index);
        refused(characteristics);
        characteristics=fixture::metadata_characteristics();
        characteristics[0].authorizations[index].value.type=km::KeyParameterValue::blob;
        refused(characteristics);
        characteristics=fixture::metadata_characteristics();
        characteristics[0].authorizations.push_back(characteristics[0].authorizations[index]);
        refused(characteristics);
    }
    for (auto restriction : {km::Tag::MAX_USES_PER_BOOT,km::Tag::USAGE_COUNT_LIMIT,
            km::Tag::USER_SECURE_ID,km::Tag::USER_AUTH_TYPE,km::Tag::AUTH_TIMEOUT,
            km::Tag::TRUSTED_USER_PRESENCE_REQUIRED,km::Tag::UNLOCKED_DEVICE_REQUIRED,
            static_cast<km::Tag>(999)}) {
        characteristics=fixture::metadata_characteristics();
        characteristics[0].authorizations.push_back(km::Authorization(restriction, 1));
        refused(characteristics);
    }
    for (size_t index=0; index<10; ++index) {
        characteristics=fixture::metadata_characteristics();
        auto& parameter=characteristics[0].authorizations[index];
        parameter.value.number = index==6 || index==7 ? int(km::KeyPurpose::SIGN) : -1;
        if (index==5) parameter.value.number=0;
        refused(characteristics);
    }
    characteristics=fixture::metadata_characteristics();
    characteristics.push_back({km::SecurityLevel::SOFTWARE,
        {km::Authorization(km::Tag::BOOT_PATCHLEVEL,20261001)}});
    device=fixture::reset(); device->characteristics=characteristics;
    ExistingKeyMint software_info;
    check(software_info.decryptMetadata(blob,app_id,nonce,body,&key) && key==KeyBuffer(32,7));
    characteristics.back().authorizations.push_back(characteristics.front().authorizations[0]);
    refused(characteristics);

    device=fixture::reset(); ExistingKeyMint exporter;
    KeyBuffer wrapped(32,'w');
    check(exporter.exportWrapped(wrapped,&key) && key==KeyBuffer(64,9));
    check(!exporter.exportWrapped(wrapped,&wrapped) && wrapped==KeyBuffer(32,'w'));
    check(!exporter.exportWrapped(wrapped,nullptr));
    for (size_t size : {size_t(0),size_t(513)}) {
        device->exported.resize(size);
        check(!exporter.exportWrapped(wrapped,&key) && key.empty());
    }
    device->exported.resize(64); device->export_status=-62;
    check(!exporter.exportWrapped(wrapped,&key) && key.empty() &&
          exporter.errorCode()==km::ErrorCode::KEY_REQUIRES_UPGRADE);
    const int export_before=fixture::count("export");
    check(!exporter.exportWrapped(KeyBuffer{},&key) && key.empty());
    check(!exporter.exportWrapped(KeyBuffer(65537,'b'),&key) && key.empty());
    check(fixture::count("export")==export_before);
    std::cout << cases << " existing-key-only host controls passed.\n";
}
