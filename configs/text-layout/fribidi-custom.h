/* SPDX-License-Identifier: Apache-2.0 */
#ifndef URE_FRIBIDI_MEMORY_H
#define URE_FRIBIDI_MEMORY_H
#include <stddef.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void* ure_layout_malloc(size_t bytes);
void ure_layout_free(void* block);
#ifdef __cplusplus
}
#endif
#define fribidi_malloc ure_layout_malloc
#define fribidi_free ure_layout_free
#endif
