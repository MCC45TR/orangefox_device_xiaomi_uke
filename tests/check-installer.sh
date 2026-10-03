#!/usr/bin/env bash
# Host-only fixtures: production block-device writes are never invoked.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_build=$(mktemp -d)
trap 'rm -rf -- "$test_build"' EXIT
c++ -std=c++20 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    "$component/tests/installer_test.cpp" -lcrypto -o "$test_build/installer-tests"
inputs="$component/referances/firmware/global/derived/stock-payloads-global-os3.0.303.0/uke_global_images_OS3.0.303.0.WOZMIXM_16.0/images"
"$test_build/installer-tests" "$inputs"
