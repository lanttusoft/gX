// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2025-2026 lanttu, opencode
// This file is part of gX. See LICENSE.

//ai: libFuzzer harness for the in-tree JSON parser (presets/schemas are the
//   main untrusted input surface). Including the implementation gives direct
//   access to the static parser; build with -fsanitize=fuzzer,address.
#include <stdint.h>
#include <stddef.h>

#include "../src/common/marshal/json_impl.c"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0 || size > (1u << 20)) return 0;
    parser ps;
    ps.p = (const char *)data;
    ps.end = (const char *)data + size;
    ps.depth = 0;
    struct json_object *root = parse_value(&ps);
    if (root) json_object_put(root);
    return 0;
}
