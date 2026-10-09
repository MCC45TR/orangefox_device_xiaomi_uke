#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Real host EVP with synthetic buffers only; no credential, Binder or device I/O.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-check}
[[ $mode == check || $mode == candidate ]]
source_dir="$component/src/upstream/orangefox-android16/system/vold"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(git -C "$source_dir" rev-parse HEAD) == 953de9608eb78380b3c4e39e801c2bc0af7dbddc ]]
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/fbe-gcm-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
cp "$source_dir/Decrypt.cpp" "$work/Decrypt.cpp"
if [[ $mode == candidate && ! -f $source_dir/ure-fbe-parser.hpp ]]; then
    (cd "$work" && patch --batch --forward -p1 < "$component/patches/0054-bound-synthetic-password-records.patch")
else cp "$source_dir/ure-fbe-parser.hpp" "$work/ure-fbe-parser.hpp"; fi
if [[ $mode == candidate && ! -f $source_dir/ure-fbe-gcm.hpp ]]; then
    (cd "$work" && patch --batch --forward -p1 < "$component/patches/0055-authenticate-synthetic-password-gcm.patch")
else cp "$source_dir/ure-fbe-gcm.hpp" "$work/ure-fbe-gcm.hpp"; fi
for contract in 'ure_fbe::ScopedOptionalCleanse plaintext_cleanup(optPlaintext)' 'ure_fbe::SecretBuffer personalized_application_id' 'ure_fbe::DecryptGcm(optPlaintext, personalized_application_id.data(), 32, &secret_key)' 'secret_key.size() == 0'; do
    rg -q -F "$contract" "$work/Decrypt.cpp"
done
if rg -q 'optPlaintext->front\(|EVP_DecryptFinal_ex\(d_ctx|unsigned char tag\[AES_BLOCK_SIZE\]|actual_size - 16' "$work/Decrypt.cpp"; then
    echo 'Unauthenticated legacy extraction remains' >&2; exit 1
fi
read -r -a crypto_libs <<< "$(pkg-config --libs openssl)"
wrappers=()
for function in malloc free EVP_CIPHER_CTX_new EVP_CIPHER_CTX_free EVP_DecryptInit_ex EVP_CIPHER_CTX_ctrl EVP_DecryptUpdate EVP_DecryptFinal_ex; do
    wrappers+=("-Wl,--wrap=$function")
done
flags=(-std=c++17 -Wall -Wextra -Werror -D_GLIBCXX_ASSERTIONS -UNDEBUG -O1 "${wrappers[@]}")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    timeout 45 "$compiler" "${flags[@]}" "${instrumentation[@]}" -I "$work" "$component/tests/ure/fbe_gcm.cpp" "${crypto_libs[@]}" -o "$work/test-$flavor"
    for scenario in positive tamper absent failures zeroize; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            timeout 20 "$work/test-$flavor" "$scenario"
    done
done
reject_mutant() {
    local label=$1 scenario=$2 message=$3
    local mutant="$work/$label"
    mkdir "$mutant"
    cp "$work/ure-fbe-parser.hpp" "$mutant/"
    case $label in
        authentication) sed 's/EVP_DecryptFinal_ex(ctx.get(), output->data() + produced, \&finalSize) != 1/EVP_DecryptFinal_ex(ctx.get(), output->data() + produced, \&finalSize) < 0/' "$work/ure-fbe-gcm.hpp" > "$mutant/ure-fbe-gcm.hpp";;
        optional) sed 's/keySize != 32 || !input/keySize != 32/' "$work/ure-fbe-gcm.hpp" > "$mutant/ure-fbe-gcm.hpp";;
        output-cleanse) sed '/OPENSSL_cleanse(data_, capacity_);/d' "$work/ure-fbe-gcm.hpp" > "$mutant/ure-fbe-gcm.hpp";;
        optional-cleanse) sed 's/if (bytes_ \&\& !bytes_->empty()) OPENSSL_cleanse(bytes_->data(), bytes_->size());/(void)bytes_;/' "$work/ure-fbe-gcm.hpp" > "$mutant/ure-fbe-gcm.hpp";;
    esac
    timeout 45 "$compiler" "${flags[@]}" -I "$mutant" "$component/tests/ure/fbe_gcm.cpp" "${crypto_libs[@]}" -o "$mutant/test"
    set +e
    bash -c 'timeout 20 "$1" "$2"; exit "$?"' _ "$mutant/test" "$scenario" > "$mutant/log" 2>&1
    local status=$?
    set -e
    if [[ $label == optional ]]; then
        [[ $status == 134 ]]
        rg -q 'optional' "$mutant/log"
    else
        [[ $status == 1 ]]
        rg -q -F "$message" "$mutant/log"
    fi
}
ulimit -c 0
reject_mutant authentication tamper 'FAIL: GCM tamper was accepted'
reject_mutant optional absent ''
reject_mutant output-cleanse zeroize 'FAIL: secret allocation freed without cleansing'
reject_mutant optional-cleanse zeroize 'FAIL: keystore plaintext was not cleansed'
printf '%s\n' 'Exact production EVP authentication/cleanup helper passes native/ASAN/UBSAN synthetic controls; four removed-fix mutants rejected. Android BoringSSL/Binder and physical FBE acceptance remain separate.'
