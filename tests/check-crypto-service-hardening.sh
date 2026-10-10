#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Source-only patch/guard controls; no compilation, service, device or key I/O.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
work=$(mktemp -d "${TMPDIR:-/tmp}/uke-crypto-source-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

stage() {
    local kind=$1 source=$2 pin=$3 candidate=$4 file patch
    shift 4
    [[ $(git -C "$source" rev-parse HEAD) == "$pin" ]]
    mkdir "$work/$kind" "$work/$kind-before"
    local includes=()
    for file in "$@"; do
        git -C "$source" show "$pin:$file" > "$work/$kind/$file"
        chmod --reference="$source/$file" "$work/$kind/$file"
        includes+=("--include=$file")
    done
    while IFS= read -r patch; do
        [[ $patch == "$candidate" ]] && continue
        git -C "$work/$kind" apply "${includes[@]}" "$component/patches/$patch"
    done < "$component/configs/$kind-patches.list"
    for file in "$@"; do cp "$work/$kind/$file" "$work/$kind-before/$file"; done
    git -C "$work/$kind" apply --check "$component/patches/$candidate"
    git -C "$work/$kind" apply "$component/patches/$candidate"
}
stage recovery "$tree/bootable/recovery" 3d733672081bca3af42475a286145f4a8cdce4e7 \
    0072-recovery-crypto-service-hardening.patch partition.cpp
stage vold "$tree/system/vold" 953de9608eb78380b3c4e39e801c2bc0af7dbddc \
    0073-vold-crypto-service-hardening.patch Decrypt.cpp Keystore.cpp

before() {
    local first second
    first=$(awk -v needle="$2" 'index($0,needle) {print NR; exit}' "$1")
    second=$(awk -v needle="$3" 'index($0,needle) {print NR; exit}' "$1")
    [[ -n $first && -n $second && $first -lt $second ]]
}
guards() {
    local file=$1
    before "$file" 'if (!keystore) return disk_decryption_secret_key;' 'keystore->getKeyEntry(' &&
    before "$file" 'if (!keyResponse.iSecurityLevel) return disk_decryption_secret_key;' 'keyResponse.iSecurityLevel->createOperation(' &&
    before "$file" 'if (!encOperationResponse.iOperation) return disk_decryption_secret_key;' 'encOperationResponse.iOperation->finish(' &&
    before "$file" 'if (!result.isOk()) return Free_Return(retval, weaver_key, &pwd);' 'if (rsp.statusCode >=' &&
    before "$file" 'if (gkResponse.payload().size() != sizeof(hw_auth_token_t))' 'reinterpret_cast<const hw_auth_token_t*>' &&
    rg -Uq 'if \(service == NULL\) \{[^}]*return Free_Return\(retval, weaver_key, &pwd\);' "$file" &&
    rg -qF 'if (!binder_result.isOk()) return Free_Return(retval, weaver_key, &pwd);' "$file"
}
guards "$work/vold/Decrypt.cpp"
rg -Uq 'if \(logKeystore2ExceptionIfPresent\(rc, "getSecurityLevel"\)\) \{\n[[:space:]]*securityLevel.reset\(\);' "$work/vold/Keystore.cpp"
awk '/^bool TWPartition::Decrypt\(/ {copy=1} copy {print} copy && /^}/ {exit}' \
    "$work/recovery/partition.cpp" > "$work/stub"
rg -qF 'ure_legacy_write_guard(ure::LegacyWrite::EncryptionAccess)' "$work/stub"
rg -qF 'return false;' "$work/stub"
if rg -q 'Password|LOG|printf|return (1|true);' "$work/stub"; then
    echo 'Legacy stub logs a secret or reports success' >&2; exit 1
fi

variant() {
    awk -v recovery="$2" '
        /^#ifdef __ANDROID_RECOVERY__$/ {branch=1; next}
        branch && /^#else$/ {branch=2; next}
        branch && /^#endif$/ {branch=0; next}
        !branch || (branch==1 && recovery) || (branch==2 && !recovery) {print}
        END {if (branch) exit 1}' "$1"
}
for file in Decrypt.cpp Keystore.cpp; do
    variant "$work/vold/$file" 0 > "$work/$file-normal"
    variant "$work/vold/$file" 1 > "$work/$file-recovery"
    # Preserve every non-recovery registration call, including maintenance.
    cmp <(rg 'AServiceManager_(waitForService|checkService)|IGatekeeper::(getService|tryGetService)' "$work/vold-before/$file") \
        <(rg 'AServiceManager_(waitForService|checkService)|IGatekeeper::(getService|tryGetService)' "$work/$file-normal")
done
if rg -q 'AServiceManager_waitForService|IGatekeeper::getService' "$work/Decrypt.cpp-recovery"; then
    echo 'Recovery credential path still waits for registration' >&2; exit 1
fi
awk '/^Keystore::Keystore\(\)/ {copy=1} copy {print} copy && /^}/ {exit}' \
    "$work/Keystore.cpp-recovery" > "$work/constructor"
rg -qF 'AServiceManager_checkService(keystore2_service_name)' "$work/constructor"
if rg -q 'waitForService' "$work/constructor"; then exit 1; fi

for mutant in keystore security-level operation verify token-length authorization auth-token; do
    case $mutant in
        keystore) removed='if (!keystore) return disk_decryption_secret_key;' ;;
        security-level) removed='if (!keyResponse.iSecurityLevel) return disk_decryption_secret_key;' ;;
        operation) removed='if (!encOperationResponse.iOperation) return disk_decryption_secret_key;' ;;
        verify) removed='if (!result.isOk()) return Free_Return(retval, weaver_key, &pwd);' ;;
        token-length) removed='if (gkResponse.payload().size() != sizeof(hw_auth_token_t))' ;;
        authorization) removed='if (service == NULL)' ;;
        auth-token) removed='if (!binder_result.isOk()) return Free_Return(retval, weaver_key, &pwd);' ;;
    esac
    awk -v removed="$removed" '!index($0,removed)' "$work/vold/Decrypt.cpp" > "$work/mutant"
    if guards "$work/mutant"; then
        printf 'Missing crypto guard escaped source control: %s\n' "$mutant" >&2; exit 1
    fi
done
printf '%s\n' 'Crypto patch application, source guard ordering, seven removed-guard controls and unchanged non-recovery registration calls passed. No compilation, Binder runtime, TEE or FBE acceptance.'
