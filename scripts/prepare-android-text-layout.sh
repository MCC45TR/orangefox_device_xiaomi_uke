#!/usr/bin/env bash
# Stage reviewed native text sources without depending on a firmware payload.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tree="$component/src/upstream/orangefox-android16"
bash "$component/scripts/prepare-text-layout-sources.sh"
[[ -d $tree && ! -L $tree && -d $tree/external && ! -L $tree/external ]]
work=$(mktemp -d "$component/build/android-text-stage-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
for name in harfbuzz fribidi; do
    version=$(jq -er --arg name "$name" '.libraries[]|select(.name==$name)|.version' "$component/manifests/text-layout.lock.json")
    source="$component/src/upstream/text-layout/$name-$version"
    target="$tree/external/ure-$name"
    if [[ -e $target || -L $target ]]; then
        [[ -d $target && ! -L $target && -f $target/.uke-linux-owned && ! -L $target/.uke-linux-owned ]]
        if ! diff -qr -- "$source" "$target" > "$work/differences"; then
            # A reviewed project-adapter predecessor can advance, while every
            # upstream file and member must remain exact. Unknown edits refuse
            # before any target file is replaced.
            find "$source" -mindepth 1 -printf '%P\n' | sort > "$work/expected-members"
            find "$target" -mindepth 1 -printf '%P\n' | sort > "$work/actual-members"
            cmp "$work/expected-members" "$work/actual-members"
            : > "$work/reviewed-updates"
            while IFS= read -r path; do
                if [[ -L $source/$path ]]; then
                    [[ -L $target/$path && $(readlink "$source/$path") == "$(readlink "$target/$path")" ]]
                elif [[ -d $source/$path ]]; then [[ -d $target/$path && ! -L $target/$path ]];
                else
                    [[ -f $target/$path && ! -L $target/$path ]]
                    [[ $((8#$(stat -c %a "$target/$path") & 0111)) == $((8#$(stat -c %a "$source/$path") & 0111)) ]]
                    if ! cmp -s "$source/$path" "$target/$path"; then
                        digest=$(sha256sum "$target/$path" | cut -d' ' -f1)
                        if [[ $path == Android.bp ]]; then
                            jq -e --arg name "$name" --arg digest "$digest" \
                                '.libraries[]|select(.name==$name)|.adapter_predecessors["Android.bp"]|index($digest)!=null' \
                                "$component/manifests/text-layout.lock.json" >/dev/null
                        else
                            [[ $name == fribidi && $path == lib/fribidi-bidi.c ]]
                            jq -e --arg name "$name" --arg path "$path" --arg digest "$digest" \
                                '.libraries[]|select(.name==$name)|.source_predecessors[$path]|index($digest)!=null' \
                                "$component/manifests/text-layout.lock.json" >/dev/null
                        fi
                        printf '%s\n' "$path" >> "$work/reviewed-updates"
                    fi
                fi
            done < "$work/expected-members"
            while IFS= read -r path; do cp -- "$source/$path" "$target/$path"; done < "$work/reviewed-updates"
            diff -qr -- "$source" "$target"
        fi
    else
        cp -a -- "$source" "$target"
    fi
done
echo 'Android text dependencies match the reviewed active source snapshots.'
