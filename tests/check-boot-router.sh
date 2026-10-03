#!/usr/bin/env bash
# Exercise the real JSON CLI on private EFI file fixtures, including ARM64/QEMU.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-"$component/build/ure-host/uke-recoveryctl"}
fixture="$component/build/ure-host/uke-boot-router-tests"
work=$(mktemp -d /tmp/ure-boot-cli-XXXXXX)
trap 'rm -rf -- "$work"' EXIT
"$fixture" --fixture "$work"
vars="$work/variables"
esp="$work/esp"
next="$vars/BootNext-8be4df61-93ca-11d2-aa0d-00e098032b8c"
order="$vars/BootOrder-8be4df61-93ca-11d2-aa0d-00e098032b8c"
original_order=$(sha256sum "$order" | cut -d' ' -f1)
expect_error() {
    local code=$1; shift
    if "$binary" "$@" > "$work/error.json"; then echo 'Unexpected boot CLI success' >&2; exit 1; fi
    jq -e --arg code "$code" '.result=="error" and .error.code==$code' "$work/error.json" >/dev/null
}
"$binary" boot route-inventory --esp "$esp" --variables "$vars" | jq -e \
    '.data.default_option=="0000" and (.data.options|length)==4 and (.data.physical_device_write_allowed==false)' >/dev/null
expect_error invalid-options boot route-inventory --esp "$esp" --variables "$vars" --confirm unused
"$binary" boot route-plan "$work/request.json" --esp "$esp" --variables "$vars" --output "$work/plan.json" >/dev/null
confirmation=$(jq -er .plan_sha256 "$work/plan.json")
expect_error confirmation-required boot route-execute "$work/plan.json" --esp "$esp" --variables "$vars" --journal "$work/rejected" --confirm wrong
[[ ! -e $work/rejected && ! -e $next ]]
"$binary" boot route-execute "$work/plan.json" --esp "$esp" --variables "$vars" --journal "$work/journal" --confirm "$confirmation" | jq -e \
    '.data.state.phase=="ARMED" and .data.default_preserved and (.data.reboot_performed==false)' >/dev/null
[[ $(od -An -tx1 -N6 "$next" | tr -d ' \n') == 070000000100 ]]
"$binary" boot route-consume-fixture "$work/journal" --esp "$esp" --variables "$vars" --confirm "$confirmation" > "$work/handoff.json"
[[ ! -e $next ]]
expect_error boot-already-consumed boot route-consume-fixture "$work/journal" --esp "$esp" --variables "$vars" --confirm "$confirmation"
jq '.data | {schema:1,request_id,attempt_id:.state.attempt_id,handoff_token,boot_id:"deadbeef-1234-5678-9abc-def012345678",loader_sha256:.decision.loader_identity.sha256,result:"failure"}' \
    "$work/handoff.json" > "$work/receipt.json"
"$binary" boot route-ack-fixture "$work/journal" --esp "$esp" --variables "$vars" --confirm "$confirmation" --receipt "$work/receipt.json" | jq -e \
    '.data.state.phase=="FALLBACK_PENDING" and (.data.state.physical_boot_success==false)' >/dev/null
"$binary" boot route-fallback-fixture "$work/journal" --esp "$esp" --variables "$vars" --confirm "$confirmation" | jq -e \
    '.data.decision.number=="0000" and .data.state.phase=="FALLBACK_SELECTED" and (.data.efi_application_started==false)' >/dev/null
"$binary" boot route-history "$work/journal" | jq -e \
    '.data.attempts==1 and (.data.events|length)==6 and (.data.physical_boot_success==false)' >/dev/null
[[ $(sha256sum "$order" | cut -d' ' -f1) == "$original_order" && ! -e $next && ! -e $vars/.ure-boot-owner.json ]]
expect_error boot-plan-replayed boot route-execute "$work/plan.json" --esp "$esp" --variables "$vars" --journal "$work/replay" --confirm "$confirmation"
[[ ! -e $work/replay && ! -e $next ]]
rm -- "$vars/.ure-efi-fixture.json" "$vars/.ure-boot-lock"
"$binary" boot route-plan "$work/request.json" --esp "$esp" --variables "$vars" --output "$work/runtime-plan.json" >/dev/null
runtime_hash=$(jq -er .plan_sha256 "$work/runtime-plan.json")
expect_error boot-backend-unverified boot route-execute "$work/runtime-plan.json" --esp "$esp" --variables "$vars" --journal "$work/runtime-rejected" --confirm "$runtime_hash"
[[ ! -e $work/runtime-rejected && ! -e $next && ! -e $vars/.ure-boot-lock ]]
printf '%s\n' 'One-shot boot CLI/default/consumption/failure/fallback/history and retired-plan cross-journal replay refusal passed; no real EFI write, network, reboot or tablet acceptance.'
