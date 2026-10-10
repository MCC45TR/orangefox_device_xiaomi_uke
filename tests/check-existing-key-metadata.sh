#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Production-source host controls; no service startup, credentials or device I/O.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
source_dir="$tree/system/vold"
pin=953de9608eb78380b3c4e39e801c2bc0af7dbddc
compiler="$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(git -C "$source_dir" rev-parse HEAD) == "$pin" ]]
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
bash "$component/scripts/prepare-reviewed-patches.sh" "$source_dir" check vold
work=$(mktemp -d "$component/build/existing-key-controls-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/before"
git init -q "$work/before"
for file in KeyStorage.cpp KeyStorage.h Android.bp; do
    git -C "$source_dir" show "$pin:$file" > "$work/before/$file"
done
while IFS= read -r patch; do
    [[ $patch == 0082-read-existing-metadata-keys-without-keystore2.patch ]] && continue
    git -C "$work/before" apply --include=KeyStorage.cpp --include=KeyStorage.h --include=Android.bp "$component/patches/$patch"
done < "$component/configs/vold-patches.list"
normal() {
    awk '/^#ifdef __ANDROID_RECOVERY__$/ {branch=1;next}
         branch && /^#else$/ {branch=2;next}
         branch && /^#endif$/ {branch=0;next}
         !branch || branch==2 {print} END {if(branch)exit 1}' "$1" | sed '/^$/d'
}
for file in KeyStorage.cpp KeyStorage.h; do
    cmp <(normal "$work/before/$file") <(normal "$source_dir/$file")
done
for header in KeyBuffer.h aidl/android/hardware/security/keymint/ErrorCode.h \
    aidl/android/hardware/security/keymint/IKeyMintDevice.h android/binder_manager.h \
    keymint_support/authorization_set.h openssl/mem.h; do
    mkdir -p -- "$(dirname -- "$work/$header")"
    printf '#include "fake.hpp"\n' > "$work/$header"
done
mkdir "$work/production"
cp -- "$source_dir/ExistingKeyMint.cpp" "$source_dir/ExistingKeyMint.h" "$work/production/"
flags=(-std=c++20 -D__ANDROID_RECOVERY__ -Wall -Wextra -Werror -UNDEBUG -g -O1
    -fsanitize=address,undefined -fno-sanitize=vptr
    -I"$work" -I"$component/tests/crypto-existing-key" -I"$work/production")
"$compiler" "${flags[@]}" "$work/production/ExistingKeyMint.cpp" \
    "$component/tests/crypto-existing-key/existing_key.cpp" -o "$work/client"
timeout 20 "$work/client"
{
    printf '#include "reader_fixture.hpp"\nnamespace android::vold {\n'
    awk '/^static bool readMetadataKeyFile\(/ {copy=1} copy && /^#endif/ {exit} copy {print}' "$source_dir/KeyStorage.cpp"
    printf '}\n'
    cat "$component/tests/crypto-existing-key/reader_controls.cpp"
} > "$work/reader.cpp"
"$compiler" "${flags[@]}" "$work/production/ExistingKeyMint.cpp" "$work/reader.cpp" -o "$work/reader"
timeout 20 "$work/reader" "$work/records"
ulimit -c 0
refuse() {
    local status=0
    bash -c 'timeout 20 "$@"; exit "$?"' _ "$@" > "$work/refusal.log" 2>&1 || status=$?
    [[ $status == 134 ]]
}
sed 's/KeyPurpose::DECRYPT/KeyPurpose::ENCRYPT/' "$source_dir/ExistingKeyMint.cpp" > "$work/mutant.cpp"
"$compiler" "${flags[@]}" "$work/mutant.cpp" "$component/tests/crypto-existing-key/existing_key.cpp" -o "$work/mutant"
refuse "$work/mutant"
sed 's/!metadataCharacteristicsSafe(characteristics)/false/' "$source_dir/ExistingKeyMint.cpp" > "$work/mutant.cpp"
"$compiler" "${flags[@]}" -Wno-unused-function "$work/mutant.cpp" \
    "$component/tests/crypto-existing-key/existing_key.cpp" -o "$work/mutant"
refuse "$work/mutant"
sed 's/(binding != MetadataKeyBinding::Unbound \&\& binding != MetadataKeyBinding::Bound) ||/false ||/' \
    "$work/reader.cpp" > "$work/mutant.cpp"
"$compiler" "${flags[@]}" "$work/production/ExistingKeyMint.cpp" "$work/mutant.cpp" -o "$work/mutant"
refuse "$work/mutant" "$work/mutant-records"
if "$compiler" -std=c++20 -I"$work" -I"$component/tests/crypto-existing-key" -I"$work/production" \
    -fsyntax-only "$work/production/ExistingKeyMint.cpp" > "$work/android.log" 2>&1; then
    echo 'Normal Android guard escaped' >&2; exit 1
fi
rg -q 'recovery-only client' "$work/android.log"
printf '%s\n' 'Existing-key restrictions, authenticated output, bounded record reads and three removed-fix controls passed; normal Android is unchanged. No Binder/TEE or physical unlock acceptance.'
