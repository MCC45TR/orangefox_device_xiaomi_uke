#!/usr/bin/env bash
set -euo pipefail
# QMP control of a disposable test guest only; no native desktop input.
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
job=$(realpath -- "${1:?Pass an active private GUI VM job directory}")
[[ $job == "$component"/build/gui-vm/job-* && ! -L $job && -S $job/qmp.sock && -f $job/initrd.cpio.gz ]]
action=${2:?capture/click/key}
qmp() {
  local response
  response=$({ printf '%s\n' '{"execute":"qmp_capabilities"}'; printf '%s\n' "$1"; } |
    timeout 8 socat -t 1 - "UNIX-CONNECT:$job/qmp.sock")
  printf '%s\n' "$response" >> "$job/interactions.jsonl"
  jq -se '[.[]|select(has("return"))]|length>=2' <<<"$response" >/dev/null
  jq -se 'all(.[]; has("error")|not)' <<<"$response" >/dev/null
}
case $action in
 capture)
  name=${3:?name}; [[ $name =~ ^[a-z][a-z0-9-]{0,60}$ ]]
  [[ -f $job/orientation-fixture.json && ! -L $job/orientation-fixture.json ]] || {
    printf 'Orientation-aware capture requires the per-job VM orientation fixture.\n' >&2; exit 1;
  }
  rotation=$(jq -er '.rotation_property.value|select(.==0 or .==90 or .==180 or .==270)' "$job/orientation-fixture.json")
  review_rotation=$(((360-rotation)%360))
  qmp "$(jq -cn --arg path "$job/$name.ppm" '{execute:"screendump",arguments:{filename:$path}}')"
  read -r raw_width raw_height < <(magick identify -format '%w %h\n' "$job/$name.ppm")
  [[ $raw_width =~ ^[1-9][0-9]{2,3}$ && $raw_height =~ ^[1-9][0-9]{2,3}$ ]]
  jq -e --argjson width "$raw_width" --argjson height "$raw_height" \
    '.raw_framebuffer_requested.width==$width and .raw_framebuffer_requested.height==$height' \
    "$job/orientation-fixture.json" >/dev/null
  magick "$job/$name.ppm" "$job/$name-raw.png"
  magick "$job/$name-raw.png" -rotate "$review_rotation" "$job/$name.png"
  jq -n --argjson width "$raw_width" --argjson height "$raw_height" \
    --argjson review_rotation "$review_rotation" --slurpfile orientation "$job/orientation-fixture.json" \
    --arg raw "$(sha256sum "$job/$name-raw.png"|cut -d' ' -f1)" \
    --arg review "$(sha256sum "$job/$name.png"|cut -d' ' -f1)" \
    '{schema_version:1,evidence_class:"adapted-generic-gui-vm-capture",
      raw_capture:{width:$width,height:$height,sha256:$raw},review_image_sha256:$review,
      review_clockwise_rotation_degrees:$review_rotation,orientation_fixture:$orientation[0],
      logical_dimensions_runtime_measured:false,visual_inspection:false,physical_device:false}' \
    > "$job/$name.capture.json"
  ;;
 click)
  x=${3:?x}; y=${4:?y}; [[ $x =~ ^[0-9]{1,4}$ && $y =~ ^[0-9]{1,4}$ ]]
  qmp '{"execute":"input-send-event","arguments":{"events":[{"type":"rel","data":{"axis":"x","value":-4096}},{"type":"rel","data":{"axis":"y","value":-4096}}]}}'
  qmp "$(jq -cn --argjson x "$((x*2/5))" --argjson y "$((y*2/5))" '{execute:"input-send-event",arguments:{events:[{type:"rel",data:{axis:"x",value:$x}},{type:"rel",data:{axis:"y",value:$y}}]}}')"
  # A QMP acknowledgement means queued input, not a guest-rendered cursor.
  # Let the TCG guest consume movement before the button edge is queued.
  sleep 2
  qmp '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":true}}]}}'
  # TCG rendering can lag behind host events; retain a visible press long enough
  # to inspect it without pretending this measures physical input latency.
  sleep 1
  qmp '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":false}}]}}'
  sleep 2
  ;;
 key)
  key=${3:?key}; [[ $key =~ ^(f6|up|down|ret|esc|backspace|tab)$ ]]
  qmp "$(jq -cn --arg key "$key" '{execute:"send-key",arguments:{keys:[{type:"qcode",data:$key}],"hold-time":120}}')"
  ;;
 *) exit 2;;
esac
