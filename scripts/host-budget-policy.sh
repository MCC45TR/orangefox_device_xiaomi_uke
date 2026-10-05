#!/usr/bin/env bash
# Host-only admission policy. Optional snapshot roots are explicit fixture inputs.
set -euo pipefail
export LC_ALL=C
if [[ ${1:-} == --compile-jobs ]]; then
    requested=${2:?Requested compile count required}
    ceiling=${3:?Admitted compile ceiling required}
    [[ $# == 3 && $requested =~ ^([1-9]|1[0-6])$ && $ceiling =~ ^([1-9]|1[0-6])$ ]]
    (( requested <= ceiling )) || requested=$ceiling
    printf '%s\n' "$requested"
    exit 0
fi
mode=${1:?Usage: host-budget-policy.sh MODE [PROC_ROOT CGROUP_ROOT caller|owned]}
proc_root=${2:-/proc}
group_root=${3:-/sys/fs/cgroup}
role=${4:-caller}
[[ $mode =~ ^[a-z][a-z0-9-]{0,40}$ && ( $role == caller || $role == owned ) ]]
command -v jq >/dev/null
read_integer() {
    local value
    value=$(cat -- "$1")
    [[ $value =~ ^(0|[1-9][0-9]*)$ && ${#value} -le 19 ]] || return 1
    # Saturate only values larger than any supported physical host. This avoids
    # arithmetic overflow for an effectively unlimited cgroup counter/limit.
    awk -v value="$value" 'BEGIN {printf "%.0f\n",(value>1099511627776 ? 1099511627776 : value)}'
}
read_limit() {
    local value
    value=$(cat -- "$1")
    if [[ $value == max ]]; then printf 'null\n'; else read_integer "$1"; fi
}
memory=$(awk '/^MemTotal:/ {t=$2; nt++} /^MemAvailable:/ {a=$2; na++}
  END {if(nt!=1 || na!=1 || t!~/^[0-9]+$/ || a!~/^[0-9]+$/ || a>t || t<1)exit 1;
       printf "{\"total_mib\":%.0f,\"available_mib\":%.0f}\n",int(t/1024),int(a/1024)}' "$proc_root/meminfo")
allowed=$(awk '/^Cpus_allowed_list:/ {n++; list=$2} END {if(n!=1)exit 1; print list}' "$proc_root/self/status")
[[ $allowed =~ ^[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*$ ]]
affinity=$(awk -v list="$allowed" 'BEGIN {n=split(list,g,","); count=0; previous=-1;
  for(i=1;i<=n;i++) {m=split(g[i],r,"-"); last=m==2?r[2]:r[1];
    if(r[1]<=previous || last<r[1] || last>1048575)exit 1; previous=last;
    for(cpu=r[1];cpu<=last && count<16;cpu++) {printf "%s%d",count?",":"",cpu;count++}}
  if(!count)exit 1; print ""}')
[[ $affinity =~ ^[0-9]+(,[0-9]+)*$ ]]
cpus=$(awk -F, '{print NF}' <<<"$affinity")
relative=$(awk -F: '$1==0 && $2=="" {n++; p=$3} END {if(n!=1)exit 1; print p}' "$proc_root/self/cgroup")
[[ $relative == /* && $relative != *'/../'* && $relative != *'/./'* && $relative != */.. && $relative != */. && $relative != *' (deleted)' ]]
root=$(realpath -e -- "$group_root")
leaf=$(realpath -e -- "$root$relative")
[[ $leaf == "$root" || $leaf == "$root/"* ]]
current=$leaf
groups='[]'
own_ceiling=null
while :; do
    if [[ -f $current/memory.max && -f $current/memory.high && -f $current/memory.current ]]; then
        maximum=$(read_limit "$current/memory.max")
        high=$(read_limit "$current/memory.high")
        used=$(read_integer "$current/memory.current")
        if [[ $role == owned && $current == "$leaf" ]]; then
            own_ceiling=$maximum
        else
            groups=$(jq -cn --argjson all "$groups" --argjson maximum "$maximum" --argjson high "$high" --argjson used "$used" \
              '$all + [{maximum_bytes:$maximum,high_bytes:$high,current_bytes:$used}]')
        fi
    elif [[ $current != "$root" ]]; then
        echo 'Cgroup memory accounting is unreadable; host admission refused.' >&2; exit 1
    fi
    [[ $current != "$root" ]] || break
    current=${current%/*}
done
plan=$(jq -cn --argjson memory "$memory" --argjson groups "$groups" --argjson own "$own_ceiling" \
    --argjson cpus "$cpus" --arg affinity "$affinity" --arg mode "$mode" '
  def mib: (. / 1048576 | floor);
  ($groups | map([.maximum_bytes,.high_bytes]|map(select(.!=null))|min) | map(select(.!=null))) as $limits |
  ([ $memory.total_mib ] + ($limits|map(mib)) | min) as $capacity |
  ([4096,($capacity/4|floor)]|max) as $reserve |
  ($groups | map(. as $g | ([.maximum_bytes,.high_bytes]|map(select(.!=null))|min) as $l |
    if $l==null then empty else ([0,($l-$g.current_bytes)]|max|mib) end)) as $headrooms |
  ([ $memory.available_mib ] + $headrooms | min) as $available |
  ([16384,($available-$reserve)] + (if $own==null then [] else [$own|mib] end)|min|floor) as $maximum |
  (if $mode=="arm64" then 4096 else 1536 end) as $minimum |
  (if $mode=="sanitizer" then 1280 else 768 end) as $per_compile |
  ([2048,($maximum/3|floor)]|min) as $internal |
  ([16,$cpus,([1,(($maximum-$internal)/$per_compile|floor)]|max)]|min) as $jobs |
  ([6144,($maximum/2|floor)]|min) as $heap |
  ([8,$cpus,$jobs,([1,($heap/1024|floor)]|max)]|min) as $soong |
  # Full Soong graphs sustained excessive reclaim at 85 percent. Keep their
  # soft threshold closer to the same hard ceiling; desktop reserve is unchanged.
  (if $mode=="arm64" then 0.95 else 0.85 end) as $high_fraction |
  {schema_version:1,mode:$mode,host:$memory,effective_capacity_mib:$capacity,
   shared_available_mib:$available,shared_reserve_mib:$reserve,minimum_mib:$minimum,
   memory_max_mib:$maximum,memory_high_mib:($maximum*$high_fraction|floor),
   memory_high_fraction:$high_fraction,swap_max_mib:512,
   compile_jobs:$jobs,compile_worker_allowance_mib:$per_compile,internal_reserve_mib:$internal,
   soong_procs:$soong,go_heap_mib:$heap,affinity:$affinity,allowed_cpu_count:$cpus,
   admitted:($maximum>=$minimum),ancestor_memory:$groups}')
jq -e '.admitted == true' <<<"$plan" >/dev/null || {
    echo 'Insufficient RAM or ancestor cgroup headroom after the desktop reserve; heavy job refused.' >&2
    exit 1
}
printf '%s\n' "$plan"
