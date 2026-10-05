#!/usr/bin/env bash
# Host inventories bind bytes, paths, modes, links and source pins, not semantics.
set -euo pipefail
localization_digest() { sha256sum -- "$1" | cut -d' ' -f1; }
localization_config() {
    [[ -f $1 && ! -L $1 && $(stat -c '%s' "$1") -le 65536 ]] || return 1
    jq -e '.schema_version==1 and
        all(.required[],.optional[],.source_projects[],.language_directories[];
            type=="string" and test("^[a-zA-Z0-9_./-]+$") and
            (startswith("/")|not) and (split("/")|all(.!=".." and .!="." and .!=""))) and
        (.required|length)>0 and (.required|length)==(.required|unique|length) and
        (.optional|length)==(.optional|unique|length) and
        (.source_projects|length)>0 and (.source_projects|length)==(.source_projects|unique|length) and
        (.language_codes|length)>0 and .language_codes==(.language_codes|sort|unique) and
        all(.language_codes[];type=="string" and test("^[a-zA-Z0-9_-]+$"))' "$1" >/dev/null
}
localization_source() {
    local component=$1 work=$2 config="$1/configs/localization-inputs.json" path locked actual directory file code selector
    local tree="$1/src/upstream/orangefox-android16"
    localization_config "$config" || return 1
    bash "$component/scripts/index-build-tree.sh" "$component" "$work/index" localization
    : > "$work/projects.jsonl"
    while IFS= read -r path; do
        selector="/manifest/project[@path='$path' or (not(@path) and @name='$path')]"
        [[ $(xmllint --nonet --xpath "count($selector)" "$component/manifests/orangefox-android16-uke.lock.xml") == 1 ]] || return 1
        locked=$(xmllint --nonet --xpath "string($selector/@revision)" \
            "$component/manifests/orangefox-android16-uke.lock.xml")
        [[ $locked =~ ^[0-9a-f]{40}$ ]] || return 1
        actual=$(git -C "$tree/$path" rev-parse HEAD)
        [[ $actual == "$locked" ]] || { echo 'Localization dependency differs from the locked source revision.' >&2; return 1; }
        jq -cn --arg path "$path" --arg commit "$actual" '{path:$path,commit:$commit}' >> "$work/projects.jsonl"
    done < <(jq -r '.source_projects[]' "$config")
    : > "$work/languages"
    while IFS= read -r directory; do
        [[ -d $tree/$directory && ! -L $tree/$directory ]] || return 1
        while IFS= read -r -d '' file; do
            [[ -f $file && ! -L $file ]] || return 1
            code=${file##*/}; code=${code%.xml}
            [[ $code =~ ^[a-zA-Z0-9_-]+$ ]] || return 1
            printf '%s\n' "$code" >> "$work/languages"
        done < <(find "$tree/$directory" -mindepth 1 -maxdepth 1 -name '*.xml' -print0)
    done < <(jq -r '.language_directories[]' "$config")
    jq -Rn '[inputs]|sort' < "$work/languages" > "$work/languages.json"
    cmp <(jq -S '.language_codes' "$config") <(jq -S . "$work/languages.json") || {
        echo 'Supported language resource closure changed or contains duplicates.' >&2; return 1;
    }
    : > "$work/optional.jsonl"
    while IFS= read -r path; do
        if [[ -e $component/$path || -L $component/$path ]]; then
            jq -cn --arg path "$path" '{path:$path,present:true}'
        else
            jq -cn --arg path "$path" '{path:$path,present:false}'
        fi
    done < <(jq -r '.optional[]' "$config") > "$work/optional.jsonl"
    jq -Sn --arg index "$(localization_digest "$work/index/index.sha256")" \
        --argjson count "$(wc -l < "$work/index/files.sha256")" \
        --slurpfile projects "$work/projects.jsonl" --slurpfile optional "$work/optional.jsonl" \
        --slurpfile languages "$work/languages.json" \
        '{schema_version:1,evidence_class:"localization-source-inventory",input_index_sha256:$index,
          file_count:$count,source_projects:($projects|sort_by(.path)),optional_inputs:($optional|sort_by(.path)),
          language_codes:$languages[0],language_count:($languages[0]|length),
          validation:{source_inventory_only:true,generator_executed:false,translation_semantics:false,
            glyph_coverage:false,license_closure:false,physical_device:false}}'
}
localization_ui() {
    local component=$1 payload=$2 work=$3 path resolved
    [[ -d $payload && ! -L $payload ]] || return 1
    payload=$(realpath -e -- "$payload")
    for path in twres sbin/maintainer.xml system/etc/ure/licenses; do
        resolved=$(realpath -e -- "$payload/$path")
        [[ $resolved == "$payload/$path" ]] || { echo 'Indirect GUI resource root refused.' >&2; return 1; }
    done
    [[ -d $payload/twres/languages && ! -L $payload/twres/languages &&
       -d $payload/twres/fonts && ! -L $payload/twres/fonts ]] || return 1
    bash "$component/scripts/index-build-tree.sh" "$payload" "$work/index" ui
    [[ -z $(find "$payload/twres" "$payload/sbin/maintainer.xml" "$payload/system/etc/ure/licenses" -type l -print -quit) ]] || {
        echo 'Indirect GUI resources refused; inventory must not follow host runtime links.' >&2; return 1;
    }
    jq -Sn --arg index "$(localization_digest "$work/index/index.sha256")" \
        --argjson count "$(wc -l < "$work/index/files.sha256")" \
        '{schema_version:1,evidence_class:"gui-resource-inventory",input_index_sha256:$index,file_count:$count,
          includes:{themes:true,icons:true,languages:true,fonts:true,licenses:true,maintainer_pages:true},
          validation:{resource_inventory_only:true,glyph_coverage:false,translation_semantics:false,
            license_closure:false,visual_review:false,physical_device:false}}'
}
localization_verify_gui() {
    local source=$1 ui=$2 record=$3 input assets
    for file in "$source" "$ui" "$record"; do
        [[ -f $file && ! -L $file && $(stat -c '%s' "$file") -le 16777216 ]] || return 1
    done
    input=$(localization_digest "$source")
    assets=$(localization_digest "$ui")
    jq -e --arg input "$input" --arg assets "$assets" '
        .localization_inputs_sha256==$input and .shipping_ui_assets_sha256==$assets and
        (.runs|type)=="array" and (.runs|length)>=2 and
        all(.runs[]; .localization_inputs_sha256==$input and .shipping_ui_assets_sha256==$assets and
            (.overlay_ui_assets_sha256|type)=="string" and (.overlay_ui_assets_sha256|test("^[0-9a-f]{64}$")) and
            .reviewed_ui_assets_sha256==.overlay_ui_assets_sha256)' "$record" >/dev/null
}
