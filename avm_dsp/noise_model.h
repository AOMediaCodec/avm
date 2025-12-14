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

#ifndef AVM_AVM_DSP_NOISE_MODEL_H_
#define AVM_AVM_DSP_NOISE_MODEL_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#include <stdint.h>
#include "avm_dsp/grain_synthesis.h"
#include "avm_scale/yv12config.h"

/*!\brief Wrapper of data required to represent linear system of eqns and soln.
 */
typedef struct {
  double *A;
  double *b;
  double *x;
  int n;
} avm_equation_system_t;

/*!\brief Representation of a piecewise linear curve
 *
 * Holds n points as (x, y) pairs, that store the curve.
 */
typedef struct {
  double (*points)[2];
  int num_points;
} avm_noise_strength_lut_t;

/*!\brief Init the noise strength lut with the given number of points*/
int avm_noise_strength_lut_init(avm_noise_strength_lut_t *lut, int num_points);

/*!\brief Frees the noise strength lut. */
void avm_noise_strength_lut_free(avm_noise_strength_lut_t *lut);

/*!\brief Evaluate the lut at the point x.
 *
 * \param[in] lut  The lut data.
 * \param[in] x    The coordinate to evaluate the lut.
 */
double avm_noise_strength_lut_eval(const avm_noise_strength_lut_t *lut,
                                   double x);

/*!\brief Helper struct to model noise strength as a function of intensity.
 *
 * Internally, this structure holds a representation of a linear system
 * of equations that models noise strength (standard deviation) as a
 * function of intensity. The mapping is initially stored using a
 * piecewise representation with evenly spaced bins that cover the entire
 * domain from [min_intensity, max_intensity]. Each observation (x,y) gives a
 * constraint of the form:
 *   y_{i} (1 - a) + y_{i+1} a = y
 * where y_{i} is the value of bin i and x_{i} <= x <= x_{i+1} and
 * a = x/(x_{i+1} - x{i}). The equation system holds the corresponding
 * normal equations.
 *
 * As there may be missing data, the solution is regularized to get a
 * complete set of values for the bins. A reduced representation after
 * solving can be obtained by getting the corresponding noise_strength_lut_t.
 */
typedef struct {
  avm_equation_system_t eqns;
  double min_intensity;
  double max_intensity;
  int num_bins;
  int num_equations;
  double total;
} avm_noise_strength_solver_t;

/*!\brief Initializes the noise solver with the given number of bins.
 *
 * Returns 0 if initialization fails.
 *
 * \param[in]  solver    The noise solver to be initialized.
 * \param[in]  num_bins  Number of bins to use in the internal representation.
 * \param[in]  bit_depth The bit depth used to derive {min,max}_intensity.
 */
int avm_noise_strength_solver_init(avm_noise_strength_solver_t *solver,
                                   int num_bins, int bit_depth);
void avm_noise_strength_solver_free(avm_noise_strength_solver_t *solver);

/*!\brief Gets the x coordinate of bin i.
 *
 * \param[in]  i  The bin whose coordinate to query.
 */
double avm_noise_strength_solver_get_center(
    const avm_noise_strength_solver_t *solver, int i);

/*!\brief Add an observation of the block mean intensity to its noise strength.
 *
 * \param[in]  block_mean  The average block intensity,
 * \param[in]  noise_std   The observed noise strength.
 */
void avm_noise_strength_solver_add_measurement(
    avm_noise_strength_solver_t *solver, double block_mean, double noise_std);

/*!\brief Solves the current set of equations for the noise strength. */
int avm_noise_strength_solver_solve(avm_noise_strength_solver_t *solver);

/*!\brief Fits a reduced piecewise linear lut to the internal solution
 *
 * \param[in] max_num_points  The maximum number of output points
 * \param[out] lut  The output piecewise linear lut.
 */
int avm_noise_strength_solver_fit_piecewise(
    const avm_noise_strength_solver_t *solver, int max_num_points,
    avm_noise_strength_lut_t *lut);

/*!\brief Helper for holding precomputed data for finding flat blocks.
 *
 * Internally a block is modeled with a low-order polynomial model. A
 * planar model would be a bunch of equations like:
 * <[y_i x_i 1], [a_1, a_2, a_3]>  = b_i
 * for each point in the block. The system matrix A with row i as [y_i x_i 1]
 * is maintained as is the inverse, inv(A'*A), so that the plane parameters
 * can be fit for each block.
 */
typedef struct {
  double *AtA_inv;
  double *A;
  int num_params;  // The number of parameters used for internal low-order model
  int block_size;  // The block size the finder was initialized with
  double normalization;  // Normalization factor (1 / (2^(bit_depth) - 1))
} avm_flat_block_finder_t;

/*!\brief Init the block_finder with the given block size, bit_depth */
int avm_flat_block_finder_init(avm_flat_block_finder_t *block_finder,
                               int block_size, int bit_depth);
void avm_flat_block_finder_free(avm_flat_block_finder_t *block_finder);

/*!\brief Helper to extract a block and low order "planar" model. */
void avm_flat_block_finder_extract_block(
    const avm_flat_block_finder_t *block_finder, const uint8_t *const data,
    int w, int h, int stride, int offsx, int offsy, double *plane,
    double *block);

/*!\brief Runs the flat block finder on the input data.
 *
 * Find flat blocks in the input image data. Returns a map of
 * flat_blocks, where the value of flat_blocks map will be non-zero
 * when a block is determined to be flat. A higher value indicates a bigger
 * confidence in the decision.
 */
int avm_flat_block_finder_run(const avm_flat_block_finder_t *block_finder,
                              const uint8_t *const data, int w, int h,
                              int stride, uint8_t *flat_blocks);

// The noise shape indicates the allowed coefficients in the AR model.
enum {
  AVM_NOISE_SHAPE_DIAMOND = 0,
  AVM_NOISE_SHAPE_SQUARE = 1
} UENUM1BYTE(avm_noise_shape);

// The parameters of the noise model include the shape type, lag, the
// bit depth of the input images provided, and whether the input images
// will be using uint16 (or uint8) representation.
typedef struct {
  avm_noise_shape shape;
  int lag;
  int bit_depth;
} avm_noise_model_params_t;

/*!\brief State of a noise model estimate for a single channel.
 *
 * This contains a system of equations that can be used to solve
 * for the auto-regressive coefficients as well as a noise strength
 * solver that can be used to model noise strength as a function of
 * intensity.
 */
typedef struct {
  avm_equation_system_t eqns;
  avm_noise_strength_solver_t strength_solver;
  int num_observations;  // The number of observations in the eqn system
  double ar_gain;        // The gain of the current AR filter
} avm_noise_state_t;

/*!\brief Complete model of noise for a planar video
 *
 * This includes a noise model for the latest frame and an aggregated
 * estimate over all previous frames that had similar parameters.
 */
typedef struct {
  avm_noise_model_params_t params;
  avm_noise_state_t combined_state[3];  // Combined state per channel
  avm_noise_state_t latest_state[3];    // Latest state per channel
  int (*coords)[2];  // Offsets (x,y) of the coefficient samples
  int n;             // Number of parameters (size of coords)
  int bit_depth;
} avm_noise_model_t;

/*!\brief Result of a noise model update. */
enum {
  AVM_NOISE_STATUS_OK = 0,
  AVM_NOISE_STATUS_INVALID_ARGUMENT,
  AVM_NOISE_STATUS_INSUFFICIENT_FLAT_BLOCKS,
  AVM_NOISE_STATUS_DIFFERENT_NOISE_TYPE,
  AVM_NOISE_STATUS_INTERNAL_ERROR,
} UENUM1BYTE(avm_noise_status_t);

/*!\brief Initializes a noise model with the given parameters.
 *
 * Returns 0 on failure.
 */
int avm_noise_model_init(avm_noise_model_t *model,
                         const avm_noise_model_params_t params);
void avm_noise_model_free(avm_noise_model_t *model);

/*!\brief Updates the noise model with a new frame observation.
 *
 * Updates the noise model with measurements from the given input frame and a
 * denoised variant of it. Noise is sampled from flat blocks using the flat
 * block map.
 *
 * Returns a noise_status indicating if the update was successful. If the
 * Update was successful, the combined_state is updated with measurements from
 * the provided frame. If status is OK or DIFFERENT_NOISE_TYPE, the latest noise
 * state will be updated with measurements from the provided frame.
 *
 * \param[in,out] noise_model     The noise model to be updated
 * \param[in]     data            Raw frame data
 * \param[in]     denoised        Denoised frame data.
 * \param[in]     w               Frame width
 * \param[in]     h               Frame height
 * \param[in]     strides         Stride of the planes
 * \param[in]     chroma_sub_log2 Chroma subsampling for planes != 0.
 * \param[in]     flat_blocks     A map to blocks that have been determined flat
 * \param[in]     block_size      The size of blocks.
 */
avm_noise_status_t avm_noise_model_update(
    avm_noise_model_t *const noise_model, const uint8_t *const data[3],
    const uint8_t *const denoised[3], int w, int h, int strides[3],
    int chroma_sub_log2[2], const uint8_t *const flat_blocks, int block_size);

/*\brief Save the "latest" estimate into the "combined" estimate.
 *
 * This is meant to be called when the noise modeling detected a change
 * in parameters (or for example, if a user wanted to reset estimation at
 * a shot boundary).
 */
void avm_noise_model_save_latest(avm_noise_model_t *noise_model);

/*!\brief Converts the noise_model parameters to the corresponding
 *    grain_parameters.
 *
 * The noise structs in this file are suitable for estimation (e.g., using
 * floats), but the grain parameters in the bitstream are quantized. This
 * function does the conversion by selecting the correct quantization levels.
 */
int avm_noise_model_get_grain_parameters(avm_noise_model_t *const noise_model,
                                         avm_film_grain_t *film_grain);

/*!\brief Perform a Wiener filter denoising in 2D using the provided noise psd.
 *
 * \param[in]     data            Raw frame data
 * \param[out]    denoised        Denoised frame data
 * \param[in]     w               Frame width
 * \param[in]     h               Frame height
 * \param[in]     stride          Stride of the planes
 * \param[in]     chroma_sub_log2 Chroma subsampling for planes != 0.
 * \param[in]     noise_psd       The power spectral density of the noise
 * \param[in]     block_size      The size of blocks
 * \param[in]     bit_depth       Bit depth of the image
 */
int avm_wiener_denoise_2d(const uint8_t *const data[3], uint8_t *denoised[3],
                          int w, int h, int stride[3], int chroma_sub_log2[2],
                          float *noise_psd[3], int block_size, int bit_depth);

struct avm_denoise_and_model_t;

/*!\brief Denoise the buffer and model the residual noise.
 *
 * This is meant to be called sequentially on input frames. The input buffer
 * is denoised and the residual noise is modelled. The current noise estimate
 * is populated in film_grain. Returns true on success. The grain.apply_grain
 * parameter will be true when the input buffer was successfully denoised and
 * grain was modelled. Returns false on error.
 *
 * \param[in]      ctx   Struct allocated with avm_denoise_and_model_alloc
 *                       that holds some buffers for denoising and the current
 *                       noise estimate.
 * \param[in/out]   buf  The raw input buffer to be denoised.
 * \param[out]    grain  Output film grain parameters
 */
int avm_denoise_and_model_run(struct avm_denoise_and_model_t *ctx,
                              YV12_BUFFER_CONFIG *buf, avm_film_grain_t *grain);

/*!\brief Allocates a context that can be used for denoising and noise modeling.
 *
 * \param[in]  bit_depth   Bit depth of buffers this will be run on.
 * \param[in]  block_size  Block size for noise modeling and flat block
 *                         estimation
 * \param[in]  noise_level The noise_level (2.5 for moderate noise, and 5 for
 *                         higher levels of noise)
 */
struct avm_denoise_and_model_t *avm_denoise_and_model_alloc(int bit_depth,
                                                            int block_size,
                                                            float noise_level);

/*!\brief Frees the denoise context allocated with avm_denoise_and_model_alloc
 */
void avm_denoise_and_model_free(struct avm_denoise_and_model_t *denoise_model);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus
#endif  // AVM_AVM_DSP_NOISE_MODEL_H_
