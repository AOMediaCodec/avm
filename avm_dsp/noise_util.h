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

#ifndef AVM_AVM_DSP_NOISE_UTIL_H_
#define AVM_AVM_DSP_NOISE_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// avm_noise_tx_t is an abstraction of a transform that is used for denoising.
// It is meant to be lightweight and does hold the transformed data (as
// the user should not be manipulating the transformed data directly).
struct avm_noise_tx_t;

// Allocates and returns a avm_noise_tx_t useful for denoising the given
// block_size. The resulting avm_noise_tx_t should be free'd with
// avm_noise_tx_free.
struct avm_noise_tx_t *avm_noise_tx_malloc(int block_size);
void avm_noise_tx_free(struct avm_noise_tx_t *avm_noise_tx);

// Transforms the internal data and holds it in the avm_noise_tx's internal
// buffer. For compatibility with existing SIMD implementations, "data" must
// be 32-byte aligned.
void avm_noise_tx_forward(struct avm_noise_tx_t *avm_noise_tx,
                          const float *data);

// Filters avm_noise_tx's internal data using the provided noise power spectral
// density. The PSD must be at least block_size * block_size and should be
// populated with a constant or via estimates taken from
// avm_noise_tx_add_energy.
void avm_noise_tx_filter(struct avm_noise_tx_t *avm_noise_tx, const float *psd);

// Performs an inverse transform using the internal transform data.
// For compatibility with existing SIMD implementations, "data" must be 32-byte
// aligned.
void avm_noise_tx_inverse(struct avm_noise_tx_t *avm_noise_tx, float *data);

// Aggregates the power of the buffered transform data into the psd buffer.
void avm_noise_tx_add_energy(const struct avm_noise_tx_t *avm_noise_tx,
                             float *psd);

// Returns a default value suitable for denosing a transform of the given
// block_size. The noise "factor" determines the strength of the noise to
// be removed. A value of about 2.5 can be used for moderate denoising,
// where a value of 5.0 can be used for a high level of denoising.
float avm_noise_psd_get_default_value(int block_size, float factor);

// Computes normalized cross correlation of two vectors a and b of length n.
double avm_normalized_cross_correlation(const double *a, const double *b,
                                        int n);

// Validates the correlated noise in the data buffer of size (w, h).
int avm_noise_data_validate(const double *data, int w, int h);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // AVM_AVM_DSP_NOISE_UTIL_H_
