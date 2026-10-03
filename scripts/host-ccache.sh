#!/usr/bin/env bash
# Host compiler cache only. Keep content checks and avoid permissive time/path
# sloppiness inherited from the pinned Android make defaults.
set -euo pipefail
[[ -n ${CCACHE_DIR:-} && -d $CCACHE_DIR && $# -gt 0 ]]
export CCACHE_COMPILERCHECK=content CCACHE_SLOPPINESS= CCACHE_MAXSIZE=10G
unset CCACHE_BASEDIR CCACHE_IGNOREOPTIONS CCACHE_NODIRECT
exec /usr/bin/ccache "$@"
