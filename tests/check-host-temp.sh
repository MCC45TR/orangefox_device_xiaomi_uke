#!/usr/bin/env bash
# Host-only scratch controls and the actual nested Android mount recipe.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d "$component/build/host-temp-fixture-XXXXXXXX")
policy="$component/scripts/host-temp-policy.sh"
refuse() {
    if "$@" > "$scratch/refused.log" 2>&1; then
        echo 'Expected disk scratch refusal.' >&2; exit 1
    fi
}
bash "$policy" --capacity arm64 ext4 4096 2097152 200000 131072 | jq -e '.available_bytes==8589934592 and .inode_accounting=="fixed"' >/dev/null
refuse bash "$policy" --capacity arm64 ext4 4096 2097151 200000 131072
refuse bash "$policy" --capacity arm64 ext4 4096 2097152 200000 131071
refuse bash "$policy" --capacity native ext4 4096 2097152 0 0
refuse bash "$policy" --capacity native ext4 4096 2097152 100000 131072
refuse bash "$policy" --capacity native btrfs 4096 524287 0 0
bash "$policy" --capacity native btrfs 4096 524288 0 0 | jq -e '.available_bytes==2147483648 and .inode_accounting=="dynamic-btrfs"' >/dev/null
for fs in tmpfs ramfs overlay nfs fuseblk; do refuse bash "$policy" --capacity arm64 "$fs" 4096 2097152 200000 131072; done
refuse bash "$policy" --capacity native btrfs invalid 524288 0 0
refuse bash "$policy" --capacity native btrfs 4096 100000000000000 0 0
bash "$policy" arm64 "$scratch" > "$scratch/expected.json"
bash "$policy" arm64 "$scratch" "$scratch/expected.json" > "$scratch/rechecked.json"
jq '.identity.directory_inode += 1' "$scratch/expected.json" > "$scratch/wrong-inode.json"
refuse bash "$policy" arm64 "$scratch" "$scratch/wrong-inode.json"
jq '.job_mode="native"' "$scratch/expected.json" > "$scratch/wrong-mode.json"
refuse bash "$policy" arm64 "$scratch" "$scratch/wrong-mode.json"
ln "$scratch/expected.json" "$scratch/proof-alias"
refuse bash "$policy" arm64 "$scratch" "$scratch/expected.json"
rm "$scratch/proof-alias"
chmod 0711 "$scratch"
refuse bash "$policy" arm64 "$scratch"
chmod 0700 "$scratch"
ln -s "$scratch" "$scratch/scratch-alias"
refuse bash "$policy" arm64 "$scratch/scratch-alias"
printf 'Disk policy snapshots, low-byte/inode and unsupported-filesystem refusal, private-directory and receipt identity controls passed.\n'

cat > "$scratch/inner-job.sh" <<'INNER_JOB_EOF'
set -euo pipefail
umask 077
[[ $TMPDIR == /tmp && $TMP == /tmp && $TEMP == /tmp && $OUT_DIR == /mnt/out-public && $FOX_BUILD_BASH == 1 ]]
jq -en --slurpfile initial /tmp/host-temp-policy.json --slurpfile admitted /tmp/host-temp-admitted.json \
    --slurpfile before /tmp/android-temp-before.json --slurpfile inner /tmp/android-temp-inner.json \
    '$initial[0].identity==$admitted[0].identity and $initial[0].identity==$before[0].identity and $initial[0].identity==$inner[0].identity' >/dev/null
membership=$(awk -F: '$1==0 {print $3}' /proc/self/cgroup)
cg=/sys/fs/cgroup$membership
cat "$cg/memory.stat" > /tmp/probe-memory.stat.before
df -B1 --output=avail /tmp > /tmp/probe-disk.before
dd if=/dev/urandom of=/tmp/disk-probe.bin bs=1M count=64 conv=fsync status=none
sync -f /tmp/disk-probe.bin
cat "$cg/memory.stat" > /tmp/probe-memory.stat.after
df -B1 --output=avail /tmp > /tmp/probe-disk.after
[[ $(stat -c %s /tmp/disk-probe.bin) == 67108864 ]]
allocated=$(( $(stat -c %b /tmp/disk-probe.bin) * 512 ))
(( allocated >= 67108864 ))
anon_before=$(awk '$1=="anon" {print $2}' /tmp/probe-memory.stat.before)
anon_after=$(awk '$1=="anon" {print $2}' /tmp/probe-memory.stat.after)
shmem_before=$(awk '$1=="shmem" {print $2}' /tmp/probe-memory.stat.before)
shmem_after=$(awk '$1=="shmem" {print $2}' /tmp/probe-memory.stat.after)
file_before=$(awk '$1=="file" {print $2}' /tmp/probe-memory.stat.before)
file_after=$(awk '$1=="file" {print $2}' /tmp/probe-memory.stat.after)
(( anon_after-anon_before <= 33554432 && shmem_after==shmem_before ))
free_before=$(awk 'NR==2 {print $1}' /tmp/probe-disk.before)
free_after=$(awk 'NR==2 {print $1}' /tmp/probe-disk.after)
jq -cn --argjson allocated "$allocated" --argjson anonymous "$((anon_after-anon_before))" \
    --argjson shmem "$((shmem_after-shmem_before))" --argjson file "$((file_after-file_before))" --argjson disk "$((free_before-free_after))" \
    '{file_bytes:67108864,allocated_bytes:$allocated,anonymous_growth_bytes:$anonymous,shmem_growth_bytes:$shmem,file_cache_growth_bytes:$file,observed_filesystem_available_drop_bytes:$disk}' \
    > /mnt/probe-result.json
cp /tmp/android-temp-inner.json /mnt/inner-temp-receipt.json
cp /tmp/android-temp-mount.txt /mnt/inner-temp-mount.txt
printf 'Actual inner disk scratch: %s\n' "$(cat /mnt/probe-result.json)"
INNER_JOB_EOF
cat > "$scratch/outer-job.sh" <<'OUTER_JOB_EOF'
set -euo pipefail
umask 077
component=$1
fixture=$2
refuse() {
    if "$@" > /tmp/namespace-refused.log 2>&1; then
        echo 'Expected namespace admission refusal.' >&2; exit 1
    fi
}
refuse bwrap --ro-bind / / --dev-bind /dev /dev --proc /proc --tmpfs /tmp --chmod 0700 /tmp \
    --ro-bind /tmp/host-temp-policy.json /tmp/expected.json --bind "$fixture" /mnt \
    bash -c 'set -euo pipefail; bash "$1" arm64 /tmp /tmp/expected.json; touch /mnt/unexpected-ram-admission' bash "$component/scripts/host-temp-policy.sh"
rg -q 'reviewed local disk filesystem' /tmp/namespace-refused.log
[[ ! -e $fixture/unexpected-ram-admission ]]
replacement=$(mktemp -d /tmp/replaced-disk-XXXXXXXX)
refuse bwrap --ro-bind / / --dev-bind /dev /dev --proc /proc --bind "$replacement" /tmp \
    --ro-bind /tmp/host-temp-policy.json /tmp/expected.json --bind "$fixture" /mnt \
    bash -c 'set -euo pipefail; bash "$1" arm64 /tmp /tmp/expected.json; touch /mnt/unexpected-disk-admission' bash "$component/scripts/host-temp-policy.sh"
rg -q 'identity changed' /tmp/namespace-refused.log
[[ ! -e $fixture/unexpected-disk-admission ]]
refuse bwrap --ro-bind / / --dev-bind /dev /dev --proc /proc --ro-bind /tmp /tmp --bind "$fixture" /mnt \
    bash -c 'set -euo pipefail; bash "$1" arm64 /tmp /tmp/host-temp-policy.json; touch /mnt/unexpected-readonly-admission' bash "$component/scripts/host-temp-policy.sh"
rg -q 'Read-only file system' /tmp/namespace-refused.log
[[ ! -e $fixture/unexpected-readonly-admission ]]
bash "$component/scripts/with-android-build-environment.sh" "$fixture" bash /mnt/inner-job.sh
OUTER_JOB_EOF
bash "$component/scripts/with-host-budget.sh" arm64 bash "$scratch/outer-job.sh" "$component" "$scratch" > "$scratch/service.log" 2>&1
cat "$scratch/service.log"
jq -e '.file_bytes==67108864 and .allocated_bytes>=.file_bytes and .anonymous_growth_bytes<=33554432 and .shmem_growth_bytes==0' "$scratch/probe-result.json" >/dev/null
printf 'Actual nested Android mount, bounded disk write and anonymous/shared-memory accounting passed; tmpfs, substituted disk and readonly scratch refused before the command.\n'
