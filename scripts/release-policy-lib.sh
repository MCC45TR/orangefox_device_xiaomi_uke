#!/usr/bin/env bash
# Host release preflight. Artifact names never select evidence requirements.
set -euo pipefail
release_digest() { sha256sum -- "$1" | cut -d' ' -f1; }
release_regular_json() {
    [[ -f $1 && ! -L $1 && $(stat -c '%s' "$1") -le 16777216 ]] || return 1
    jq -e 'type=="object"' "$1" >/dev/null
}
release_mutable_destination() {
    local destination=$1
    [[ ! -L $destination && ( ! -e $destination || -d $destination ) ]] || return 1
    [[ ! -e $destination/ARTIFACT-MANIFEST.json && ! -L $destination/ARTIFACT-MANIFEST.json ]] || {
        echo 'A sealed candidate is immutable; choose a new candidate directory.' >&2; return 1;
    }
    if [[ -d $destination ]]; then
        [[ $(stat -c '%u' "$destination") == "$UID" ]] || return 1
        [[ -z $(find "$destination" -mindepth 1 -maxdepth 1 ! -type f -print -quit) ]] || return 1
    fi
}
release_output_mutable() {
    local component=$1 output=$2 resolved artifacts relative candidate
    [[ ! -L $output ]] || return 1
    resolved=$(realpath -m -- "$output")
    artifacts=$(realpath -m -- "$component/artifacts")
    if [[ $resolved == "$artifacts/"* ]]; then
        relative=${resolved#"$artifacts/"}
        candidate=${relative%%/*}
        release_mutable_destination "$artifacts/$candidate"
    fi
}
release_plan_matches() {
    local destination=$1 plan=$2
    if [[ -e $destination/RELEASE-POLICY.json || -L $destination/RELEASE-POLICY.json ]]; then
        release_regular_json "$destination/RELEASE-POLICY.json" || return 1
        cmp "$plan" "$destination/RELEASE-POLICY.json" >/dev/null || {
            echo 'Existing candidate is bound to a different release policy; choose a new candidate.' >&2; return 1;
        }
    fi
}
release_normalize_stream() {
    local policy=$1
    jq -Se --slurpfile policy "$policy" --arg digest "$(release_digest "$policy")" '
        . as $request | $policy[0] as $p |
        if $p.schema_version!=1 or .schema_version!=1 or
           (keys|sort)!=["capabilities","release_class","schema_version"] or
           (.release_class|type)!="string" or ($p.classes[.release_class]|type)!="object" or
           (.capabilities|type)!="array" then error("Invalid or unknown explicit release request") else . end |
        if (.capabilities|all(.[]; type=="string")) and
           (.capabilities|length)==(.capabilities|unique|length) and
           (.capabilities|sort)==($p.shipping_capabilities|sort) then .
        else error("Capabilities must include every shipped feature exactly once") end |
        [.capabilities[] as $cap | $p.capabilities[$cap][$request.release_class][]] as $extra |
        ($p.classes[.release_class].required_receipts + $extra | unique | sort) as $required |
        if all($required[]; ($p.receipts[.]|type)=="object") then
          {schema_version:1,release_class:.release_class,capabilities:(.capabilities|sort),
           policy_sha256:$digest,required_receipts:$required,physical_acceptance:false,
           shipping_kernel_acceptance:false,complete_feature_acceptance:false}
        else error("Release policy references an unknown receipt") end'
}
release_normalize() {
    local policy=$1 request=$2
    release_regular_json "$policy" || return 1
    release_regular_json "$request" || return 1
    [[ $(stat -c '%s' "$request") -le 65536 ]] || return 1
    release_normalize_stream "$policy" < "$request"
}
release_validate_plan() {
    local policy=$1 plan=$2
    release_regular_json "$plan" || return 1
    [[ $(stat -c '%s' "$plan") -le 65536 ]] || return 1
    # Recompute the requirements; supplied requirements or policy digests cannot
    # lower the current reviewed policy. Candidate names are absent by design.
    diff -u <(jq -S . "$plan") <(
        jq '{schema_version,release_class,capabilities}' "$plan" | release_normalize_stream "$policy"
    ) >/dev/null
}
release_has_requirement() { jq -e --arg requirement "$2" '.required_receipts|index($requirement)!=null' "$1" >/dev/null; }
release_native_pair() {
    local native=$1 sanitizer=$2 inputs=$3 catalog=$4 cli=$5 record localization
    release_regular_json "$native" || return 1
    release_regular_json "$sanitizer" || return 1
    [[ -f $inputs && ! -L $inputs && -f $catalog && ! -L $catalog && -f $cli && ! -L $cli ]] || return 1
    jq -e 'type=="array" and length>0 and .==(sort|unique) and all(.[];type=="string")' "$catalog" >/dev/null || return 1
    localization=$(sed -n 's/^# localization_source_sha256=//p' "$inputs")
    [[ $localization =~ ^[0-9a-f]{64}$ ]] || return 1
    for record in "$native" "$sanitizer"; do
        jq -e --arg inputs "$(release_digest "$inputs")" --arg localization "$localization" --slurpfile catalog "$catalog" \
            '.schema_version==1 and .native_test_inputs_sha256==$inputs and .ctest_test_names==$catalog[0] and
             .localization_inputs_sha256==$localization and .validation.localization_input_closure==true and
             .ctest_executable_count==($catalog[0]|length) and (.validation.physical_device==false)' "$record" >/dev/null || return 1
    done
    jq -e --arg binary "$(release_digest "$cli")" \
        '.host_cli_sha256==$binary and .validation.name_independent_release_policy==true' "$native" >/dev/null || return 1
    jq -e '.compiler_sha256=="55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f" and
           .validation.address_sanitizer==true and .validation.undefined_behavior_sanitizer==true and
           .validation.leak_detection==true and .validation.halt_on_error==true' "$sanitizer" >/dev/null
}
release_receipt_hashes() {
    local policy=$1 plan=$2 artifact=$3 private=$4 id scope file root
    release_check_receipts "$policy" "$plan" "$artifact" "$private" || return 1
    while IFS= read -r id; do
        scope=$(jq -er --arg id "$id" '.receipts[$id].scope' "$policy")
        file=$(jq -er --arg id "$id" '.receipts[$id].file' "$policy")
        root=$private
        [[ $scope != artifact ]] || root=$artifact
        jq -cn --arg id "$id" --arg digest "$(release_digest "$root/$file")" '{id:$id,sha256:$digest}'
    done < <(jq -r '.required_receipts[]' "$plan")
}
release_check_receipts() {
    local policy=$1 plan=$2 artifact=$3 private=$4 id scope name receipt
    release_validate_plan "$policy" "$plan" || return 1
    while IFS= read -r id; do
        scope=$(jq -er --arg id "$id" '.receipts[$id].scope' "$policy")
        name=$(jq -er --arg id "$id" '.receipts[$id].file' "$policy")
        [[ $name =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]+\.json$ ]] || return 1
        case $scope in artifact) receipt="$artifact/$name";; private) receipt="$private/$name";; *) return 1;; esac
        if ! release_regular_json "$receipt"; then
            printf 'Required release receipt is absent, indirect or invalid: %s\n' "$id" >&2; return 1
        fi
    done < <(jq -r '.required_receipts[]' "$plan")
    # Presence/JSON preflight only. Exact source, ELF, runner, scope and semantic
    # receipt validation is mandatory in describe-prerelease after this check.
}
