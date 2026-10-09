// SPDX-License-Identifier: Apache-2.0
// Compile the extracted production function with inert host-only boundaries.
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace km {
class AuthorizationSet : public std::vector<int> {
public:
    using std::vector<int>::vector;
    template <class Iterator> void append(Iterator begin, Iterator end) {
        insert(this->end(), begin, end);
    }
};
}

static unsigned begin_calls;
static unsigned read_calls;
static unsigned write_calls;
static unsigned mkdir_calls;
static bool read_succeeds = true;
static bool write_succeeds = true;
static bool begin_succeeds = true;
static std::optional<std::string> upgrade;
static std::string read_path;
static std::string write_path;
static std::string write_value;

class KeystoreOperation {
    bool valid_ = false;
    std::optional<std::string> upgraded_;
public:
    KeystoreOperation() = default;
    KeystoreOperation(bool valid, std::optional<std::string> blob)
        : valid_(valid), upgraded_(std::move(blob)) {}
    KeystoreOperation(const KeystoreOperation&) = delete;
    KeystoreOperation& operator=(const KeystoreOperation&) = delete;
    KeystoreOperation(KeystoreOperation&&) = default;
    KeystoreOperation& operator=(KeystoreOperation&&) = default;
    explicit operator bool() const { return valid_; }
    std::optional<std::string> getUpgradedBlob() const { return upgraded_; }
};

class Keystore {
public:
    KeystoreOperation begin(const std::string& blob, const km::AuthorizationSet& parameters,
                            km::AuthorizationSet*) {
        ++begin_calls;
        if (blob != "inert-host-fixture" || parameters != km::AuthorizationSet{1, 2}) std::exit(2);
        return {begin_succeeds, upgrade};
    }
};

struct LogSink {
    template <class T> LogSink& operator<<(const T&) { return *this; }
};
#define LOG(...) LogSink{}
#define PLOG(...) LogSink{}

static std::mutex key_upgrade_lock;
static const char* kFn_keymaster_key_blob = "keymaster_key_blob";

extern "C" int __wrap_mkdir(const char* path, mode_t mode) {
    ++mkdir_calls;
    if (std::string(path) != "/tmp/keymaster_key_blob/" || mode != 0700) std::exit(2);
    errno = EEXIST;
    return -1;
}

static bool readFileToString(const std::string& path, std::string* output) {
    ++read_calls;
    read_path = path;
    *output = "inert-host-fixture";
    return read_succeeds;
}

static bool writeStringToFile(const std::string& value, const std::string& path) {
    ++write_calls;
    write_value = value;
    write_path = path;
    return write_succeeds;
}

#include "vold-begin-keystore-op.inc"

static void require(bool condition, const char* label) {
    if (condition) return;
    std::cerr << "FAIL: " << label << '\n';
    std::exit(1);
}

static void reset() {
    begin_calls = read_calls = write_calls = mkdir_calls = 0;
    read_succeeds = write_succeeds = begin_succeeds = true;
    upgrade.reset();
    read_path.clear();
    write_path.clear();
    write_value.clear();
}

int main(int argc, char**) {
    Keystore keystore;
    const km::AuthorizationSet key_parameters{1};
    const km::AuthorizationSet operation_parameters{2};
    km::AuthorizationSet output_parameters;
    reset();
    require(static_cast<bool>(BeginKeystoreOp(keystore, "/host-fixture-never-read", key_parameters,
        operation_parameters, &output_parameters)), "normal no-upgrade operation failed");
    require(begin_calls == 1 && read_calls == 1 && mkdir_calls == 0 && write_calls == 0,
        "no-upgrade operation attempted a blob write");
    if (argc > 1) return 0;  // Original-source negative control ends here.
    require(read_path == "/host-fixture-never-read/keymaster_key_blob", "wrong blob source path");

    reset();
    upgrade = "inert-upgraded-fixture";
    require(static_cast<bool>(BeginKeystoreOp(keystore, "/host-fixture-never-read", key_parameters,
        operation_parameters, &output_parameters)), "genuine upgrade operation failed");
    require(mkdir_calls == 1 && write_calls == 1 && write_value == *upgrade &&
        write_path == "/tmp/keymaster_key_blob/keymaster_key_blob", "upgrade output changed");

    reset();
    upgrade = "inert-upgraded-fixture";
    write_succeeds = false;
    require(!static_cast<bool>(BeginKeystoreOp(keystore, "/host-fixture-never-read", key_parameters,
        operation_parameters, &output_parameters)) && write_calls == 1,
        "failed upgrade write did not invalidate the operation");

    reset();
    begin_succeeds = false;
    upgrade = "inert-upgraded-fixture";
    require(!static_cast<bool>(BeginKeystoreOp(keystore, "/host-fixture-never-read", key_parameters,
        operation_parameters, &output_parameters)) && write_calls == 0,
        "invalid begin operation attempted an upgrade write");

    reset();
    read_succeeds = false;
    require(!static_cast<bool>(BeginKeystoreOp(keystore, "/host-fixture-never-read", key_parameters,
        operation_parameters, &output_parameters)) && begin_calls == 0 && write_calls == 0,
        "failed key read continued into keystore or storage writes");
    std::cout << "Actual BeginKeystoreOp no-upgrade, upgrade, read failure, begin failure and write failure controls passed with all I/O intercepted.\n";
}
