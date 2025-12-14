/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AVM_MEM_AVM_MEM_H_
#define AVM_AVM_MEM_AVM_MEM_H_

#include "avm/avm_integer.h"
#include "config/avm_config.h"

#if defined(__uClinux__)
#include <lddk.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef AVM_MAX_ALLOCABLE_MEMORY
#if SIZE_MAX > (1ULL << 32)
#define AVM_MAX_ALLOCABLE_MEMORY 8589934592  // 8 GB
#else
// For 32-bit targets keep this below INT_MAX to avoid valgrind warnings.
#define AVM_MAX_ALLOCABLE_MEMORY ((1ULL << 31) - (1 << 16))
#endif
#endif

void *avm_memalign(size_t align, size_t size);
void *avm_malloc(size_t size);
void *avm_calloc(size_t num, size_t size);
void avm_free(void *memblk);
void *avm_memset16(void *dest, int val, size_t length);
void *avm_memset_int16(void *dest, int16_t val, size_t length);

/*returns an addr aligned to the byte boundary specified by align*/
#define avm_align_addr(addr, align) \
  (void *)(((uintptr_t)(addr) + ((align) - 1)) & ~(uintptr_t)((align) - 1))

#include <string.h>

#ifdef AVM_MEM_PLTFRM
#include AVM_MEM_PLTFRM
#endif

#if CONFIG_DEBUG
#define AVM_CHECK_MEM_ERROR(error_info, lval, expr)                         \
  do {                                                                      \
    lval = (expr);                                                          \
    if (!lval)                                                              \
      avm_internal_error(error_info, AVM_CODEC_MEM_ERROR,                   \
                         "Failed to allocate " #lval " at %s:%d", __FILE__, \
                         __LINE__);                                         \
  } while (0)
#else
#define AVM_CHECK_MEM_ERROR(error_info, lval, expr)       \
  do {                                                    \
    lval = (expr);                                        \
    if (!lval)                                            \
      avm_internal_error(error_info, AVM_CODEC_MEM_ERROR, \
                         "Failed to allocate " #lval);    \
  } while (0)
#endif

#if defined(__cplusplus)
}
#endif

#endif  // AVM_AVM_MEM_AVM_MEM_H_
