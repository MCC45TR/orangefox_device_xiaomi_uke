#!/usr/bin/env bash
# Host-only setup for the pinned OrangeFox Android 16 source checkout.
set -euo pipefail

component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
repo_source="$component/referances/tools/android-repo"
manifest_source="$component/referances/recovery/orangefox-manifest"
theme_source="$component/referances/recovery/orangefox-classic-theme"
local_manifest="$component/configs/orangefox/uke.xml"
repo_commit=d27d6829a84f488b7253ea693dcc429076c33914
manifest_commit=6bbb43ed568c9ee2127fb64333808388e583e459
theme_commit=350432b6a0f65a119ac2c4511aff5c57344f7c23

[[ -d $repo_source/.git && $(git -C "$repo_source" rev-parse HEAD) == "$repo_commit" ]] || {
  echo 'Pinned Android repo client is missing or changed' >&2; exit 1;
}
[[ -d $manifest_source/.git && $(git -C "$manifest_source" rev-parse HEAD) == "$manifest_commit" ]] || {
  echo 'Pinned OrangeFox manifest is missing or changed' >&2; exit 1;
}
[[ -d $theme_source/.git && $(git -C "$theme_source" rev-parse HEAD) == "$theme_commit" ]] || {
  echo 'Pinned OrangeFox theme is missing or changed' >&2; exit 1;
}
[[ -f $local_manifest ]] || { echo 'Uke local manifest is missing' >&2; exit 1; }
xmllint --noout "$local_manifest"

mkdir -p "$tree"
if [[ ! -d $tree/.repo ]]; then
  python3 "$repo_source/repo" init \
    --no-clone-bundle \
    --repo-url "file://$repo_source" \
    --repo-rev "$repo_commit" \
    --no-repo-verify \
    -u "file://$manifest_source" \
    -b "$manifest_commit" \
    -m default.xml
fi

mkdir -p "$tree/.repo/local_manifests"
cp -- "$local_manifest" "$tree/.repo/local_manifests/uke.xml"

printf 'Prepared %s with the pinned Uke local manifest.\n' "${tree#"$component/"}"
printf 'Run the documented repo sync command before recording a resolved manifest.\n'
