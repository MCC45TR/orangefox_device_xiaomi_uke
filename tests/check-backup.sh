#!/usr/bin/env bash
# Local synthetic transport fixtures, including a mocked remote POSIX shell.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
host_cli="$component/build/ure-host/uke-recoveryctl"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
source_root="$work/root quote ' dollar "'$(touch NEVER)'
mkdir -p "$source_root" "$work/mock-bin"
dd if=/dev/zero of="$source_root/large-file" bs=65536 count=35 status=none
printf 'last chunk\n' >> "$source_root/large-file"
"$binary" backup plan large-file --root "$source_root" --profile fixture --chunk-size 1048576 --output "$work/plan" > "$work/plan-result"
jq -e '.data.source_identity.bytes>1048576 and (.data.chunks|length)==3 and .data.firmware_identity_validated==false' "$work/plan-result" >/dev/null
"$binary" backup export "$work/plan" --root "$source_root" --chunk 0 > "$work/chunk"
expected=$(jq -r '.chunks[0].sha256' "$work/plan")
[[ $(sha256sum "$work/chunk" | cut -d' ' -f1) == "$expected" ]]
if "$binary" backup export "$work/plan" --root "$source_root" --chunk 9 > "$work/binary-error" 2> "$work/json-error"; then exit 1; fi
[[ ! -s $work/binary-error ]]
jq -e '.error.code=="invalid-chunk"' "$work/json-error" >/dev/null
"$binary" backup capture "$work/plan" --root "$source_root" --journal "$work/native-store" | jq -e '.data.state=="COMPLETE" and .data.verified==true' >/dev/null
unlink "$work/native-store/chunk-00001.bin"
unlink "$work/native-store/chunk-00002.bin"
"$binary" backup verify "$work/native-store" | jq -e '.data.state=="PARTIAL" and .data.next_chunk==1' >/dev/null
"$binary" backup resume "$work/native-store" --root "$source_root" | jq -e '.data.state=="COMPLETE"' >/dev/null
bash "$component/scripts/receive-backup.sh" --manifest "$work/plan" --output "$work/local-receiver" \
    --source-root "$source_root" --source-plan "$work/plan" --source-cli "$binary" --host-cli "$host_cli" --local > "$work/local-result"
jq -e '.data.verified==true and .data.state=="COMPLETE"' "$work/local-result" >/dev/null
unlink "$work/local-receiver/chunk-00002.bin"
bash "$component/scripts/receive-backup.sh" --manifest "$work/plan" --output "$work/local-receiver" \
    --source-root "$source_root" --source-plan "$work/plan" --source-cli "$binary" --host-cli "$host_cli" --local > "$work/resume-result"
jq -e '.data.verified==true' "$work/resume-result" >/dev/null
cat > "$work/mock-bin/ssh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
[[ $1 == -T && $2 == -o && $3 == BatchMode=yes && $4 == -o && $5 == StrictHostKeyChecking=yes && $6 == -- && $7 == mock-host ]]
[[ $# == 8 ]]
exec /bin/sh -c "$8"
SH
chmod 755 "$work/mock-bin/ssh"
# No real SSH or ADB connection is made. The mock exercises the remote argument
# quoting with spaces, apostrophes and literal command-substitution syntax.
PATH="$work/mock-bin:$PATH" bash "$component/scripts/receive-backup.sh" --manifest "$work/plan" --output "$work/mock-remote-receiver" \
    --source-root "$source_root" --source-plan "$work/plan" --source-cli "$binary" --host-cli "$host_cli" --ssh mock-host > "$work/mock-result"
jq -e '.data.verified==true' "$work/mock-result" >/dev/null
[[ ! -e NEVER ]]
printf 'Backup CLI: large file, binary framing, chunk hashes, resume and local/mocked SSH receiver passed; no remote device used.\n'
