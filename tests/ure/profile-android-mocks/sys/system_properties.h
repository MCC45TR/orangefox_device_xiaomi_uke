// SPDX-License-Identifier: Apache-2.0
// Host-only Android property ABI stand-in; no tablet runtime implementation.
#pragma once
#define PROP_VALUE_MAX 92
extern "C" int __system_property_get(const char*,char*);
