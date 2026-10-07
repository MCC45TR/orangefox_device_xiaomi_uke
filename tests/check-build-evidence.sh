#!/usr/bin/env bash
# Actual host compiler and atomic output fixtures, classified separately from Android.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "$component/tests/check-build-evidence.sh" "$@"
fi
scratch=$(mktemp -d "$component/build/build-evidence-fixture-XXXXXXXX")
project="$scratch/project"
android="$scratch/android"
publisher="$scratch/publish-build-directory"
mkdir -p "$project"/{configs,patches,manifests,scripts,tests,src/{device,installer,inventory}} "$android"/{.repo,source}
printf 'patches/*.patch whitespace=-space-before-tab\n' > "$project/.gitattributes"
printf 'Host fixture license text\n' > "$project/LICENSE"
printf '#define VALUE 17\n' > "$android/source/value.hpp"
printf '#include "value.hpp"\nextern "C" void _start(){volatile int result=VALUE;(void)result;}\n' > "$android/source/fixture.cpp"
printf 'ignored-header.hpp\n' > "$android/source/.gitignore"
cp -p "$android/source/value.hpp" "$scratch/golden-header"
cp "$component/scripts/build-evidence-lib.sh" "$project/scripts/"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
CCACHE_DIR="$component/build/ccache/native" ccache /usr/bin/c++ -std=c++20 -O2 -Wall -Wextra -Werror \
    "$component/src/host/publish-build-directory.cpp" -o "$publisher"
git -C "$android/source" init -q
git -C "$android/source" -c user.name=MCC45TR -c user.email=75160848+MCC45TR@users.noreply.github.com add .
git -C "$android/source" -c user.name=MCC45TR -c user.email=75160848+MCC45TR@users.noreply.github.com commit -qm 'Create an isolated compiler input fixture'
pin=$(git -C "$android/source" rev-parse HEAD)
printf '<manifest><project name="source" revision="%s"/></manifest>\n' "$pin" > "$project/manifests/orangefox-android16-uke.lock.xml"
printf 'source\n' > "$android/.repo/project.list"
ln -s "$compiler" "$android/source/compiler-input"
cat > "$scratch/entry.sh" <<'ENTRY_EOF'
set -euo pipefail
component=$1; project=$2; android=$3; publisher=$4; mode=$5; job=$6
build_scripts="$component/scripts"
build_publisher=$publisher
source "$component/scripts/build-evidence-lib.sh"
case $mode in
    begin) build_begin "$project" "$android" "$job" host-fixture 1 2 0;;
    seal) build_seal "$project" "$android" "$job";;
    publish)
        build_publish "$job" "$android/out-public" host-fixture
        jq -cn --arg receipt "$(build_digest "$job/receipt/SHA256SUMS")" \
            '{schema_version:1,receipt_index_sha256:$receipt,command_status:0,new_oom_events:0,published:true,
              systemd_run_status:0,controller_confirmed:true}' > "$job/SERVICE-COMPLETION.json"
        chmod 0444 "$job/SERVICE-COMPLETION.json";;
    verify)
        work=$(mktemp -d "${job%/*}/verify-XXXXXXXX")
        build_verify "$project" "$android" "$job" "$android/out-public" host-fixture "$work";;
    integrity) build_receipt_integrity "$job" host-fixture;;
    production-class) build_receipt_integrity "$job" android-recovery;;
    *) exit 2;;
esac
ENTRY_EOF
entry() { bash "$scratch/entry.sh" "$component" "$project" "$android" "$publisher" "$@"; }
refuse() {
    if "$@" > "$scratch/refused.log" 2>&1; then echo 'Expected build-evidence refusal.' >&2; exit 1; fi
}
compile() {
    local output=$1
    mkdir -p "$output/target/product/uke/recovery/root/system/bin" "$output/target/product/uke/system/lib64" "$output/host/linux-x86/bin" "$output/host/linux-x86/lib64" "$output/soong"
    "$compiler" --target=aarch64-linux-android10000 -nostdlib -static -fuse-ld=lld -Wl,-e,_start \
        "$android/source/fixture.cpp" -o "$output/target/product/uke/recovery/root/system/bin/recovery"
    for binary in fastbootd uke-recoveryctl uke-recovery-install; do
        cp "$output/target/product/uke/recovery/root/system/bin/recovery" "$output/target/product/uke/recovery/root/system/bin/$binary"
    done
    cp "$output/target/product/uke/recovery/root/system/bin/recovery" "$output/target/product/uke/system/lib64/fixture.so"
    cp /usr/bin/true "$output/host/linux-x86/bin/fixture-host-tool"
    cp /usr/bin/true "$output/host/linux-x86/lib64/fixture-host-runtime.so"
    mkdir -p "$output/target/product/uke/vendor/etc"
    printf 'Fixture filesystem configuration\n' > "$output/target/product/uke/vendor/etc/fs_config_files"
    printf 'Fixture image for receipt mechanics; not an Android boot image.\n' > "$output/target/product/uke/recovery.img"
    printf '{"fixture":true}\n' > "$output/soong/soong.twrp_uke.variables"
    printf '{"fixture":true}\n' > "$output/soong/soong.twrp_uke.extra.variables"
    printf 'Fixed fixture environment\n' > "$output/soong/soong.environment.available"
    printf 'Fixture build graph\n' > "$output/build-twrp_uke.ninja"
    ln -s /system/bin/recovery "$output/target/product/uke/recovery/root/system/bin/runtime-alias"
}
job="$scratch/job-fresh"
entry begin "$job"
refuse entry begin "$job"
refuse entry publish "$job"
compile "$job/output"
before=$(sha256sum "$job/output/target/product/uke/recovery/root/system/bin/recovery" | cut -d' ' -f1)
printf '#define VALUE 31\n' > "$android/source/value.hpp"
refuse entry seal "$job"
[[ ! -e $job/receipt ]]
printf '#define VALUE 17\n' > "$android/source/value.hpp"
touch -r "$scratch/golden-header" "$android/source/value.hpp"
refuse entry seal "$job"
job="$scratch/job-accepted"
entry begin "$job"
compile "$job/output"
before=$(sha256sum "$job/output/target/product/uke/recovery/root/system/bin/recovery" | cut -d' ' -f1)
entry seal "$job"
refuse entry seal "$job"
refuse entry production-class "$job"
refuse entry verify "$job"
entry publish "$job"
entry verify "$job"
printf 'Changed packaging runtime\n' > "$android/out-public/host/linux-x86/lib64/fixture-host-runtime.so"
refuse entry verify "$job"
cp /usr/bin/true "$android/out-public/host/linux-x86/lib64/fixture-host-runtime.so"
entry verify "$job"
printf 'Changed sibling filesystem configuration\n' > "$android/out-public/target/product/uke/vendor/etc/fs_config_files"
refuse entry verify "$job"
printf 'Fixture filesystem configuration\n' > "$android/out-public/target/product/uke/vendor/etc/fs_config_files"
entry verify "$job"
printf '#define VALUE 31\n' > "$android/source/value.hpp"
refuse entry verify "$job"
printf '#define VALUE 17\n' > "$android/source/value.hpp"
touch -r "$scratch/golden-header" "$android/source/value.hpp"
touch "$android/source/value.hpp"
refuse entry verify "$job"
touch -r "$scratch/golden-header" "$android/source/value.hpp"
printf 'Unrebuilt ignored input\n' > "$android/source/ignored-header.hpp"
git -C "$android/source" check-ignore -q ignored-header.hpp
refuse entry verify "$job"
rm "$android/source/ignored-header.hpp"
chmod 0644 "$android/source/value.hpp"
refuse entry verify "$job"
# The original fixture header was created under umask 077.
chmod 0600 "$android/source/value.hpp"
entry verify "$job"
git -C "$android/source" -c user.name=MCC45TR -c user.email=75160848+MCC45TR@users.noreply.github.com commit -qm 'Exercise a source revision mismatch' --allow-empty
refuse entry verify "$job"
git -C "$android/source" checkout -q --detach "$pin"
touch -r "$scratch/golden-header" "$android/source/value.hpp"
entry verify "$job"
mv "$android/source/compiler-input" "$scratch/compiler-link"
printf 'Replacement toolchain bytes\n' > "$android/source/compiler-input"
refuse entry verify "$job"
rm "$android/source/compiler-input"
mv "$scratch/compiler-link" "$android/source/compiler-input"
cp "$android/out-public/target/product/uke/recovery/root/system/bin/recovery" "$scratch/sealed-elf"
printf 'stale-ELF-substitution\n' >> "$android/out-public/target/product/uke/recovery/root/system/bin/recovery"
refuse entry verify "$job"
cp "$scratch/sealed-elf" "$android/out-public/target/product/uke/recovery/root/system/bin/recovery"
ln -s source "$android/unindexed-directory"
refuse entry verify "$job"
rm "$android/unindexed-directory"
ln -s /usr "$android/host-directory-alias"
refuse entry verify "$job"
rg -q 'escapes into a host directory' "$scratch/refused.log"
rm "$android/host-directory-alias"
cp "$job/SERVICE-COMPLETION.json" "$scratch/service-record"
chmod 0600 "$job/SERVICE-COMPLETION.json"
jq '.new_oom_events=1' "$scratch/service-record" > "$job/SERVICE-COMPLETION.json"
chmod 0444 "$job/SERVICE-COMPLETION.json"
refuse entry verify "$job"
chmod 0600 "$job/SERVICE-COMPLETION.json"
cp "$scratch/service-record" "$job/SERVICE-COMPLETION.json"
chmod 0444 "$job/SERVICE-COMPLETION.json"
chmod 0600 "$job/SERVICE-COMPLETION.json"
jq '.controller_confirmed=false' "$scratch/service-record" > "$job/SERVICE-COMPLETION.json"
chmod 0444 "$job/SERVICE-COMPLETION.json"
refuse entry verify "$job"
chmod 0600 "$job/SERVICE-COMPLETION.json"
jq '.systemd_run_status=137' "$scratch/service-record" > "$job/SERVICE-COMPLETION.json"
chmod 0444 "$job/SERVICE-COMPLETION.json"
refuse entry verify "$job"
chmod 0600 "$job/SERVICE-COMPLETION.json"
cp "$scratch/service-record" "$job/SERVICE-COMPLETION.json"
chmod 0444 "$job/SERVICE-COMPLETION.json"
entry verify "$job"
jq '.build.compile_jobs=3' "$job/receipt/BUILD-COMPLETION.json" > "$scratch/tampered-valid.json"
chmod 0644 "$job/receipt/BUILD-COMPLETION.json"
cp "$scratch/tampered-valid.json" "$job/receipt/BUILD-COMPLETION.json"
chmod 0444 "$job/receipt/BUILD-COMPLETION.json"
refuse entry integrity "$job"
rg -q 'checksum did NOT match' "$scratch/refused.log"
# This tampered job is never resealed. The next fresh job is the only new pass.
printf 'Fresh-output compiler fixture and changed header/time, ignored input, file mode, revision, toolchain, ELF, links, missing service, OOM/class and corrupted-receipt refusals passed.\n'

next="$scratch/job-next"
printf '#define VALUE 31\n' > "$android/source/value.hpp"
entry begin "$next"
compile "$next/output"
after=$(sha256sum "$next/output/target/product/uke/recovery/root/system/bin/recovery" | cut -d' ' -f1)
[[ $before != "$after" ]]
entry seal "$next"
entry publish "$next"
[[ $(sha256sum "$next/previous-output/target/product/uke/recovery/root/system/bin/recovery" | cut -d' ' -f1) == "$before" ]]
entry verify "$next"
printf 'Actual changed-header rebuild produced a different ARM64 ELF; atomic promotion preserved the complete old output.\n'

# Interrupt an actual child compiler invocation and prove no receipt/promotion.
interrupted="$scratch/job-interrupted"
entry begin "$interrupted"
bash -c 'set -euo pipefail; "$1" --target=aarch64-linux-android10000 -nostdlib -static -fuse-ld=lld -Wl,-e,_start "$2" -o "$3"; touch "$4"; kill -KILL "$BASHPID"' \
    bash "$compiler" "$android/source/fixture.cpp" "$interrupted/output/partial-elf" "$interrupted/compiler-finished" \
    > "$scratch/interrupted.log" 2>&1 & interrupted_pid=$!
status=0
wait "$interrupted_pid" || status=$?
[[ $status == 137 && -s $interrupted/output/partial-elf && -f $interrupted/compiler-finished && ! -e $interrupted/receipt ]]
refuse entry publish "$interrupted"
refuse entry begin "$interrupted"
entry verify "$next"

failed="$scratch/job-compiler-failed"
entry begin "$failed"
printf 'deliberate compiler syntax error\n' > "$scratch/broken.cpp"
refuse "$compiler" --target=aarch64-linux-android10000 -nostdlib -static -fuse-ld=lld "$scratch/broken.cpp" -o "$failed/output/rejected-elf"
[[ ! -e $failed/receipt && ! -e $failed/output/rejected-elf ]]
refuse entry seal "$failed"
refuse entry publish "$failed"
entry verify "$next"

mkdir -m 0700 "$scratch/atomic-a" "$scratch/atomic-b"
printf 'a\n' > "$scratch/atomic-a/marker"
printf 'b\n' > "$scratch/atomic-b/marker"
bash -c 'set -euo pipefail; while [[ ! -e $1/stop ]]; do [[ -s $1/atomic-b/marker ]] || { touch "$1/missing-output"; exit 1; }; done' bash "$scratch" & observer=$!
for ((i=0;i<40;i++)); do "$publisher" --exchange "$scratch/atomic-a" "$scratch/atomic-b"; done
touch "$scratch/stop"
wait "$observer"
[[ ! -e $scratch/missing-output ]]
refuse "$publisher" --new "$scratch/atomic-a" "$scratch/atomic-b"
ln -s "$scratch/atomic-b" "$scratch/indirect-output"
refuse "$publisher" --exchange "$scratch/atomic-a" "$scratch/indirect-output"
ram=$(mktemp -d /dev/shm/uke-build-exchange-XXXXXXXX)
trap 'rm -f -- "$ram/marker"; rmdir "$ram"' EXIT
printf 'ram\n' > "$ram/marker"
refuse "$publisher" --exchange "$scratch/atomic-a" "$ram"
[[ $(cat "$ram/marker") == ram && -s $scratch/atomic-a/marker ]]
printf 'Actual SIGKILL left an unsealed job and retained old output; repeated directory exchange had no missing-output window. Cross-filesystem, symlink and overwrite publication refused.\n'
