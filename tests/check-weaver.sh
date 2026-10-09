#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Exact production wrapper with inert host Binder/HIDL boundaries, synthetic
# secrets, and no services, devices, credentials or persistent key operations.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_dir="$component/src/upstream/orangefox-android16/system/vold"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(git -C "$source_dir" rev-parse HEAD) == 953de9608eb78380b3c4e39e801c2bc0af7dbddc ]]
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
work=$(mktemp -d "$component/build/weaver-check-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
cp "$source_dir/"{Weaver1.cpp,Weaver1.h,ure-weaver-policy.hpp} "$work/"
if rg -q 'waitForService|::getService\(' "$work/Weaver1.cpp"; then
    echo 'Unbounded Weaver service registration wait remains' >&2; exit 1
fi
rg -q -F 'WeaverVerify(wd.slot, weaver_key, SHA512_DIGEST_LENGTH,' "$source_dir/Decrypt.cpp"
rg -q -F '&weaver_payload, &weaver_retry_ms)' "$source_dir/Decrypt.cpp"
read -r -a crypto_libs <<< "$(pkg-config --libs openssl)"
flags=(-std=c++17 -Wall -Wextra -Werror -UNDEBUG -O1 -g -I "$work" -I "$component/tests/ure/weaver-stubs")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then
        instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
    fi
    timeout 45 "$compiler" "${flags[@]}" "${instrumentation[@]}" \
        "$work/Weaver1.cpp" "$component/tests/ure/weaver.cpp" "${crypto_libs[@]}" -o "$work/$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$work/$flavor"
done
for mutant in key-size slot-size config-size reply-size delay; do
    directory="$work/$mutant"
    mkdir "$directory"
    cp "$work/"{Weaver1.cpp,Weaver1.h,ure-weaver-policy.hpp} "$directory/"
    case $mutant in
        key-size) sed -i 's/available_bytes >= key_bytes/available_bytes >= 0/' "$directory/ure-weaver-policy.hpp";;
        slot-size) sed -i 's/slot < slots/slot <= slots/' "$directory/ure-weaver-policy.hpp";;
        config-size) sed -i 's/key_bytes <= kMaxKeyBytes/key_bytes <= 4096/g' "$directory/ure-weaver-policy.hpp";;
        reply-size) sed -i 's/response.value.size() != valueSize/response.value.size() < valueSize/g' "$directory/Weaver1.cpp";;
        delay) sed -i 's/\*output = static_cast<uint64_t>(milliseconds);/*output = 0;/' "$directory/ure-weaver-policy.hpp";;
    esac
    timeout 45 "$compiler" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -UNDEBUG -O1 \
        -I "$directory" -I "$component/tests/ure/weaver-stubs" \
        "$directory/Weaver1.cpp" "$component/tests/ure/weaver.cpp" "${crypto_libs[@]}" -o "$directory/test"
    status=0
    timeout 20 "$directory/test" > "$directory/result.log" 2>&1 || status=$?
    [[ $status == 1 ]]
    rg -q '^FAIL:' "$directory/result.log"
    printf 'Rejected removed-bound mutant: %s\n' "$mutant"
done
printf '%s\n' 'Production Weaver native/ASAN/UBSAN and five negative controls passed. Service registration does not wait; Binder transactions, session throttle enforcement and physical FBE access remain separate.'
