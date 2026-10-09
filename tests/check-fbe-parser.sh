#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Synthetic host records and exact production parsers; no credential/device I/O.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-check}
[[ $mode == check || $mode == candidate ]]
source_dir="$component/src/upstream/orangefox-android16/system/vold"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(git -C "$source_dir" rev-parse HEAD) == 953de9608eb78380b3c4e39e801c2bc0af7dbddc ]]
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
mkdir -p "$component/build"
work=$(mktemp -d "$component/build/fbe-parser-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
cp "$source_dir/Decrypt.cpp" "$work/Decrypt.cpp"
if [[ $mode == candidate && ! -f $source_dir/ure-fbe-parser.hpp ]]; then
    (cd "$work" && patch --batch --forward -p1 < "$component/patches/0054-bound-synthetic-password-records.patch")
else
    cp "$source_dir/ure-fbe-parser.hpp" "$work/ure-fbe-parser.hpp"
fi
extract() {
    awk '/^struct password_data_struct \{/ {copy=1} /^namespace android \{/ {exit} copy {print}' "$1" > "$2"
    rg -q '^bool Get_Weaver_Data' "$2"
}
extract "$work/Decrypt.cpp" "$work/fbe-production-functions.inc"
for contract in 'ParsePasswordData(pwd_data, &parsed)' 'CheckScrypt(pwd->scryptN, pwd->scryptR, pwd->scryptP, &cost)' 'ParseWeaver(weaver_data, &slot)' 'ParseSpblob(spblob_data, &parsed_blob)' 'ParseGcmPayload(std::string_view('; do
    rg -q -F "$contract" "$work/Decrypt.cpp"
done
if rg -q 'const int\* intptr|1 << pwd->scrypt|cipher_text_str\(byteptr' "$work/Decrypt.cpp"; then
    echo 'Unsafe legacy record parsing remains' >&2; exit 1
fi
mkdir "$work/synthetic"
flags=(-std=c++17 -Wall -Wextra -Werror -UNDEBUG -O1 -I "$work")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g); fi
    timeout 45 "$compiler" "${flags[@]}" "${instrumentation[@]}" "$component/tests/ure/fbe_parser.cpp" -o "$work/test-$flavor"
    for scenario in weaver password scrypt blobs read fuzz; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
            timeout 20 "$work/test-$flavor" "$scenario" "$work/synthetic"
    done
done
reject_mutant() {
    local label=$1 scenario=$2 message=$3
    local mutant="$work/$label"
    mkdir "$mutant"
    cp "$work/fbe-production-functions.inc" "$mutant/"
    case $label in
        weaver-offset) sed 's/size_t offset = 1;/size_t offset = 4;/' "$work/ure-fbe-parser.hpp" > "$mutant/ure-fbe-parser.hpp";;
        extensions) sed '/if (offset != data.size()) return false;/d' "$work/ure-fbe-parser.hpp" > "$mutant/ure-fbe-parser.hpp";;
        memory) sed '/if (n > (kMaxScryptMemory - fixed) \/ block) return false;/d' "$work/ure-fbe-parser.hpp" > "$mutant/ure-fbe-parser.hpp";;
        work) sed 's/ || n > kMaxScryptWork \/ r \/ p//' "$work/ure-fbe-parser.hpp" > "$mutant/ure-fbe-parser.hpp";;
        inner-tag) sed 's/data.size() < kGcmIvBytes + kGcmTagBytes/data.size() < kGcmIvBytes/' "$work/ure-fbe-parser.hpp" > "$mutant/ure-fbe-parser.hpp";;
    esac
    timeout 45 "$compiler" -std=c++17 -Wall -Wextra -Werror -UNDEBUG -O1 -I "$mutant" "$component/tests/ure/fbe_parser.cpp" -o "$mutant/test"
    if "$mutant/test" "$scenario" "$work/synthetic" > "$mutant/log" 2>&1; then
        printf 'Removed %s bound unexpectedly passed\n' "$label" >&2; exit 1
    fi
    rg -q -F "$message" "$mutant/log"
}
reject_mutant weaver-offset weaver 'FAIL: Weaver byte-one big-endian slot decode failed'
reject_mutant extensions password 'FAIL: truncated password record accepted'
reject_mutant memory scrypt 'FAIL: scrypt memory budget bypassed'
reject_mutant work scrypt 'FAIL: scrypt work budget bypassed'
reject_mutant inner-tag blobs 'FAIL: truncated inner GCM tag accepted'
printf '%s\n' 'Exact production FBE record/KDF admission and bounded-file controls passed under native/ASAN/UBSAN; five removed-bound mutants rejected. Crypto flags and runtime access remain disabled.'
