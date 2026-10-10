#!/usr/bin/env bash
# Orchestration-only mocks; native binary transport has its own storage fixtures.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "$component/build/host-partition-backup-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/bin"
cat > "$work/bin/adb" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ $1 == -s && $2 == 'serial with spaces' ]]
shift 2
if [[ $# == 1 && $1 == features ]]; then echo shell_v2; exit 0; fi
[[ $# == 5 && $1 == shell && $2 == -T && $3 == -e && $4 == none ]]
exec /bin/sh -c "$5"
MOCK
cat > "$work/source tool ' literal \$(true)" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
if [[ $1 == storage && $2 == inventory ]]; then
    jq -n '{result:"ok",data:{objects:[
      {partition:true,label:"boot_a",stable_id:"sysfs:fixture/boot_a",bytes:65536},
      {partition:true,label:"recovery_b",stable_id:"sysfs:fixture/recovery_b",bytes:104857600},
      {partition:true,label:"super",stable_id:"sysfs:fixture/super",bytes:1048576},
      {partition:true,label:"vendor_a",stable_id:"sysfs:fixture/virtual",bytes:65536},
      {partition:false,label:"sda",stable_id:"sysfs:fixture/sda",bytes:1099511627776}]}}'
    exit 0
fi
printf 'Read-only planning intentionally refused\n' >&2
exit 9
MOCK
chmod 700 "$work/bin/adb" "$work/source tool ' literal \$(true)"
export PATH="$work/bin:$PATH"
source_cli="$work/source tool ' literal \$(true)"
run() { bash "$component/scripts/backup-important-partitions.sh" --adb-serial 'serial with spaces' --source-cli "$source_cli" "$@"; }
refuse() { if "$@" > "$work/refusal.log" 2>&1; then echo 'Unexpected host-backup success' >&2; exit 1; fi; }
run --list > "$work/list.json"
jq -e '.source_read_only and (.restore_authorized|not) and (.physical_test_record|not) and
  (.partitions|map(.label))==["boot_a","recovery_b","super"] and .total_bytes==105971712' "$work/list.json" >/dev/null
run --list --partition recovery_b > "$work/one.json"
jq -e '(.partitions|length)==1 and .partitions[0].label=="recovery_b"' "$work/one.json" >/dev/null
refuse run --list --partition absent
refuse run --list --partition '../escape'
refuse run --list --list
refuse run --capture --profile fixture --partition boot_a --output "$work/private-output" --host-cli /usr/bin/true
[[ $(stat -c '%a' "$work/private-output") == 700 ]]
jq -e '.state=="PARTIAL" and (.physical_test_record|not) and .partitions[0].label=="boot_a" and (.partitions[0].verified//false)==false' "$work/private-output/manifest.json" >/dev/null
[[ ! -e $work/private-output/plans/boot_a.json ]]
refuse run --capture --profile fixture --partition boot_a --output "$work/private-output" --host-cli /usr/bin/true
bash -n "$component/scripts/backup-important-partitions.sh"
printf '%s\n' 'Host backup orchestration: serial-bound shell_v2, literal argument quoting, physical selection, missing/traversal options, refused capture and retained PARTIAL state passed; no real ADB or block capture.'
