#!/usr/bin/env bash
# Verify first-load theme ordering and physical navigation exclusion, host only.
set -euo pipefail
export LC_ALL=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
vars=${1:-"$component/src/upstream/orangefox-android16/bootable/recovery/gui/theme/portrait_hdpi/resources/vars.xml"}
xmllint --noout "$vars"
# PageSet::LoadVariables evaluates in document order; missing references become 0.
awk '
 /name="nav_panel_y"/ {nav=NR}
 /name="ab_h"/ {bar=NR}
 /name="scroll_nav_h"/ {
   if (!nav || !bar || nav>=NR || bar>=NR || seen++) exit 1
 }
 END {if (seen!=1) exit 1}
' "$vars" || { echo 'Scroll viewport uses an undefined or duplicate bar height.' >&2; exit 1; }
expression() { xmllint --xpath "string(/recovery/variables/variable[@name='$1']/@value)" "$vars"; }
[[ $(expression nav_panel_y) == '%screen_h%-360' ]]
[[ $(expression ab_h) == '240+%cutout_w%' ]]
[[ $(expression scroll_nav_h) == '%nav_panel_y%-%ab_h%' ]]
# The same scale is applied to viewport placement and tab hit rectangles.
for screen in '2136 3200' '3200 2136' '1080 1920'; do
    read -r width height <<< "$screen"
    for scale in {50..100..5}; do
        for cutout in 0 43; do
            canvas_height=$(((height*100+scale/2)/scale))
            bar=$((240+cutout))
            nav=$((canvas_height-360))
            viewport=$((nav-bar))
            (( viewport>0 ))
            viewport_end=$((bar*scale/100+viewport*scale/100))
            nav_start=$((nav*scale/100))
            nav_button_y=$(((canvas_height-252)*scale/100))
            (( viewport_end<=nav_start && nav_start-viewport_end<=1 && nav_button_y>viewport_end ))
        done
    done
done
printf '%s\n' 'Theme viewport: cold-load dependency order and navigation exclusion passed at 50–100% in portrait/landscape, with/without a cutout.'
