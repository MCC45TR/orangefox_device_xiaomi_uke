// Host API fixture only. No service or device is opened.
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#define EX_SERVICE_SPECIFIC -8
namespace ndk {
struct SpAIBinder { explicit SpAIBinder(void*) {} };
struct ScopedAStatus {
    int code = 0;
    bool isOk() const { return code == 0; }
    int getExceptionCode() const { return code == 1 ? -1 : EX_SERVICE_SPECIFIC; }
    int getServiceSpecificError() const { return code; }
};
}
namespace android::vold { using KeyBuffer = std::vector<char>; }
namespace aidl::android::hardware::security::keymint {
enum class ErrorCode { OK=0, UNKNOWN_ERROR=-1000, INVALID_ARGUMENT=-38,
                       KEY_REQUIRES_UPGRADE=-62, VERIFICATION_FAILED=-30,
                       HARDWARE_TYPE_UNAVAILABLE=-68 };
enum class SecurityLevel { SOFTWARE, TRUSTED_ENVIRONMENT };
enum class KeyPurpose { DECRYPT, ENCRYPT, SIGN };
enum class Algorithm { AES, RSA };
enum class KeyOrigin { GENERATED, IMPORTED };
enum class BlockMode { GCM=32 };
enum class PaddingMode { NONE=0 };
enum class Tag {
    BLOCK_MODE, MAC_LENGTH, APPLICATION_ID, NONCE, PADDING, ALGORITHM, KEY_SIZE,
    MIN_MAC_LENGTH, NO_AUTH_REQUIRED, PURPOSE, ROLLBACK_RESISTANCE, CALLER_NONCE,
    ORIGIN, OS_VERSION, OS_PATCHLEVEL, VENDOR_PATCHLEVEL, BOOT_PATCHLEVEL,
    MAX_USES_PER_BOOT, USAGE_COUNT_LIMIT, USER_SECURE_ID, USER_AUTH_TYPE,
    AUTH_TIMEOUT, TRUSTED_USER_PRESENCE_REQUIRED, UNLOCKED_DEVICE_REQUIRED,
};
constexpr Tag TAG_BLOCK_MODE=Tag::BLOCK_MODE, TAG_MAC_LENGTH=Tag::MAC_LENGTH,
    TAG_APPLICATION_ID=Tag::APPLICATION_ID, TAG_NONCE=Tag::NONCE, TAG_PADDING=Tag::PADDING;
struct KeyParameterValue {
    enum Tag { integer, blob, algorithm, blockMode, paddingMode, keyPurpose, boolValue, origin };
    Tag type = integer;
    int number=0;
    std::vector<uint8_t> bytes;
    Tag getTag() const { return type; }
    template<Tag T> decltype(auto) get() {
        assert(type == T);
        if constexpr (T == blob) return (bytes);
        else return std::as_const(*this).get<T>();
    }
    template<Tag T> decltype(auto) get() const {
        assert(type == T);
        if constexpr (T == blob) return (bytes);
        else if constexpr (T == algorithm) return static_cast<Algorithm>(number);
        else if constexpr (T == blockMode) return static_cast<BlockMode>(number);
        else if constexpr (T == paddingMode) return static_cast<PaddingMode>(number);
        else if constexpr (T == keyPurpose) return static_cast<KeyPurpose>(number);
        else if constexpr (T == origin) return static_cast<KeyOrigin>(number);
        else if constexpr (T == boolValue) return bool(number);
        else return number;
    }
};
struct KeyParameter { Tag tag; KeyParameterValue value; };
inline KeyParameter Authorization(Tag tag, int value) {
    return {tag, {KeyParameterValue::integer, value, {}}};
}
inline KeyParameter Authorization(Tag tag, BlockMode value) {
    return {tag, {KeyParameterValue::blockMode, static_cast<int>(value), {}}};
}
inline KeyParameter Authorization(Tag tag, PaddingMode value) {
    return {tag, {KeyParameterValue::paddingMode, static_cast<int>(value), {}}};
}
inline KeyParameter Authorization(Tag tag, std::vector<uint8_t> value) {
    return {tag, {KeyParameterValue::blob, 0, std::move(value)}};
}
struct KeyCharacteristics { SecurityLevel securityLevel; std::vector<KeyParameter> authorizations; };
struct KeyMintHardwareInfo { SecurityLevel securityLevel; };
class IKeyMintOperation {
  public:
    virtual ~IKeyMintOperation()=default;
    virtual ndk::ScopedAStatus finish(const std::optional<std::vector<uint8_t>>&,
        std::nullopt_t, std::nullopt_t, std::nullopt_t, std::nullopt_t,
        std::vector<uint8_t>*)=0;
    virtual ndk::ScopedAStatus abort()=0;
};
struct BeginResult { std::shared_ptr<IKeyMintOperation> operation; };
class IKeyMintDevice {
  public:
    virtual ~IKeyMintDevice()=default;
    static std::shared_ptr<IKeyMintDevice> fromBinder(const ndk::SpAIBinder&);
    virtual ndk::ScopedAStatus getInterfaceVersion(int32_t*)=0;
    virtual ndk::ScopedAStatus getHardwareInfo(KeyMintHardwareInfo*)=0;
    virtual ndk::ScopedAStatus getKeyCharacteristics(const std::vector<uint8_t>&,
        const std::vector<uint8_t>&, const std::vector<uint8_t>&,
        std::vector<KeyCharacteristics>*)=0;
    virtual ndk::ScopedAStatus begin(KeyPurpose, const std::vector<uint8_t>&,
        const std::vector<KeyParameter>&, std::nullopt_t, BeginResult*)=0;
    virtual ndk::ScopedAStatus convertStorageKeyToEphemeral(const std::vector<uint8_t>&,
        std::vector<uint8_t>*)=0;
};
}
namespace fixture {
namespace km=aidl::android::hardware::security::keymint;
inline std::vector<km::KeyCharacteristics> metadata_characteristics() {
    using V = km::KeyParameterValue;
    return {{km::SecurityLevel::TRUSTED_ENVIRONMENT, {
        {km::Tag::ALGORITHM, {V::algorithm, int(km::Algorithm::AES), {}}},
        km::Authorization(km::Tag::KEY_SIZE, 256),
        km::Authorization(km::Tag::BLOCK_MODE, km::BlockMode::GCM),
        km::Authorization(km::Tag::PADDING, km::PaddingMode::NONE),
        km::Authorization(km::Tag::MIN_MAC_LENGTH, 128),
        {km::Tag::NO_AUTH_REQUIRED, {V::boolValue, 1, {}}},
        {km::Tag::PURPOSE, {V::keyPurpose, int(km::KeyPurpose::DECRYPT), {}}},
        {km::Tag::PURPOSE, {V::keyPurpose, int(km::KeyPurpose::ENCRYPT), {}}},
        {km::Tag::ORIGIN, {V::origin, int(km::KeyOrigin::GENERATED), {}}},
        km::Authorization(km::Tag::OS_VERSION, 170000),
    }}};
}
inline int checks=0, cleansed=0;
inline std::vector<std::string> calls;
inline std::shared_ptr<km::IKeyMintDevice> connected;
struct Operation : km::IKeyMintOperation {
    int status=0;
    std::vector<uint8_t> output=std::vector<uint8_t>(32, 7);
    ndk::ScopedAStatus finish(const std::optional<std::vector<uint8_t>>& input,
        std::nullopt_t, std::nullopt_t, std::nullopt_t, std::nullopt_t,
        std::vector<uint8_t>* result) override {
        calls.push_back("finish"); assert(input && input->size() >= 17);
        *result=output;
        return {status};
    }
    ndk::ScopedAStatus abort() override { calls.push_back("abort"); return {}; }
};
struct Device : km::IKeyMintDevice {
    int version=4, version_status=0, hardware_status=0, begin_status=0, export_status=0,
        characteristics_status=0;
    bool missing_operation=false;
    km::SecurityLevel security=km::SecurityLevel::TRUSTED_ENVIRONMENT;
    std::shared_ptr<Operation> operation=std::make_shared<Operation>();
    std::vector<uint8_t> exported=std::vector<uint8_t>(64, 9);
    std::vector<uint8_t> last_app_id;
    std::vector<km::KeyCharacteristics> characteristics=metadata_characteristics();
    ndk::ScopedAStatus getInterfaceVersion(int32_t* out) override {
        calls.push_back("version"); *out=version; return {version_status};
    }
    ndk::ScopedAStatus getHardwareInfo(km::KeyMintHardwareInfo* out) override {
        calls.push_back("hardware"); out->securityLevel=security; return {hardware_status};
    }
    ndk::ScopedAStatus getKeyCharacteristics(const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& app, const std::vector<uint8_t>& data,
        std::vector<km::KeyCharacteristics>* out) override {
        calls.push_back("characteristics"); assert(!blob.empty() && !app.empty() && data.empty());
        last_app_id=app; *out=characteristics; return {characteristics_status};
    }
    ndk::ScopedAStatus begin(km::KeyPurpose purpose, const std::vector<uint8_t>& blob,
        const std::vector<km::KeyParameter>& params, std::nullopt_t,
        km::BeginResult* result) override {
        calls.push_back("begin");
        assert(purpose == km::KeyPurpose::DECRYPT && !blob.empty());
        assert(params.size()==5 && params[0].tag==km::TAG_BLOCK_MODE &&
               params[0].value.number==static_cast<int>(km::BlockMode::GCM) &&
               params[1].tag==km::TAG_PADDING &&
               params[1].value.number==static_cast<int>(km::PaddingMode::NONE) &&
               params[2].tag==km::TAG_MAC_LENGTH && params[2].value.number==128 &&
               params[3].tag==km::TAG_APPLICATION_ID && !params[3].value.bytes.empty() &&
               params[4].tag==km::TAG_NONCE && params[4].value.bytes.size()==12);
        last_app_id=params[3].value.bytes;
        if (!missing_operation) result->operation=operation;
        return {begin_status};
    }
    ndk::ScopedAStatus convertStorageKeyToEphemeral(const std::vector<uint8_t>& blob,
        std::vector<uint8_t>* output) override {
        calls.push_back("export"); assert(!blob.empty()); *output=exported; return {export_status};
    }
};
inline std::shared_ptr<Device> reset() {
    calls.clear(); checks=0; cleansed=0;
    auto device=std::make_shared<Device>(); connected=device; return device;
}
inline int count(const char* value) { return std::count(calls.begin(),calls.end(),value); }
}
inline void* AServiceManager_checkService(const char* name) {
    assert(std::string(name)=="android.hardware.security.keymint.IKeyMintDevice/default");
    ++fixture::checks; return nullptr;
}
inline void OPENSSL_cleanse(void* memory, size_t size) {
    assert(memory && size>0); ++fixture::cleansed;
    std::memset(memory, 0, size);
}
inline std::shared_ptr<aidl::android::hardware::security::keymint::IKeyMintDevice>
aidl::android::hardware::security::keymint::IKeyMintDevice::fromBinder(const ndk::SpAIBinder&) {
    return fixture::connected;
}
