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
  qmp "$(jq -cn --arg path "$job/$name.ppm" '{execute:"screendump",arguments:{filename:$path}}')"
  magick "$job/$name.ppm" -rotate 90 "$job/$name.png"
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
