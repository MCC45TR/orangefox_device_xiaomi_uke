// SPDX-License-Identifier: Apache-2.0
// Real host EVP operations on synthetic bytes; no Binder, credentials or keys.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <openssl/evp.h>

enum class Failure { None, Allocation, Context, Init, Iv, Update, Tag, Final };
static Failure failure = Failure::None;
static unsigned uncleanFrees = 0, contextCreated = 0, contextFreed = 0;
struct Allocation { void* pointer = nullptr; size_t size = 0; };
static std::array<Allocation, 32> allocations{};
[[noreturn]] static void fail(const char* message) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
static void require(bool value, const char* message) { if (!value) fail(message); }

extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(size_t size) {
    if (failure == Failure::Allocation) return nullptr;
    void* pointer = __real_malloc(size);
    if (!pointer) return nullptr;
    for (auto& item : allocations) if (!item.pointer) {
        item = {pointer, size}; std::memset(pointer, 0x6d, size); return pointer;
    }
    fail("allocation trace capacity exceeded");
}
extern "C" void __wrap_free(void* pointer) {
    for (auto& item : allocations) if (item.pointer && item.pointer == pointer) {
        const auto* bytes = static_cast<const unsigned char*>(pointer);
        if (!std::all_of(bytes, bytes + item.size, [](unsigned char byte) { return byte == 0; })) ++uncleanFrees;
        item = {}; break;
    }
    __real_free(pointer);
}
extern "C" EVP_CIPHER_CTX* __real_EVP_CIPHER_CTX_new();
extern "C" void __real_EVP_CIPHER_CTX_free(EVP_CIPHER_CTX*);
extern "C" EVP_CIPHER_CTX* __wrap_EVP_CIPHER_CTX_new() {
    if (failure == Failure::Context) return nullptr;
    auto* ctx = __real_EVP_CIPHER_CTX_new(); if (ctx) ++contextCreated; return ctx;
}
extern "C" void __wrap_EVP_CIPHER_CTX_free(EVP_CIPHER_CTX* ctx) { if (ctx) ++contextFreed; __real_EVP_CIPHER_CTX_free(ctx); }
extern "C" int __real_EVP_DecryptInit_ex(EVP_CIPHER_CTX*, const EVP_CIPHER*, ENGINE*, const unsigned char*, const unsigned char*);
extern "C" int __wrap_EVP_DecryptInit_ex(EVP_CIPHER_CTX* ctx, const EVP_CIPHER* cipher, ENGINE* engine, const unsigned char* key, const unsigned char* iv) {
    if (failure == Failure::Init) return 0;
    return __real_EVP_DecryptInit_ex(ctx, cipher, engine, key, iv);
}
extern "C" int __real_EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX*, int, int, void*);
extern "C" int __wrap_EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX* ctx, int command, int argument, void* value) {
    if ((failure == Failure::Iv && command == EVP_CTRL_GCM_SET_IVLEN) ||
        (failure == Failure::Tag && command == EVP_CTRL_GCM_SET_TAG)) return 0;
    return __real_EVP_CIPHER_CTX_ctrl(ctx, command, argument, value);
}
extern "C" int __real_EVP_DecryptUpdate(EVP_CIPHER_CTX*, unsigned char*, int*, const unsigned char*, int);
extern "C" int __wrap_EVP_DecryptUpdate(EVP_CIPHER_CTX* ctx, unsigned char* output, int* count, const unsigned char* input, int size) {
    if (failure == Failure::Update) { std::memcpy(output, "dirty", 5); *count = 5; return 0; }
    return __real_EVP_DecryptUpdate(ctx, output, count, input, size);
}
extern "C" int __real_EVP_DecryptFinal_ex(EVP_CIPHER_CTX*, unsigned char*, int*);
extern "C" int __wrap_EVP_DecryptFinal_ex(EVP_CIPHER_CTX* ctx, unsigned char* output, int* count) {
    if (failure == Failure::Final) { *count = 0; return 0; }
    return __real_EVP_DecryptFinal_ex(ctx, output, count);
}

#include "ure-fbe-gcm.hpp"

static std::array<unsigned char, 32> key() {
    std::array<unsigned char, 32> result{};
    for (size_t i = 0; i < result.size(); ++i) result[i] = static_cast<unsigned char>(i);
    return result;
}
static std::vector<uint8_t> encrypt(const std::string& plaintext) {
    const auto secret = key();
    std::array<unsigned char, 12> iv{};
    for (size_t i = 0; i < iv.size(); ++i) iv[i] = static_cast<unsigned char>(0xa0 + i);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new(); require(ctx != nullptr, "synthetic encryption setup failed");
    require(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, secret.data(), iv.data()) == 1, "synthetic encryption init failed");
    std::vector<uint8_t> result(iv.begin(), iv.end()); result.resize(12 + plaintext.size() + 16);
    int size = 0, finalSize = 0;
    require(EVP_EncryptUpdate(ctx, result.data() + 12, &size, reinterpret_cast<const unsigned char*>(plaintext.data()),
                             static_cast<int>(plaintext.size())) == 1, "synthetic encryption update failed");
    require(EVP_EncryptFinal_ex(ctx, result.data() + 12 + size, &finalSize) == 1, "synthetic encryption final failed");
    require(static_cast<size_t>(size + finalSize) == plaintext.size(), "synthetic ciphertext size changed");
    require(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, result.data() + 12 + plaintext.size()) == 1, "synthetic encryption tag failed");
    EVP_CIPHER_CTX_free(ctx);
    return result;
}
static void verifyCleanup() {
    require(uncleanFrees == 0, "secret allocation freed without cleansing");
    for (const auto& allocation : allocations) require(!allocation.pointer, "secret allocation retained");
    require(contextCreated == contextFreed, "EVP context retained");
}

static void positive() {
    const auto secret = key();
    for (const size_t size : {0U, 1U, 15U, 16U, 31U, 32U, 64U, 256U}) {
        std::string plaintext(size, 'x');
        for (size_t i = 0; i < size; ++i) plaintext[i] = static_cast<char>(i & 255);
        std::optional<std::vector<uint8_t>> input = encrypt(plaintext);
        {
            ure_fbe::ScopedOptionalCleanse guard(input);
            ure_fbe::SecretBuffer output;
            require(ure_fbe::DecryptGcm(input, secret.data(), secret.size(), &output), "valid authenticated GCM rejected");
            require(output.size() == size && std::memcmp(output.data(), plaintext.data(), size) == 0, "GCM plaintext boundary changed");
        }
        require(std::all_of(input->begin(), input->end(), [](uint8_t byte) { return byte == 0; }), "keystore plaintext was not cleansed");
        verifyCleanup();
    }
    std::puts("Actual production EVP helper authenticates synthetic AES-256-GCM vectors across plaintext boundaries.");
}

static void tamper() {
    const auto secret = key();
    const auto blob = encrypt(std::string(64, 'x'));
    for (size_t index = 0; index < blob.size(); ++index) {
        std::optional<std::vector<uint8_t>> input = blob; (*input)[index] ^= 1;
        { ure_fbe::SecretBuffer output;
          require(!ure_fbe::DecryptGcm(input, secret.data(), secret.size(), &output), "GCM tamper was accepted");
          require(!output.data() && output.size() == 0, "unauthenticated plaintext remained published"); }
        verifyCleanup();
    }
    for (size_t length = 0; length < blob.size(); ++length) {
        std::optional<std::vector<uint8_t>> input(std::in_place, blob.begin(), blob.begin() + length);
        { ure_fbe::SecretBuffer output;
          require(!ure_fbe::DecryptGcm(input, secret.data(), secret.size(), &output), "truncated GCM was accepted"); }
        verifyCleanup();
    }
    auto wrongKey = secret; wrongKey[0] ^= 1;
    std::optional<std::vector<uint8_t>> input = blob;
    { ure_fbe::SecretBuffer output; require(!ure_fbe::DecryptGcm(input, wrongKey.data(), wrongKey.size(), &output), "wrong GCM key was accepted"); }
    verifyCleanup();
    std::puts("Every synthetic IV/ciphertext/tag byte mutation, truncation and wrong key fails without publishing plaintext.");
}

static void absent() {
    const auto secret = key();
    std::optional<std::vector<uint8_t>> input;
    {
        ure_fbe::SecretBuffer output;
        require(output.allocate(32), "stale output setup failed"); std::memset(output.data(), 0x5a, output.size());
        require(!ure_fbe::DecryptGcm(input, secret.data(), secret.size(), &output) && !output.data(), "absent keystore output accepted");
        input.emplace();
        require(!ure_fbe::DecryptGcm(input, secret.data(), secret.size(), &output), "empty keystore output accepted");
        input = encrypt("synthetic");
        require(!ure_fbe::DecryptGcm(input, nullptr, 32, &output), "null AES key accepted");
        require(!ure_fbe::DecryptGcm(input, secret.data(), 31, &output), "wrong AES key size accepted");
        require(!ure_fbe::DecryptGcm(input, secret.data(), 32, nullptr), "null plaintext output accepted");
    }
    verifyCleanup();
    std::puts("Absent/empty optional outputs and invalid key/output arguments fail before dereference or decryption.");
}

static void failures() {
    const auto secret = key();
    std::optional<std::vector<uint8_t>> input = encrypt("synthetic authentication failure controls");
    for (const Failure stage : {Failure::Allocation, Failure::Context, Failure::Init, Failure::Iv, Failure::Update, Failure::Tag, Failure::Final}) {
        { ure_fbe::SecretBuffer output; failure = stage;
          require(!ure_fbe::DecryptGcm(input, secret.data(), secret.size(), &output) && !output.data() && output.size() == 0,
                  "EVP failure published partial plaintext"); failure = Failure::None; }
        verifyCleanup();
    }
    std::puts("Allocation, context and every EVP failure stage fail closed with cleared output and released context.");
}

static void zeroize() {
    { ure_fbe::SecretBuffer empty; empty.truncate(0); require(!empty.data() && empty.size() == 0, "empty secret truncate changed state"); }
    { ure_fbe::SecretBuffer bytes; require(bytes.allocate(64), "zeroization setup failed"); std::memset(bytes.data(), 0x5a, bytes.size());
      // Keep these synthetic allocations observable in the removed-cleanse
      // control; otherwise the optimizer can eliminate malloc/write/free.
      asm volatile("" : : "r"(bytes.data()) : "memory"); }
    verifyCleanup();
    auto* adopted = static_cast<unsigned char*>(std::malloc(64)); require(adopted != nullptr, "adopted key setup failed");
    std::memset(adopted, 0x5a, 64);
    { ure_fbe::SecretBuffer bytes(adopted, 64); asm volatile("" : : "r"(bytes.data()) : "memory"); }
    verifyCleanup();
    std::optional<std::vector<uint8_t>> bytes(std::in_place, 64, 0x5a);
    { ure_fbe::ScopedOptionalCleanse guard(bytes); }
    require(std::all_of(bytes->begin(), bytes->end(), [](uint8_t byte) { return byte == 0; }), "keystore plaintext was not cleansed");
    std::puts("Output, adopted application key and keystore temporary bytes are cleansed on scope exit.");
}

int main(int argc, char** argv) {
    require(argc == 2, "scenario required"); const std::string mode = argv[1];
    if (mode == "positive") positive();
    else if (mode == "tamper") tamper();
    else if (mode == "absent") absent();
    else if (mode == "failures") failures();
    else if (mode == "zeroize") zeroize();
    else fail("unknown scenario");
}
