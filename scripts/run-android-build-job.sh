#!/usr/bin/env bash
# Validate scratch from inside the production mount recipe before any compiler.
set -euo pipefail
umask 077
[[ ${UKE_HOST_BUDGET_ACTIVE:-0} == 1 && $# -gt 0 && $TMPDIR == /tmp && $TMP == /tmp && $TEMP == /tmp ]]
bash /mnt/uke-host-temp-policy.sh arm64 /tmp /tmp/host-temp-policy.json > /tmp/android-temp-inner.json
{
    stat -fc 'resolved-filesystem=%T magic=%t block-size=%S available-blocks=%a' /tmp
    stat -c 'resolved-directory-device=%d inode=%i mode=%a' /tmp
    printf 'Mountinfo target entries (may include shadowed mounts):\n'
    awk '$5=="/tmp" {print $1,$2,$3,$5,$6}' /proc/self/mountinfo
} > /tmp/android-temp-mount.txt
exec "$@"
