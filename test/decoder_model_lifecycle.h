/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_TEST_DECODER_MODEL_LIFECYCLE_H_
#define AVM_TEST_DECODER_MODEL_LIFECYCLE_H_

#include "av2/common/decoder_model.h"
#include "av2/decoder/decoder_model.h"

namespace libavm_test {

class ScopedDmBufferPool : public ::Av2DmBufferPool {
 public:
  ScopedDmBufferPool() { av2_dm_buffer_pool_init(this); }
  ~ScopedDmBufferPool() { av2_dm_buffer_pool_destroy(this); }
  ScopedDmBufferPool(const ScopedDmBufferPool &) = delete;
  ScopedDmBufferPool &operator=(const ScopedDmBufferPool &) = delete;
};

class ScopedDmLevelLimits : public ::Av2DmLevelLimits {
 public:
  ScopedDmLevelLimits() { av2_dm_level_limits_init(this); }
  ~ScopedDmLevelLimits() { av2_dm_level_limits_destroy(this); }
  ScopedDmLevelLimits(const ScopedDmLevelLimits &) = delete;
  ScopedDmLevelLimits &operator=(const ScopedDmLevelLimits &) = delete;
};

class ScopedDmState : public ::Av2DmState {
 public:
  ScopedDmState() { av2_dm_state_init(this); }
  ~ScopedDmState() { av2_dm_state_destroy(this); }
  ScopedDmState(const ScopedDmState &) = delete;
  ScopedDmState &operator=(const ScopedDmState &) = delete;
};

class ScopedDmVerifierStats : public ::Av2DmVerifierStats {
 public:
  ScopedDmVerifierStats() { av2_decoder_model_verifier_stats_init(this); }
  ~ScopedDmVerifierStats() { av2_decoder_model_verifier_stats_destroy(this); }
  ScopedDmVerifierStats(const ScopedDmVerifierStats &) = delete;
  ScopedDmVerifierStats &operator=(const ScopedDmVerifierStats &) = delete;
};

class ScopedDmRunStats : public ::Av2DmRunStats {
 public:
  ScopedDmRunStats() { av2_decoder_model_run_stats_init(this); }
  ~ScopedDmRunStats() { av2_decoder_model_run_stats_destroy(this); }
  ScopedDmRunStats(const ScopedDmRunStats &) = delete;
  ScopedDmRunStats &operator=(const ScopedDmRunStats &) = delete;
};

}  // namespace libavm_test

#endif  // AVM_TEST_DECODER_MODEL_LIFECYCLE_H_
