#!/usr/bin/env bash
# Stage immutable shaping sources; never execute reference build scripts.
set -euo pipefail
export LC_ALL=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$component"
lock=manifests/text-layout.lock.json
[[ -f $lock && ! -L $lock ]]
mkdir -p build
work=$(mktemp -d build/text-layout-sources-XXXXXX)
trap 'rm -rf -- "$work"' EXIT
for name in harfbuzz fribidi; do
    pin=$(jq -er --arg name "$name" '.libraries[]|select(.name==$name)|.commit' "$lock")
    reference="referances/upstream/$name"
    [[ -d $reference && ! -L $reference ]]
    [[ $(git -C "$reference" rev-parse HEAD) == "$pin" ]]
    [[ -z $(git -C "$reference" status --porcelain --untracked-files=no) ]]
    version=$(jq -er --arg name "$name" '.libraries[]|select(.name==$name)|.version' "$lock")
    [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
    expected="$work/$name"
    mkdir "$expected"
    if [[ $name == harfbuzz ]]; then
        git -C "$reference" archive "$pin" | tar --same-permissions -xf - -C "$expected"
    else
        archive="referances/upstream/fribidi-release/fribidi-$version.tar.xz"
        digest=$(jq -er '.libraries[]|select(.name=="fribidi")|.release_sha256' "$lock")
        [[ -f $archive && ! -L $archive && $(sha256sum "$archive" | cut -d' ' -f1) == "$digest" ]]
        # The original release carries its Unicode tables. All members must be
        # regular files or directories beneath the exact versioned root.
        tar -tf "$archive" > "$work/members"
        ! rg -v "^fribidi-$version/" "$work/members"
        ! rg '(^|/)\.\.(/|$)' "$work/members"
        tar -tvf "$archive" > "$work/types"
        ! rg '^[^d-]' "$work/types"
        tar --same-permissions -xf "$archive" --strip-components=1 -C "$expected"
        # Release C/header files must match the pinned tag; only generated
        # tables/header and distribution metadata may differ from Git.
        while IFS= read -r path; do
            git -C "$reference" show "$pin:$path" > "$work/git-file"
            cmp "$work/git-file" "$expected/$path"
        done < <(git -C "$reference" ls-tree -r --name-only "$pin" lib | rg '\.(c|h|h\.in)$')
        patch --directory="$expected" -p1 --batch --fuzz=0 --no-backup-if-mismatch \
            < patches/0031-fribidi-allocation-failure.patch
        patch --directory="$expected" -p1 --batch --fuzz=0 --no-backup-if-mismatch \
            < patches/0033-fribidi-explicit-direction-types.patch
    fi
    target="src/upstream/text-layout/$name-$version"
    if [[ -e $target || -L $target ]]; then
        [[ -d $target && ! -L $target && -f $target/.uke-linux-owned && ! -L $target/.uke-linux-owned && $(cat "$target/.uke-linux-owned") == "$pin" ]]
        # Exclude only the exact root-level project adapter names, never a
        # similarly named upstream file in a nested directory.
        find "$expected" -mindepth 1 -printf '%P\n' | sort > "$work/expected-members"
        find "$target" -mindepth 1 -printf '%P\n' | sort > "$work/all-members"
        if [[ $name == harfbuzz ]]; then
            sed '/^\.uke-linux-owned$/d; /^Android\.bp$/d; /^Unicode-LICENSE\.txt$/d' "$work/all-members" > "$work/actual-members"
        else
            sed '/^\.uke-linux-owned$/d; /^Android\.bp$/d; /^config\.h$/d; /^fribidi-config\.h$/d; /^fribidi-custom\.h$/d' "$work/all-members" > "$work/actual-members"
        fi
        cmp "$work/expected-members" "$work/actual-members"
        : > "$work/reviewed-updates"
        while IFS= read -r path; do
            if [[ -L $expected/$path ]]; then
                [[ -L $target/$path && $(readlink "$expected/$path") == "$(readlink "$target/$path")" ]]
                resolved=$(realpath -e "$expected/$path")
                [[ $resolved == "$(realpath -e "$expected")/"* && -f $resolved ]]
            elif [[ -d $expected/$path ]]; then [[ -d $target/$path && ! -L $target/$path ]];
            else
                [[ -f $target/$path && ! -L $target/$path ]]
                [[ $((8#$(stat -c %a "$target/$path") & 0111)) == $((8#$(stat -c %a "$expected/$path") & 0111)) ]]
                if ! cmp -s "$expected/$path" "$target/$path"; then
                    [[ $name == fribidi && $path == lib/fribidi-bidi.c ]]
                    digest=$(sha256sum "$target/$path" | cut -d' ' -f1)
                    jq -e --arg name "$name" --arg path "$path" --arg digest "$digest" \
                        '.libraries[]|select(.name==$name)|.source_predecessors[$path]|index($digest)!=null' \
                        "$lock" >/dev/null
                    printf '%s\n' "$path" >> "$work/reviewed-updates"
                fi
            fi
        done < "$work/expected-members"
        while IFS= read -r path; do cp -- "$expected/$path" "$target/$path"; done < "$work/reviewed-updates"
    else
        mkdir -p "${target%/*}"
        mv "$expected" "$target"
        printf '%s\n' "$pin" > "$target/.uke-linux-owned"
    fi
    for file in Android.bp; do
        [[ ! -L $target/$file ]]
        cmp -s "configs/text-layout/$name-$file" "$target/$file" || cp -- "configs/text-layout/$name-$file" "$target/$file"
    done
    if [[ $name == fribidi ]]; then
        for file in config.h fribidi-config.h fribidi-custom.h; do
            [[ ! -L $target/$file ]]
            cmp -s "configs/text-layout/$file" "$target/$file" || cp -- "configs/text-layout/$file" "$target/$file"
        done
    else
        notice=src/device/xiaomi/uke/text-notices/Unicode-LICENSE.txt
        [[ -f $notice && ! -L $notice && ! -L $target/Unicode-LICENSE.txt &&
           $(sha256sum "$notice" | cut -d' ' -f1) == e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96 ]]
        cmp -s "$notice" "$target/Unicode-LICENSE.txt" || cp -- "$notice" "$target/Unicode-LICENSE.txt"
    fi
done
echo 'Pinned text-layout sources and generated release tables verified and staged.'
