// SPDX-License-Identifier: Apache-2.0
// Host-only Binder stand-ins; production method bodies are extracted separately.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#define LOG(level) std::cerr
constexpr int EX_SERVICE_SPECIFIC=1, VOLD_NAMESPACE=100;
struct Status {
    bool ok=true;
    int getExceptionCode() const { return EX_SERVICE_SPECIFIC; }
    int getServiceSpecificError() const { return -1; }
};
bool logKeystore2ExceptionIfPresent(Status s,const std::string&) { return !s.ok; }
void zeroize_vector(std::vector<std::uint8_t>& v) { std::fill(v.begin(),v.end(),0); }
namespace km {
enum class ErrorCode { OK, UNKNOWN_ERROR };
struct AuthorizationSet {
    std::vector<int> value;
    AuthorizationSet()=default;
    AuthorizationSet(const std::vector<int>& in):value(in) {}
    const std::vector<int>& vector_data() const { return value; }
};
}
using KeyBuffer=std::vector<std::uint8_t>;
struct Operation {
    Status result{};
    std::optional<std::vector<std::uint8_t>> reply;
    unsigned calls=0;
    Status finish(std::nullopt_t,std::nullopt_t,std::optional<std::vector<std::uint8_t>>* out) {
        ++calls; *out=reply; return result;
    }
};
namespace ks2 {
enum class Domain { BLOB };
struct KeyDescriptor {
    Domain domain;
    int nspace;
    std::optional<std::string> alias;
    std::optional<std::vector<std::uint8_t>> blob;
};
struct KeyMetadata { KeyDescriptor key{}; };
struct EphemeralStorageKeyResponse { std::vector<std::uint8_t> ephemeralKey; };
struct Parameters { std::vector<int> keyParameter; };
struct CreateOperationResponse {
    std::shared_ptr<Operation> iOperation;
    std::optional<std::vector<std::uint8_t>> upgradedBlob;
    std::optional<Parameters> parameters;
};
struct SecurityLevel {
    Status result{};
    unsigned calls=0;
    std::shared_ptr<Operation> operation=std::make_shared<Operation>();
    Status generateKey(const KeyDescriptor&,std::nullopt_t,const std::vector<int>&,int,
                       std::initializer_list<int>,KeyMetadata* out) {
        ++calls; out->key.blob=std::vector<std::uint8_t>{'g'}; return result;
    }
    Status convertStorageKeyToEphemeral(const KeyDescriptor&,EphemeralStorageKeyResponse* out) {
        ++calls; out->ephemeralKey={'e'}; return result;
    }
    Status deleteKey(const KeyDescriptor&) { ++calls; return result; }
    Status createOperation(const KeyDescriptor&,const std::vector<int>&,bool,CreateOperationResponse* out) {
        ++calls; out->iOperation=operation; out->parameters=Parameters{{42}}; return result;
    }
};
}
class KeystoreOperation {
public:
    std::shared_ptr<Operation> ks2Operation;
    KeystoreOperation()=default;
    KeystoreOperation(km::ErrorCode) {}
    KeystoreOperation(std::shared_ptr<Operation> op,std::optional<std::vector<std::uint8_t>>)
        :ks2Operation(std::move(op)) {}
    explicit operator bool() const { return bool(ks2Operation); }
    bool finish(std::string*);
};
class Keystore {
public:
    std::shared_ptr<ks2::SecurityLevel> securityLevel;
    bool generateKey(const km::AuthorizationSet&,std::string*);
    bool exportKey(const KeyBuffer&,std::string*);
    bool deleteKey(const std::string&);
    KeystoreOperation begin(const std::string&,const km::AuthorizationSet&,km::AuthorizationSet*);
};

#include "production-methods.inc"

// Included after actual production method bodies, using host-only Binder stand-ins.
int main(int argc,char** argv) {
    assert(argc==2);
    const std::string scenario=argv[1];
    std::string result="unchanged";
    km::AuthorizationSet params;
    if(scenario.starts_with("missing-security-level")) {
        Keystore unavailable;
        const auto selected=[&](const char* method) {
            return scenario=="missing-security-level" || scenario==std::string("missing-security-level:")+method;
        };
        if(selected("generateKey"))assert(!unavailable.generateKey(params,&result) && result=="unchanged");
        if(selected("exportKey"))assert(!unavailable.exportKey({'k'},&result) && result=="unchanged");
        if(selected("deleteKey"))assert(!unavailable.deleteKey("k"));
        if(selected("begin"))assert(!unavailable.begin("k",params,&params));
    } else if(scenario=="valid-security-level") {
        Keystore available; available.securityLevel=std::make_shared<ks2::SecurityLevel>();
        assert(available.generateKey(params,&result) && result=="g");
        assert(available.exportKey({'k'},&result) && result=="e");
        assert(available.deleteKey("k"));
        assert(available.begin("k",params,&params));
        assert(params.value==std::vector<int>{42});
        assert(available.securityLevel->calls==4);
        available.securityLevel->result.ok=false;
        assert(!available.generateKey(params,&result));
        assert(!available.exportKey({'k'},&result));
        assert(!available.deleteKey("k"));
        assert(!available.begin("k",params,&params));
    } else if(scenario=="absent-plaintext") {
        KeystoreOperation op; op.ks2Operation=std::make_shared<Operation>();
        const auto retained=op.ks2Operation;
        assert(!op.finish(&result));
        assert(!op && result=="unchanged" && retained->calls==1);
        op.ks2Operation=std::make_shared<Operation>();
        assert(op.finish(nullptr));
    } else if(scenario=="valid-plaintext") {
        KeystoreOperation op; op.ks2Operation=std::make_shared<Operation>();
        op.ks2Operation->reply=std::vector<std::uint8_t>{'a',0,'z'};
        assert(op.finish(&result) && result==std::string("a\0z",3));
        op.ks2Operation=std::make_shared<Operation>();
        op.ks2Operation->reply=std::vector<std::uint8_t>{};
        assert(op.finish(&result) && result.empty());
        op.ks2Operation=std::make_shared<Operation>(); op.ks2Operation->result.ok=false;
        result="unchanged";
        assert(!op.finish(&result) && !op && result=="unchanged");
        assert(!op.finish(&result));
    } else return 2;
}
