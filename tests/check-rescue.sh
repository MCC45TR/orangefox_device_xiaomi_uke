#!/usr/bin/env bash
# Native execution in disposable roots and user namespaces; no installed OS is changed.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-$component/build/ure-host/uke-recoveryctl}
fixture=$(mktemp -d /tmp/ure-rescue-XXXXXX)
trap 'chmod -R u+w "$fixture"; find "$fixture" -depth -delete' EXIT
mkdir -p "$fixture/root"/{etc,usr/bin,proc,sys,dev,run,tmp,boot/efi,root,var} "$fixture/esp"
printf 'ID=arch\nNAME=Arch fixture\n' > "$fixture/root/etc/os-release"
printf 'UUID=fixture-root / ext4 defaults 0 1\nUUID=fixture-esp /boot/efi vfat defaults 0 2\n' > "$fixture/root/etc/fstab"
printf 'selected ESP\n' > "$fixture/esp/marker"
for program in bash sleep true; do
    source=$(type -P "$program")
    cp -- "$source" "$fixture/root/usr/bin/$program"
    while IFS= read -r library; do install -Dm755 -- "$library" "$fixture/root$library"; done < <(ldd "$source" | awk '/=> \/|^\s*\// {for(i=1;i<=NF;i++)if($i ~ /^\//)print $i}' | sort -u)
done
cp -- "$fixture/root/usr/bin/true" "$fixture/root/usr/bin/rpmdb"
cat > "$fixture/inside.sh" <<'SH'
set -euo pipefail
binary=$1
fixture=$2
call() {
    if ! "$binary" "$@" > "$fixture/result.json" || ! jq -e '.result=="ok"' "$fixture/result.json" >/dev/null; then cat "$fixture/result.json" >&2; return 1; fi
}
plan() {
    call linux rescue-plan "$fixture/request.json" --root "$fixture/root" --esp "$fixture/esp" --output "$fixture/plan-$1.json"
    hash=$(jq -r .plan_sha256 "$fixture/plan-$1.json")
}
execute() { call linux rescue-execute "$fixture/plan-$1.json" --root "$fixture/root" --esp "$fixture/esp" --journal "$fixture/job-$1" --confirm "$hash"; }
mounts=$(cat /proc/self/mountinfo)
jq -n --arg input 'test -d /proc/1 && test -d /sys/devices && test -c /dev/null && test -f /boot/efi/marker || exit 7
if printf bad > /etc/forbidden; then exit 8; fi
if printf bad > /boot/efi/marker; then exit 9; fi
printf runtime > /run/session-marker
printf URE_ARCH_SESSION
/usr/bin/sleep 30 &
exit 0
' '{schema:1,action:"shell",write:false,network:false,timeout_seconds:10,shell_input:$input}' > "$fixture/request.json"
plan readonly
jq -e '.distribution_family=="arch" and .connections[0].method=="selected-esp" and .raw_block_devices_exposed==false' "$fixture/plan-readonly.json" >/dev/null
execute readonly
jq -e '.data.state=="COMPLETE" and .data.namespace_worker_reaped and .data.session_mounts_released and .data.descendants_bound_to_pid_namespace and (.data.cleanup_pending|not)' "$fixture/result.json" >/dev/null
rg -q URE_ARCH_SESSION "$fixture/job-readonly/console.log"
[[ ! -e $fixture/root/etc/forbidden && ! -e $fixture/root/run/session-marker && $(cat "$fixture/esp/marker") == 'selected ESP' ]]
[[ $(cat /proc/self/mountinfo) == "$mounts" && ! -e $fixture/job-readonly/mount-root ]]
jq -n '{schema:1,action:"shell",write:true,network:false,timeout_seconds:10,shell_input:"printf restored > /etc/repaired\n"}' > "$fixture/request.json"
plan writable
execute writable
jq -e '.data.state=="COMPLETE" and .data.session_mounts_released' "$fixture/result.json" >/dev/null
[[ $(cat "$fixture/root/etc/repaired") == restored ]]
jq -n '{schema:1,action:"shell",write:false,network:false,timeout_seconds:2,shell_input:"/usr/bin/sleep 30 &\n/usr/bin/sleep 30\n"}' > "$fixture/request.json"
plan timeout
if "$binary" linux rescue-execute "$fixture/plan-timeout.json" --root "$fixture/root" --esp "$fixture/esp" --journal "$fixture/job-timeout" --confirm "$hash" > "$fixture/result.json"; then exit 1; fi
jq -e '.data.state=="TIMED_OUT" and (.data.successful|not) and .data.session_mounts_released and .data.descendants_bound_to_pid_namespace and (.data.cleanup_pending|not)' "$fixture/result.json" >/dev/null
[[ $(cat /proc/self/mountinfo) == "$mounts" && ! -e $fixture/job-timeout/mount-root ]]
plan stale
printf 'ID=fedora\nNAME=Fedora fixture\n' > "$fixture/root/etc/os-release"
if "$binary" linux rescue-execute "$fixture/plan-stale.json" --root "$fixture/root" --esp "$fixture/esp" --journal "$fixture/job-stale" --confirm "$hash" > "$fixture/rejected.json"; then exit 1; fi
jq -e '.error.code=="stale-rescue-plan"' "$fixture/rejected.json" >/dev/null
[[ ! -e $fixture/job-stale ]]
jq -n '{schema:1,action:"package-repair",write:true,network:false,timeout_seconds:10}' > "$fixture/request.json"
plan fedora
jq -e '.distribution_family=="fedora" and .arguments==["--rebuilddb"]' "$fixture/plan-fedora.json" >/dev/null
execute fedora
jq -e '.data.state=="COMPLETE" and .data.session_mounts_released' "$fixture/result.json" >/dev/null
printf '%s\n' 'Arch/Fedora dispatch, automatic selected ESP, isolated native chroot, read-only policy, writable choice, stale-plan rejection, descendant cleanup and timeout fixtures passed.'
SH
bwrap --unshare-user --unshare-pid --uid 0 --gid 0 --new-session --die-with-parent \
    --cap-add CAP_SYS_ADMIN --cap-add CAP_CHOWN --cap-add CAP_DAC_OVERRIDE --cap-add CAP_FOWNER \
    --cap-add CAP_SETUID --cap-add CAP_SETGID --cap-add CAP_SETFCAP --cap-add CAP_SYS_CHROOT \
    --ro-bind / / --bind "$fixture" "$fixture" --dev /dev --proc /proc \
    -- bash "$fixture/inside.sh" "$binary" "$fixture"
