#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Source-extracted host controls only. No Android services, secrets, mounts or device I/O.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$component/src/upstream/orangefox-android16/system/vold"
pin=953de9608eb78380b3c4e39e801c2bc0af7dbddc
candidate_patch=0076-handle-missing-keystore-results.patch
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(git -C "$source_dir" rev-parse HEAD) == "$pin" ]]
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/crypto-nullability-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/before" "$work/after"
candidate=$work
for file in Keystore.cpp Decrypt.cpp; do
    git -C "$source_dir" show "$pin:$file" > "$work/before/$file"
done
while IFS= read -r patch; do
    [[ $patch == "$candidate_patch" ]] && continue
    git -C "$work/before" apply --include=Keystore.cpp --include=Decrypt.cpp "$component/patches/$patch"
done < "$component/configs/vold-patches.list"
cp "$work/before/Keystore.cpp" "$work/before/Decrypt.cpp" "$work/after/"
git -C "$work/after" apply --check "$component/patches/$candidate_patch"
git -C "$work/after" apply "$component/patches/$candidate_patch"
extract() {
    awk -v needle="$2" 'index($0,needle)==1 {copy=1} copy {print} copy && /^}/ {exit}' "$1"
}
normal() {
    # Every inserted conditional is a top-level recovery-only branch.
    awk '/^#ifdef __ANDROID_RECOVERY__$/ {branch=1;next}
         branch && /^#else$/ {branch=2;next}
         branch && /^#endif$/ {branch=0;next}
         !branch || branch==2 {print} END {if(branch)exit 1}' "$1"
}
for file in Keystore.cpp Decrypt.cpp; do
    normal "$candidate/before/$file" > "$work/before-$file"
    normal "$candidate/after/$file" > "$work/after-$file"
    cmp "$work/before-$file" "$work/after-$file"
done
assemble() {
    for function in 'bool KeystoreOperation::finish(' 'bool Keystore::generateKey(' \
        'bool Keystore::exportKey(' 'bool Keystore::deleteKey(' 'KeystoreOperation Keystore::begin('; do
        extract "$1" "$function"
    done
}
assemble "$candidate/after/Keystore.cpp" > "$work/production-methods.inc"
flags=(-std=c++20 -Wall -Wextra -Werror -Wno-missing-field-initializers -D__ANDROID_RECOVERY__ \
       -D_GLIBCXX_ASSERTIONS -UNDEBUG -O1 -I "$work")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/crypto_nullability.cpp" -o "$work/test-$flavor"
    for scenario in missing-security-level valid-security-level absent-plaintext valid-plaintext; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            timeout 15 "$work/test-$flavor" "$scenario"
    done
done
# Extract the real declaration and default-password copy, including the recovery conditional.
awk '/^#ifdef __ANDROID_RECOVERY__$/ {pending=$0 ORS; next}
     /unsigned char password_token\[PASSWORD_TOKEN_SIZE\]/ {
         if(pending!="") {print pending; in_decl=1; pending=""}; print; next}
     in_decl {print; if(/^#endif$/)in_decl=0;next}
     /std::string defpassword = "default-password";/ || /memcpy\(password_token, defpassword.data\(\), defpassword.length\(\)\);/ {print}' \
    "$candidate/after/Decrypt.cpp" > "$work/token-body"
{
    printf '#include <cassert>\n#include <cstring>\n#include <string>\n#define PASSWORD_TOKEN_SIZE 32\nint main(){\n'
    cat "$work/token-body"
    printf 'assert(std::memcmp(password_token,"default-password",16)==0);\nfor(unsigned i=16;i<32;++i)assert(password_token[i]==0);\n}\n'
} > "$work/token.cpp"
"$compiler" "${flags[@]}" -ftrivial-auto-var-init=pattern "$work/token.cpp" -o "$work/token"
"$work/token"
ulimit -c 0
refuse() {
    set +e
    bash -c 'timeout 15 "$@"; exit "$?"' _ "$@" > "$work/refusal.log" 2>&1
    local status=$?
    set -e
    [[ $status == 134 || $status == 139 ]]
}
for method in generateKey exportKey deleteKey begin; do
    awk -v target="Keystore::$method(" '
        index($0,target) {inside=1}
        !(inside && index($0,"if (!securityLevel) return")) {print}
        inside && /^}/ {inside=0}' "$candidate/after/Keystore.cpp" > "$work/no-security-guard"
    assemble "$work/no-security-guard" > "$work/production-methods.inc"
    "$compiler" "${flags[@]}" "$component/tests/ure/crypto_nullability.cpp" -o "$work/mutant"
    refuse "$work/mutant" "missing-security-level:$method"
done
sed '/if (output && !out_vec)/,/^    }/d' "$candidate/after/Keystore.cpp" > "$work/no-optional-guard"
assemble "$work/no-optional-guard" > "$work/production-methods.inc"
"$compiler" "${flags[@]}" "$component/tests/ure/crypto_nullability.cpp" -o "$work/mutant"
refuse "$work/mutant" absent-plaintext
sed 's/password_token\[PASSWORD_TOKEN_SIZE\]{}/password_token[PASSWORD_TOKEN_SIZE]/' "$work/token.cpp" > "$work/token-mutant.cpp"
"$compiler" "${flags[@]}" -ftrivial-auto-var-init=pattern "$work/token-mutant.cpp" -o "$work/token-mutant"
refuse "$work/token-mutant"
printf '%s\n' 'Production method bodies: unavailable/valid security level, absent/empty/binary plaintext and Binder failure passed native/ASAN/UBSAN stand-ins. Default-password token tail is zero; six removed-fix controls refused; normal-Android source is unchanged. No Android build, HAL, TEE or decryption acceptance.'
