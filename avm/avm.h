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

/*!\defgroup avm AVM
 * \ingroup codecs
 * AVM is avm's newest video compression algorithm that uses motion
 * compensated prediction, Discrete Cosine Transform (DCT) coding of the
 * prediction error signal and context dependent entropy coding techniques
 * based on arithmetic principles. It features:
 *  - YUV 4:2:0 image format
 *  - Macro-block based coding (16x16 luma plus two 8x8 chroma)
 *  - 1/4 (1/8) pixel accuracy motion compensated prediction
 *  - 4x4 DCT transform
 *  - 128 level linear quantizer
 *  - In loop deblocking filter
 *  - Context-based entropy coding
 *
 * @{
 */
/*!\file
 * \brief Provides controls common to both the AVM encoder and decoder.
 */
#ifndef AVM_AVM_AVM_H_
#define AVM_AVM_AVM_H_

#include "avm/avm_codec.h"
#include "avm/avm_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!\brief Control functions
 *
 * The set of macros define the control functions of AVM interface
 */
enum avm_com_control_id {
  /* TODO(https://crbug.com/aomedia/2671): The encoder overlaps the range of
   * these values for its control ids, see the NOTEs in avm/avmcx.h. These
   * should be migrated to something like the AVM_DECODER_CTRL_ID_START range
   * next time we're ready to break the ABI.
   */
  AV2_GET_REFERENCE = 128,  /**< get a pointer to a reference frame,
                               av2_ref_frame_t* parameter */
  AV2_SET_REFERENCE = 129,  /**< write a frame into a reference buffer,
                               av2_ref_frame_t* parameter */
  AV2_COPY_REFERENCE = 130, /**< get a copy of reference frame from the decoderm
                               av2_ref_frame_t* parameter */
  AVM_COMMON_CTRL_ID_MAX,

  AV2_GET_NEW_FRAME_IMAGE =
      192, /**< get a pointer to the new frame, avm_image_t* parameter */
  AV2_COPY_NEW_FRAME_IMAGE = 193, /**< copy the new frame to an external buffer,
                                     avm_image_t* parameter */

  AVM_DECODER_CTRL_ID_START = 256
};

/*!\brief AV2 specific reference frame data struct
 *
 * Define the data struct to access av2 reference frames.
 */
typedef struct av2_ref_frame {
  int idx;              /**< frame index to get (input) */
  int use_external_ref; /**< Directly use external ref buffer(decoder only) */
  avm_image_t img;      /**< img structure to populate (output) */
} av2_ref_frame_t;

/*!\cond */
/*!\brief avm decoder control function parameter type
 *
 * Defines the data type for each of AVM decoder control function requires.
 *
 * \note For each control ID "X", a macro-define of
 * AVM_CTRL_X is provided. It is used at compile time to determine
 * if the control ID is supported by the libavm library available,
 * when the libavm version cannot be controlled.
 */
AVM_CTRL_USE_TYPE(AV2_GET_REFERENCE, av2_ref_frame_t *)
#define AVM_CTRL_AV2_GET_REFERENCE

AVM_CTRL_USE_TYPE(AV2_SET_REFERENCE, av2_ref_frame_t *)
#define AVM_CTRL_AV2_SET_REFERENCE

AVM_CTRL_USE_TYPE(AV2_COPY_REFERENCE, av2_ref_frame_t *)
#define AVM_CTRL_AV2_COPY_REFERENCE

AVM_CTRL_USE_TYPE(AV2_GET_NEW_FRAME_IMAGE, avm_image_t *)
#define AVM_CTRL_AV2_GET_NEW_FRAME_IMAGE

AVM_CTRL_USE_TYPE(AV2_COPY_NEW_FRAME_IMAGE, avm_image_t *)
#define AVM_CTRL_AV2_COPY_NEW_FRAME_IMAGE

/*!\endcond */
/*! @} - end defgroup avm */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_AVM_H_
