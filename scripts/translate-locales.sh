#!/usr/bin/env bash
# Optional host-only generation. Never used by a build, tablet or CI test.
# Input is public source text; private device/user content is not accepted.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
worker() {
    local job=$1 name target directory attempt child
    name=${job##*/}; name=${name%.json}
    directory="$locale_results/$name"
    mkdir -p "$directory"
    if [[ -f $directory/response.json ]] && "$locale_tool" import "$job" "$directory/response.json" "$directory/validated.json" 2>"$directory/import.log"; then
        return
    fi
    if [[ -d $directory/children ]] && "$locale_tool" combine "$job" "$directory/children" "$directory/validated.json" 2>"$directory/import.log"; then return; fi
    target=$(jq -er .target "$job")
    jq -jr .query "$job" > "$directory/query.txt"
    for attempt in 1 2 3; do
        if curl -q -fSs --max-time 35 --connect-timeout 10 --get \
            https://translate.googleapis.com/translate_a/single \
            --data-urlencode client=gtx --data-urlencode sl=en --data-urlencode "tl=$target" \
            --data-urlencode dt=t --data-urlencode "q@$directory/query.txt" \
            -o "$directory/response.json" 2>"$directory/network.log" && \
            "$locale_tool" import "$job" "$directory/response.json" "$directory/validated.json" 2>"$directory/import.log"; then
            printf 'Validated %s\n' "$name"
            sleep 0.5
            return
        fi
        sleep "$((attempt * 2))"
    done
    if [[ $(jq -r '.single_message // false' "$job") == false ]]; then
        "$locale_tool" split "$job" "$directory/children"
        local locale_results="$directory/children"
        for child in "$directory"/children/*.json; do worker "$child" || return 1; done
        "$locale_tool" combine "$job" "$directory/children" "$directory/validated.json"
        printf 'Validated %s with separately verified messages\n' "$name"
        return
    fi
    printf 'Translation validation failed: %s (private diagnostic files retained)\n' "$name" >&2
    return 1
}
if [[ ${1:-} == --worker ]]; then worker "${2:?Job required}"; exit; fi
[[ ${1:-} == --fetch && $# == 4 ]] || {
    echo 'Usage: translate-locales.sh --fetch HOST_TOOL JOB_DIRECTORY RESULT_DIRECTORY' >&2; exit 2;
}
export locale_tool=$(realpath "${2}") locale_results=$(realpath -m "${4}")
jobs_directory=$(realpath "${3}")
[[ -x $locale_tool && -d $jobs_directory ]]
umask 077
mkdir -p "$locale_results"
# Two bounded workers keep host and provider load modest. No shell evaluation of
# source text; curl reads it from a private file and receives no credentials.
find "$jobs_directory" -maxdepth 1 -type f -name '*.json' -print0 | sort -z | \
    xargs -0 -r -P 2 -I '{}' bash "$component/scripts/translate-locales.sh" --worker '{}'
