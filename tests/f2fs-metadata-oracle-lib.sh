#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Host fixture oracle: preserve each file's metadata without inode/log order.
ure_f2fs_fixture_metadata() {
    [[ $# == 2 ]] || return 1
    local log
    for log in "$@"; do
        [[ -f $log && ! -L $log ]] || return 1
    done
    {
    for log in "$@"; do
    awk -- '
        BEGIN { name=""; count=0; invalid=0 }
        $1=="i_name" {
            if(name!="" || NF!=2)invalid=1
            name=$2; sub(/^\[/,"",name); sub(/\]$/,"",name)
            if(name!="details.txt" && name!="payload.bin")invalid=1
        }
        $1~/^i_(mode|uid|gid|links|size)$/ {
            key=$1; value=$NF; sub(/\]$/,"",value)
            if(key in values || value!~/^[0-9]+$/)invalid=1
            values[key]=value; count++
        }
        END {
            if(invalid || name=="" || count!=5)exit 1
            printf "%s\t%s\t%s\t%s\t%s\t%s\n",name,values["i_mode"],values["i_uid"],values["i_gid"],values["i_links"],values["i_size"]
        }
    ' "$log" || return 1
    done
    } | jq -Rse '
        split("\n")|map(select(length>0)|split("\t"))|
        if length!=2 or any(.[];length!=6) then error("Incomplete F2FS inode records") else . end|
        map({name:.[0],mode:(.[1]|tonumber),uid:(.[2]|tonumber),gid:(.[3]|tonumber),links:(.[4]|tonumber),size:(.[5]|tonumber)})|
        sort_by(.name)|
        if map(.name)!=["details.txt","payload.bin"] then error("Duplicate or missing F2FS fixture paths") else . end'
}
