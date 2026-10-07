#!/usr/bin/env bash
# Compare UI bytes/membership separately from Android's packaging permissions.
# The caller must also verify canonical mkbootfs/fs_config payload metadata.
set -euo pipefail
export LC_ALL=C
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$component/scripts/localization-evidence-lib.sh"
staged=${1:?Staged payload required}
extracted=${2:?Extracted payload required}
work=${3:?New private work directory required}
[[ $# == 3 && ! -e $work && ! -L $work ]]
mkdir -m700 -- "$work"
work=$(realpath -e -- "$work")
mkdir -m700 -- "$work/staged" "$work/extracted"
localization_ui "$component" "$staged" "$work/staged" > "$work/staged-ui.json"
localization_ui "$component" "$extracted" "$work/extracted" > "$work/extracted-ui.json"
cmp -- "$work/staged/index/files.sha256" "$work/extracted/index/files.sha256"
for kind in staged extracted; do
    # Index generation has already rejected ambiguous paths/special objects.
    # Retain order, paths, object types, normalized timestamps and link targets.
    awk -v RS='\0' -F '\t' '{printf "%s\t%s\t-\t%s\t%s%c",$1,$2,$4,$5,0}' \
        "$work/$kind/index/layout.bin" > "$work/$kind-layout-without-modes.bin"
done
cmp -- "$work/staged-layout-without-modes.bin" "$work/extracted-layout-without-modes.bin"
jq -n --arg staged "$(localization_digest "$work/staged-ui.json")" \
    --arg comparator "$(localization_digest "${BASH_SOURCE[0]}")" \
    --arg extracted "$(localization_digest "$work/extracted-ui.json")" \
    --arg contents "$(localization_digest "$work/extracted/index/files.sha256")" \
    --arg layout "$(localization_digest "$work/extracted-layout-without-modes.bin")" \
    '{schema_version:1,evidence_class:"gui-packaging-resource-parity",
      comparator_sha256:$comparator,
      staged_ui_assets_sha256:$staged,extracted_ui_assets_sha256:$extracted,
      content_manifest_sha256:$contents,member_layout_projection_sha256:$layout,
      validation:{file_bytes_match:true,members_types_links_match:true,
        mode_aware_inventories_retained:true,canonical_packed_modes_verified:false,
        physical_device:false,visual_review:false}}'
