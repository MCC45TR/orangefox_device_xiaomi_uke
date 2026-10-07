#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Host-only QEMU syscall-text oracle. Matching a lookup attempt does not prove
# loading an implementation, and separate diagnostics do not protect a trace
# against tampering by an adversarial target with inherited output descriptors.

ure_packed_startup_boot_lookup_regex() {
    local path_call implementation hardware_directory
    path_call='(open|openat|openat2|access|faccessat|faccessat2|stat|lstat|newfstatat|statx|readlink|readlinkat)\('
    # Match the complete basename independently of its root or directory prefix.
    # Absolute, /payload-prefixed, system_ext and relative paths use one oracle.
    implementation='"([^"]*/)?(bootctrl[^"/]*|android\.hardware\.boot[^"/]*-impl[^"/]*)"'
    # A HAL directory enumeration is a resolution attempt even when there is no
    # implementation file to open. A relative "lib64/hw" must not escape this.
    hardware_directory='"([^"]*/)?hw(/|")'
    printf '%s.*(%s|%s)\n' "$path_call" "$implementation" "$hardware_directory"
}
