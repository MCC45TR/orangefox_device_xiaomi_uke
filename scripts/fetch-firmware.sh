#!/usr/bin/env bash
# Host-only, resumable acquisition of a pinned Xiaomi fastboot reference.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
manifest="$component/manifests/firmware.lock.json"
profile=${1:?Usage: fetch-firmware.sh PROFILE_ID [bn.d.miui.com]}
[[ $profile =~ ^[a-z0-9.-]+$ ]] || { echo 'Invalid profile ID' >&2; exit 2; }
entry=$(jq -ce --arg id "$profile" '.profiles[]|select(.id==$id)' "$manifest") || { echo 'Unknown firmware profile' >&2; exit 2; }
url=$(jq -r .url <<<"$entry")
case $url in
  https://cdnorg.d.miui.com/*|https://bigota.d.miui.com/*) ;;
  *) echo 'Unexpected firmware host' >&2; exit 2 ;;
esac
mirror=${2:-}
case $mirror in
  '') ;;
  bn.d.miui.com)
    path=${url#https://cdnorg.d.miui.com/}
    path=${path#https://bigota.d.miui.com/}
    url="https://$mirror/$path"
    ;;
  *) echo 'Unsupported Xiaomi mirror' >&2; exit 2 ;;
esac
region=$(jq -r .region <<<"$entry" | tr '[:upper:]' '[:lower:]')
filename=${url##*/}
[[ $filename =~ ^uke_[a-zA-Z0-9._-]+\.(tgz|zip)$ ]] || { echo 'Unexpected firmware filename' >&2; exit 2; }
target="$component/referances/firmware/$region/raw/$filename"
mkdir -p "$(dirname -- "$target")" "$component/reports/private"
exec 9>"$component/reports/private/firmware-$profile.lock"
flock -n 9 || { echo 'This firmware profile is already downloading' >&2; exit 1; }
free=$(df -B1 --output=avail "$component" | tail -n 1 | tr -d ' ')
expected=$(jq -r .expected_bytes_from_head <<<"$entry")
(( free-expected >= 85899345920 )) || { echo '80 GiB free-space reserve would be breached' >&2; exit 1; }
actual_head=$(curl -fLsSI --max-time 45 "$url" | tr -d '\r' | awk 'tolower($1)=="content-length:" {print $2}' | tail -n 1)
[[ $actual_head == "$expected" ]] || { echo 'Remote size differs from the pinned profile' >&2; exit 1; }
if [[ -f "$target" && $(stat -c %s "$target") == "$expected" ]]; then
  echo 'Existing file has the expected size; rechecking SHA-256'
else
  curl --fail --location --retry 6 --retry-delay 5 --retry-all-errors --continue-at - --output "$target.part" "$url"
  [[ $(stat -c %s "$target.part") == "$expected" ]] || { echo 'Incomplete file; retained resumable .part' >&2; exit 1; }
  mv "$target.part" "$target"
fi
sha=$(sha256sum "$target" | cut -d ' ' -f1)
expected_md5=$(jq -r '.expected_md5 // empty' <<<"$entry")
if [[ -n $expected_md5 ]]; then
  actual_md5=$(md5sum "$target" | cut -d ' ' -f1)
  [[ $actual_md5 == "$expected_md5" ]] || { echo 'Downloaded file failed the pinned MD5 check' >&2; exit 1; }
fi
report="$component/reports/private/firmware-$profile.json"
jq -n --arg id "$profile" --arg file "referances/firmware/$region/raw/$filename" --arg sha "$sha" --arg md5 "${actual_md5:-}" --arg acquired "$(date -u +%Y-%m-%dT%H:%M:%SZ)" --arg download_url "$url" --argjson bytes "$expected" '{id:$id,path:$file,bytes:$bytes,sha256:$sha,acquired_at:$acquired,download_url:$download_url} + (if $md5 == "" then {} else {md5:$md5} end)' > "$report"
printf '%s verified: %s bytes, sha256 %s\n' "$profile" "$expected" "$sha"
