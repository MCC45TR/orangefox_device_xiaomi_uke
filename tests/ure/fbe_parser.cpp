// SPDX-License-Identifier: Apache-2.0
// Synthetic records only; no credentials, services, keys or tablet access.
#include "ure-fbe-parser.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <vector>

[[noreturn]] static void fail(const char* message) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
static void require(bool value, const char* message) { if (!value) fail(message); }
static unsigned scryptCalls = 0;
static int scryptResult = 0, allocationBudget = -1;
static int fixture_printf(const char*, ...) { return 0; }
static void* fixture_malloc(size_t size) {
    if (allocationBudget == 0) return nullptr;
    if (allocationBudget > 0) --allocationBudget;
    return std::malloc(size);
}
namespace android { namespace vold {
static bool pathExists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }
} }
static int crypto_scrypt(const uint8_t*, size_t, const uint8_t*, size_t, uint64_t n,
                         uint32_t r, uint32_t p, uint8_t* token, size_t length) {
    ++scryptCalls;
    require(n == 2048 && r == 8 && p == 2 && length == 32, "production KDF parameters changed");
    if (scryptResult == 0) std::memset(token, 0xa5, length);
    return scryptResult;
}
#define printf fixture_printf
#define malloc fixture_malloc
#include "fbe-production-functions.inc"
#undef malloc
#undef printf

static void be32(std::string& output, uint32_t value) {
    for (int shift : {24, 16, 8, 0}) output.push_back(static_cast<char>((value >> shift) & 255));
}
static void setBe32(std::string& output, size_t offset, uint32_t value) {
    std::string field; be32(field, value); output.replace(offset, 4, field);
}
static std::string pwdRecord(size_t saltSize = 17, size_t handleSize = 7, bool tail = false) {
    std::string data; be32(data, 3); data.append("\x0b\x03\x01", 3);
    be32(data, static_cast<uint32_t>(saltSize)); data.append(saltSize, 's');
    be32(data, static_cast<uint32_t>(handleSize)); data.append(handleSize, 'h');
    if (tail) be32(data, UINT32_MAX);
    return data;
}
static void writeFixture(const std::string& path, const std::string& data) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    require(bool(stream), "synthetic fixture write failed");
}
static void release(password_data_struct& data) { std::free(data.salt); std::free(data.password_handle); data = {}; }

static void weaver(const std::string& directory) {
    for (uint32_t slot : {0U, 1U, 0x01020304U, static_cast<uint32_t>(INT32_MAX)}) {
        std::string data(1, '\1'); be32(data, slot);
        int parsed = -1;
        require(ure_fbe::ParseWeaver(data, &parsed) && parsed == static_cast<int>(slot), "Weaver byte-one big-endian slot decode failed");
        writeFixture(directory + "synthetic.weaver", data);
        weaver_data_struct output{};
        require(Get_Weaver_Data(directory, "synthetic", &output) && output.version == 1 && output.slot == parsed,
                "production Weaver parser rejected valid record");
        for (size_t length = 0; length < data.size(); ++length)
            require(!ure_fbe::ParseWeaver(std::string_view(data).substr(0, length), &parsed), "truncated Weaver accepted");
        data.push_back('x'); require(!ure_fbe::ParseWeaver(data, &parsed), "oversized Weaver accepted");
    }
    for (const unsigned version : {0U, 2U, 255U}) {
        std::string data(1, static_cast<char>(version)); be32(data, 1); int slot;
        require(!ure_fbe::ParseWeaver(data, &slot), "unknown Weaver version accepted");
    }
    std::string negative(1, '\1'); be32(negative, UINT32_MAX); int slot;
    require(!ure_fbe::ParseWeaver(negative, &slot), "negative Weaver slot accepted");
    require(!ure_fbe::ParseWeaver(negative, nullptr), "null Weaver output accepted");
    std::puts("Weaver exact size/version, byte-one BE slot, truncation and signed-slot controls passed.");
}

static void password(const std::string& directory) {
    for (const bool tail : {false, true}) for (const size_t salt : {16U, 17U, 31U}) for (const size_t handle : {0U, 7U, 58U}) {
        const std::string data = pwdRecord(salt, handle, tail);
        ure_fbe::PasswordData parsed;
        require(ure_fbe::ParsePasswordData(data, &parsed) && parsed.type == 3 && parsed.salt.size() == salt && parsed.handle.size() == handle,
                "valid password record rejected");
        for (size_t length = 0; length < data.size(); ++length) {
            if (tail && length == data.size() - 4) continue; // Valid legacy record without pinLength.
            require(!ure_fbe::ParsePasswordData(std::string_view(data).substr(0, length), &parsed), "truncated password record accepted");
        }
        writeFixture(directory + "synthetic.pwd", data);
        password_data_struct output{};
        require(Get_Password_Data(directory, "synthetic", &output) && output.salt_len == static_cast<int>(salt) &&
                output.handle_len == static_cast<int>(handle), "production password parser rejected valid record");
        require(std::memcmp(output.salt, std::string(salt, 's').data(), salt) == 0, "production salt copy changed");
        if (handle) require(std::memcmp(output.password_handle, std::string(handle, 'h').data(), handle) == 0, "production handle copy changed");
        release(output);
    }
    for (const uint32_t length : {0x80000000U, UINT32_MAX, 0x7fffffffU, 1025U}) {
        std::string data = pwdRecord(); setBe32(data, 7, length); ure_fbe::PasswordData parsed;
        require(!ure_fbe::ParsePasswordData(data, &parsed), "invalid signed salt length accepted");
    }
    for (const uint32_t length : {0x80000000U, UINT32_MAX, 0x7fffffffU, 16385U}) {
        std::string data = pwdRecord(); setBe32(data, 11 + 17, length); ure_fbe::PasswordData parsed;
        require(!ure_fbe::ParsePasswordData(data, &parsed), "invalid signed handle length accepted");
    }
    ure_fbe::PasswordData parsed;
    require(!ure_fbe::ParsePasswordData(pwdRecord() + "x", &parsed), "unknown password extension accepted");
    std::string data = pwdRecord(); setBe32(data, 7, 0);
    require(!ure_fbe::ParsePasswordData(data, &parsed), "zero salt accepted");
    for (int budget : {0, 1}) {
        writeFixture(directory + "synthetic.pwd", pwdRecord());
        password_data_struct output{}; allocationBudget = budget;
        require(!Get_Password_Data(directory, "synthetic", &output) && !output.salt && !output.password_handle,
                "failed allocation published partial password state");
        allocationBudget = -1;
    }
    std::puts("Password endian/signed-length, odd alignment, optional pinLength, truncation and allocation-failure controls passed.");
}

static void scrypt(const std::string& directory) {
    ure_fbe::ScryptParameters output;
    require(ure_fbe::CheckScrypt(11, 3, 1, &output) && output.n == 2048 && output.r == 8 && output.p == 2,
            "pinned Android scrypt parameters rejected");
    require(!ure_fbe::CheckScrypt(18, 0, 0, &output), "scrypt memory budget bypassed");
    require(!ure_fbe::CheckScrypt(10, 0, 11, &output), "scrypt work budget bypassed");
    for (const unsigned bad : {31U, 32U, 63U, 64U, 127U, 255U}) {
        require(!ure_fbe::CheckScrypt(bad, 3, 1, &output), "unbounded scrypt N exponent accepted");
        require(!ure_fbe::CheckScrypt(11, bad, 1, &output), "unbounded scrypt r exponent accepted");
        require(!ure_fbe::CheckScrypt(11, 3, bad, &output), "unbounded scrypt p exponent accepted");
    }
    writeFixture(directory + "synthetic.pwd", pwdRecord());
    password_data_struct password{}; require(Get_Password_Data(directory, "synthetic", &password), "token fixture parse failed");
    std::array<unsigned char, 32> token{};
    require(Get_Password_Token(&password, "inert synthetic input", token.data()) && scryptCalls == 1 && token.front() == 0xa5,
            "production token call changed");
    for (const unsigned char bad : {static_cast<unsigned char>(32), static_cast<unsigned char>(255)}) {
        password.scryptN = bad;
        require(!Get_Password_Token(&password, "synthetic", token.data()) && scryptCalls == 1, "invalid exponent reached KDF");
    }
    password.scryptN = 11;
    require(!Get_Password_Token(&password, std::string(ure_fbe::kMaxCredentialBytes + 1, 'x'), token.data()) && scryptCalls == 1,
            "oversized credential reached KDF");
    require(!Get_Password_Token(nullptr, "synthetic", token.data()) && !Get_Password_Token(&password, "synthetic", nullptr), "null token arguments accepted");
    scryptResult = -1;
    require(!Get_Password_Token(&password, "synthetic", token.data()), "KDF failure reported success");
    release(password);
    std::puts("Exact production token admission preserves pinned KDF parameters and bounds exponents, memory and work before calls.");
}

static void blobs() {
    for (const unsigned version : {1U, 2U, 3U}) {
        std::string data; data.push_back(static_cast<char>(version)); data.push_back('\0');
        data.append(12, 'i'); data.append(32, 'c'); data.append(16, 't');
        ure_fbe::Spblob blob;
        require(ure_fbe::ParseSpblob(data, &blob) && blob.version == version && blob.content.iv == std::string(12, 'i') &&
                blob.content.ciphertext == std::string(32, 'c') && blob.content.tag == std::string(16, 't'), "SP blob IV/ciphertext/tag boundaries changed");
        for (size_t length = 0; length < 30; ++length)
            require(!ure_fbe::ParseSpblob(std::string_view(data).substr(0, length), &blob), "truncated SP blob accepted");
    }
    ure_fbe::Spblob blob; ure_fbe::GcmPayload payload;
    require(!ure_fbe::ParseGcmPayload(std::string(27, 'x'), &payload), "truncated inner GCM tag accepted");
    require(!ure_fbe::ParseSpblob(std::string(ure_fbe::kMaxStateBytes + 1, 'x'), &blob), "oversized blob accepted");
    for (const unsigned version : {0U, 4U, 255U}) {
        std::string data(30, '\0'); data[0] = static_cast<char>(version);
        require(!ure_fbe::ParseSpblob(data, &blob), "unknown SP blob version accepted");
    }
    std::string tokenBlob(30, '\0'); tokenBlob[0] = 3; tokenBlob[1] = 1;
    require(!ure_fbe::ParseSpblob(tokenBlob, &blob), "token-based protector admitted as password");
    std::puts("SP blob versions 1-3, password protector, outer/inner IV/tag bounds and unknown-version refusals passed.");
}

static void boundedRead(const std::string& directory) {
    const std::string path = directory + "bounded.record";
    std::string output;
    writeFixture(path, std::string(32, 'x'));
    require(ure_fbe::ReadStateFile(path, &output, 32) && output.size() == 32, "bounded file read rejected boundary");
    writeFixture(path, std::string(33, 'x'));
    require(!ure_fbe::ReadStateFile(path, &output, 32) && output.empty(), "oversized record read accepted");
    const std::string link = directory + "synthetic-link.record";
    unlink(link.c_str()); require(symlink(path.c_str(), link.c_str()) == 0, "synthetic symlink setup failed");
    require(!ure_fbe::ReadStateFile(link, &output, 64), "state file symlink followed");
    require(!ure_fbe::ReadStateFile(directory, &output, 64), "nonregular state file read accepted");
    std::puts("Bounded regular-file reads refuse oversize records, symlinks and nonregular input.");
}

static void fuzz() {
    std::minstd_rand random(17);
    for (unsigned trial = 0; trial < 20000; ++trial) {
        std::string data(random() % 192, '\0');
        for (char& byte : data) byte = static_cast<char>(random() & 255);
        ure_fbe::PasswordData pwd; ure_fbe::Spblob blob; ure_fbe::GcmPayload payload; int slot;
        ure_fbe::ParsePasswordData(data, &pwd); ure_fbe::ParseSpblob(data, &blob);
        ure_fbe::ParseWeaver(data, &slot); ure_fbe::ParseGcmPayload(data, &payload);
    }
    std::puts("20,000 deterministic synthetic malformed records passed parser instrumentation.");
}

int main(int argc, char** argv) {
    require(argc == 3, "scenario and synthetic directory required");
    const std::string mode = argv[1], directory = std::string(argv[2]) + "/";
    if (mode == "weaver") weaver(directory);
    else if (mode == "password") password(directory);
    else if (mode == "scrypt") scrypt(directory);
    else if (mode == "blobs") blobs();
    else if (mode == "read") boundedRead(directory);
    else if (mode == "fuzz") fuzz();
    else fail("unknown scenario");
}
