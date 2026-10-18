#!/usr/bin/env bash
# Exact reviewed callers with inert host boundaries; never attach a device.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$0"
fi
tree="$component/src/upstream/orangefox-android16"
vold="$tree/system/vold"; recovery="$tree/bootable/recovery"
compiler="$tree/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
bash "$component/scripts/prepare-reviewed-patches.sh" "$vold" check vold
bash "$component/scripts/prepare-recovery-patches.sh" "$recovery" check
work=$(mktemp -d "$component/build/android-readonly-access-XXXXXXXX")
extract() { awk -v prefix="$2" 'index($0,prefix)==1 {copy=1} copy {print} copy && /^}/ {exit}' "$1" > "$3"; [[ -s $3 ]]; }
extract "$vold/MetadataCrypt.cpp" 'static bool mount_via_fs_mgr(' "$work/mount-metadata.inc"
extract "$vold/MetadataCrypt.cpp" 'static bool read_key(' "$work/read-metadata-key.inc"
extract "$vold/FsCrypt.cpp" 'bool fscrypt_initialize_systemwide_keys(' "$work/systemwide-keys.inc"
extract "$vold/FsCrypt.cpp" 'bool fscrypt_init_user0(' "$work/user0-keys.inc"
extract "$vold/FsCrypt.cpp" 'static bool verify_existing_policy(' "$work/verify-policy.inc"
extract "$vold/FsCrypt.cpp" 'bool fscrypt_prepare_user_storage(' "$work/prepare-user-storage.inc"
extract "$recovery/partitionmanager.cpp" 'bool TWPartitionManager::Read_Only_Android_Access(' "$work/android-access.inc"
extract "$vold/KeystoreInfo.cpp" 'std::string KeystoreInfo::getHandle(' "$work/locksettings-handle.inc"
flags=(-std=c++20 -Wall -Wextra -Werror -D__ANDROID_RECOVERY__ -UNDEBUG -O1 -g
    -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -I"$work")
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
ccache "$compiler" "${flags[@]}" -Wl,--wrap=mount -Wl,--wrap=statvfs \
    -Wl,--wrap=open -Wl,--wrap=fstat -Wl,--wrap=stat -Wl,--wrap=fstatvfs -Wl,--wrap=ioctl \
    "$component/tests/ure/android_readonly_access.cpp" -o "$work/admission"
"$work/admission"
# Use the pinned SQLite default source, with host options; this is not an Android runtime test.
[[ $(git -C "$tree/external/sqlite" rev-parse HEAD) == 28e3f5878182658a5c34d3ce0859e950f2885aba ]]
sqlite=$(realpath -e "$tree/external/sqlite/dist/sqlite-default")
[[ $sqlite == "$tree/external/sqlite/dist/"* && -f $sqlite/sqlite3.c ]]
ccache "${compiler%++}" -O1 -DSQLITE_THREADSAFE=2 -c "$sqlite/sqlite3.c" -o "$work/sqlite.o"
ccache "$compiler" "${flags[@]}" -I"$sqlite" -Wl,--wrap=statvfs -Wl,--wrap=sqlite3_open_v2 \
    "$component/tests/ure/locksettings_readonly.cpp" "$work/sqlite.o" -lpthread -ldl -lm -o "$work/locksettings"
"$work/locksettings" "$work/locksettings.db"
for header in KeyBuffer.h aidl/android/hardware/security/keymint/ErrorCode.h \
    aidl/android/hardware/security/keymint/IKeyMintDevice.h android/binder_manager.h \
    keymint_support/authorization_set.h openssl/mem.h; do
    mkdir -p -- "$(dirname -- "$work/$header")"
    printf '#include "fake.hpp"\n' > "$work/$header"
done
{
    printf '#include "reader_fixture.hpp"\n'
    cat <<'CPP'
namespace android::vold {
struct KeyAuthentication {
    std::string secret;
    explicit KeyAuthentication(std::string value):secret(std::move(value)) {}
    bool usesKeystore() const { return secret.empty(); }
};
static std::string generateAppId(const KeyAuthentication& auth,const std::string& hash) { return hash+auth.secret; }
static bool decryptWithoutKeystore(const std::string& app,const std::string&,KeyBuffer* key) {
    key->assign(32,9); return app==std::string(64,'h')+"right";
}
}
static int readonly_fixture_open(const char* path,int flags) {
    assert((flags&O_ACCMODE)==O_RDONLY && (flags&O_DIRECTORY) && (flags&O_NOFOLLOW));
    const int descriptor=::open(path,flags);
    io_fixture::directory=descriptor;
    return descriptor;
}
#define open readonly_fixture_open
namespace android::vold {
CPP
    awk '/^static bool readMetadataKeyFile\(/ {copy=1} copy && /^#endif/ {exit} copy {print}' "$vold/KeyStorage.cpp"
    printf '}\n'
    cat "$component/tests/crypto-existing-key/storage_access.cpp"
} > "$work/storage-reader.cpp"
mkdir "$work/production"
cp "$vold/ExistingKeyMint.cpp" "$vold/ExistingKeyMint.h" "$work/production/"
ccache "$compiler" "${flags[@]}" -I"$component/tests/crypto-existing-key" -I"$work/production" \
    "$work/production/ExistingKeyMint.cpp" "$work/storage-reader.cpp" -o "$work/storage-reader"
"$work/storage-reader" "$work/key-records"
mkdir "$work/before" "$work/after"
git init -q "$work/before"
git init -q "$work/after"
for file in MetadataCrypt.cpp FsCrypt.cpp KeyStorage.cpp KeyStorage.h Decrypt.cpp KeystoreInfo.cpp Android.bp; do
    git -C "$vold" show "HEAD:$file" > "$work/before/$file"
done
while IFS= read -r patch; do
    [[ $patch == 0086-unlock-existing-android-storage-read-only.patch ]] && break
    git -C "$work/before" apply --include=MetadataCrypt.cpp --include=FsCrypt.cpp --include=KeyStorage.cpp \
        --include=KeyStorage.h --include=Decrypt.cpp --include=KeystoreInfo.cpp --include=Android.bp "$component/patches/$patch"
done < "$component/configs/vold-patches.list"
cp "$work/before/"* "$work/after/"
git -C "$work/after" apply "$component/patches/0086-unlock-existing-android-storage-read-only.patch"
for file in MetadataCrypt.cpp FsCrypt.cpp KeyStorage.cpp KeyStorage.h Decrypt.cpp KeystoreInfo.cpp; do
    cmp "$work/after/$file" "$vold/$file"
    for state in before after; do
        sed '/^#include /d' "$work/$state/$file" | "$compiler" -E -P -x c++ - | sed '/^[[:space:]]*$/d' > "$work/$state-$file.normal"
    done
    cmp "$work/before-$file.normal" "$work/after-$file.normal"
done
printf '%s\n' 'Android read-only caller, native mount/key policy, SQLite WAL and unchanged normal-Android source controls passed; target build, secure providers and tablet unlock remain unaccepted.'
