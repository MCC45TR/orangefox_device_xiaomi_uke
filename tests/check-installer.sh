#!/usr/bin/env bash
# Host-only fixtures: production block-device writes are never invoked.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_build=$(mktemp -d)
trap 'rm -rf -- "$test_build"' EXIT
c++ -std=c++20 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    "$component/tests/installer_test.cpp" -lcrypto -o "$test_build/installer-tests"
"$test_build/installer-tests"
