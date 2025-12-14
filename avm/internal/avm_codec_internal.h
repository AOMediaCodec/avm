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

/*!\file
 * \brief Describes the decoder algorithm interface for algorithm
 *        implementations.
 *
 * This file defines the private structures and data types that are only
 * relevant to implementing an algorithm, as opposed to using it.
 *
 * To create a decoder algorithm class, an interface structure is put
 * into the global namespace:
 *     <pre>
 *     my_codec.c:
 *       avm_codec_iface_t my_codec = {
 *           "My Codec v1.0",
 *           AVM_CODEC_ALG_ABI_VERSION,
 *           ...
 *       };
 *     </pre>
 *
 * An application instantiates a specific decoder instance by using
 * avm_codec_dec_init() and a pointer to the algorithm's interface structure:
 *     <pre>
 *     my_app.c:
 *       extern avm_codec_iface_t my_codec;
 *       {
 *           avm_codec_ctx_t algo;
 *           int threads = 4;
 *           avm_codec_dec_cfg_t cfg = { threads, 0, 0 };
 *           res = avm_codec_dec_init(&algo, &my_codec, &cfg, 0);
 *       }
 *     </pre>
 *
 * Once initialized, the instance is managed using other functions from
 * the avm_codec_* family.
 */
#ifndef AVM_AVM_INTERNAL_AVM_CODEC_INTERNAL_H_
#define AVM_AVM_INTERNAL_AVM_CODEC_INTERNAL_H_
#include "../avm_decoder.h"
#include "../avm_encoder.h"
#include "common/args_helper.h"
#include "av2/common/enums.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!\brief Current ABI version number
 *
 * \internal
 * If this file is altered in any way that changes the ABI, this value
 * must be bumped.  Examples include, but are not limited to, changing
 * types, removing or reassigning enums, adding/removing/rearranging
 * fields to structures
 */
#define AVM_CODEC_INTERNAL_ABI_VERSION (7) /**<\hideinitializer*/

typedef struct avm_codec_alg_priv avm_codec_alg_priv_t;

/*!\brief init function pointer prototype
 *
 * Performs algorithm-specific initialization of the decoder context. This
 * function is called by avm_codec_dec_init() and avm_codec_enc_init(), so
 * plugins implementing this interface may trust the input parameters to be
 * properly initialized.
 *
 * \param[in] ctx   Pointer to this instance's context
 * \retval #AVM_CODEC_OK
 *     The input stream was recognized and decoder initialized.
 * \retval #AVM_CODEC_MEM_ERROR
 *     Memory operation failed.
 */
typedef avm_codec_err_t (*avm_codec_init_fn_t)(avm_codec_ctx_t *ctx);

/*!\brief destroy function pointer prototype
 *
 * Performs algorithm-specific destruction of the decoder context. This
 * function is called by the generic avm_codec_destroy() wrapper function,
 * so plugins implementing this interface may trust the input parameters
 * to be properly initialized.
 *
 * \param[in] ctx   Pointer to this instance's context
 * \retval #AVM_CODEC_OK
 *     The input stream was recognized and decoder initialized.
 * \retval #AVM_CODEC_MEM_ERROR
 *     Memory operation failed.
 */
typedef avm_codec_err_t (*avm_codec_destroy_fn_t)(avm_codec_alg_priv_t *ctx);

/*!\brief parse stream info function pointer prototype
 *
 * Performs high level parsing of the bitstream. This function is called by the
 * generic avm_codec_peek_stream_info() wrapper function, so plugins
 * implementing this interface may trust the input parameters to be properly
 * initialized.
 *
 * \param[in]      data    Pointer to a block of data to parse
 * \param[in]      data_sz Size of the data buffer
 * \param[out]     si      Pointer to stream info to update.
 *
 * \retval #AVM_CODEC_OK
 *     Bitstream is parsable and stream information updated
 */
typedef avm_codec_err_t (*avm_codec_peek_si_fn_t)(const uint8_t *data,
                                                  size_t data_sz,
                                                  avm_codec_stream_info_t *si);

/*!\brief Return information about the current stream.
 *
 * Returns information about the stream that has been parsed during decoding.
 *
 * \param[in]      ctx     Pointer to this instance's context
 * \param[in,out]  si      Pointer to stream info to update
 *
 * \retval #AVM_CODEC_OK
 *     Bitstream is parsable and stream information updated
 */
typedef avm_codec_err_t (*avm_codec_get_si_fn_t)(avm_codec_alg_priv_t *ctx,
                                                 avm_codec_stream_info_t *si);

/*!\brief control function pointer prototype
 *
 * This function is used to exchange algorithm specific data with the decoder
 * instance. This can be used to implement features specific to a particular
 * algorithm.
 *
 * This function is called by the generic avm_codec_control() wrapper
 * function, so plugins implementing this interface may trust the input
 * parameters to be properly initialized. However,  this interface does not
 * provide type safety for the exchanged data or assign meanings to the
 * control IDs. Those details should be specified in the algorithm's
 * header file. In particular, the ctrl_id parameter is guaranteed to exist
 * in the algorithm's control mapping table, and the data parameter may be NULL.
 *
 *
 * \param[in]     ctx              Pointer to this instance's context
 * \param[in]     ctrl_id          Algorithm specific control identifier
 * \param[in,out] data             Data to exchange with algorithm instance.
 *
 * \retval #AVM_CODEC_OK
 *     The internal state data was deserialized.
 */
typedef avm_codec_err_t (*avm_codec_control_fn_t)(avm_codec_alg_priv_t *ctx,
                                                  va_list ap);

/*!\brief codec option setter function pointer prototype
 * This function is used to set a codec option using a key (option name) & value
 * pair.
 *
 * \param[in]     ctx              Pointer to this instance's context
 * \param[in]     name             A string of the option's name (key)
 * \param[in]     value            A string of the value to be set to
 *
 * \retval #AVM_CODEC_OK
 *     The option is successfully set to the value
 * \retval #AVM_CODEC_INVALID_PARAM
 *     The data was not valid.
 */
typedef avm_codec_err_t (*avm_codec_set_option_fn_t)(avm_codec_alg_priv_t *ctx,
                                                     const char *name,
                                                     const char *value);

/*!\brief control function pointer mapping
 *
 * This structure stores the mapping between control identifiers and
 * implementing functions. Each algorithm provides a list of these
 * mappings. This list is searched by the avm_codec_control()
 * function to determine which function to invoke. The special
 * value defined by CTRL_MAP_END is used to indicate end-of-list, and must be
 * present. It can be tested with the at_ctrl_map_end function. Note that
 * ctrl_id values \ref MUST be non-zero.
 */
typedef const struct avm_codec_ctrl_fn_map {
  int ctrl_id;
  avm_codec_control_fn_t fn;
} avm_codec_ctrl_fn_map_t;

#define CTRL_MAP_END { 0, NULL }

static AVM_INLINE int at_ctrl_map_end(avm_codec_ctrl_fn_map_t *e) {
  return e->ctrl_id == 0 && e->fn == NULL;
}

/*!\brief decode data function pointer prototype
 *
 * Processes a buffer of coded data. This function is called by the generic
 * avm_codec_decode() wrapper function, so plugins implementing this interface
 * may trust the input parameters to be properly initialized.
 *
 * \param[in] ctx          Pointer to this instance's context
 * \param[in] data         Pointer to this block of new coded data.
 * \param[in] data_sz      Size of the coded data, in bytes.
 *
 * \return Returns #AVM_CODEC_OK if the coded data was processed completely
 *         and future pictures can be decoded without error. Otherwise,
 *         see the descriptions of the other error codes in ::avm_codec_err_t
 *         for recoverability capabilities.
 */
typedef avm_codec_err_t (*avm_codec_decode_fn_t)(avm_codec_alg_priv_t *ctx,
                                                 const uint8_t *data,
                                                 size_t data_sz,
                                                 void *user_priv);

/*!\brief Decoded frames iterator
 *
 * Iterates over a list of the frames available for display. The iterator
 * storage should be initialized to NULL to start the iteration. Iteration is
 * complete when this function returns NULL.
 *
 * The list of available frames becomes valid upon completion of the
 * avm_codec_decode call, and remains valid until the next call to
 * avm_codec_decode.
 *
 * \param[in]     ctx      Pointer to this instance's context
 * \param[in out] iter     Iterator storage, initialized to NULL
 *
 * \return Returns a pointer to an image, if one is ready for display. Frames
 *         produced will always be in PTS (presentation time stamp) order.
 */
typedef avm_image_t *(*avm_codec_get_frame_fn_t)(avm_codec_alg_priv_t *ctx,
                                                 avm_codec_iter_t *iter);

/*!\brief Decoded frames peek
 *
 * Peeks at the next frame available for display given by the iterator index
 * over a list of the frames available for display. The iterator is not
 * incremented after the operation.
 *
 * The list of available frames becomes valid upon completion of the
 * avm_codec_decode call, and remains valid until the next call to
 * avm_codec_decode.
 *
 * \param[in]     ctx      Pointer to this instance's context
 * \param[in out] iter     Iterator storage, initialized to NULL
 *
 * \return Returns a pointer to an image, if one is ready for display. Frames
 *         produced will always be in PTS (presentation time stamp) order.
 */
typedef avm_image_t *(*avm_codec_peek_frame_fn_t)(avm_codec_alg_priv_t *ctx,
                                                  avm_codec_iter_t *iter);

/*!\brief Pass in external frame buffers for the decoder to use.
 *
 * Registers functions to be called when libavm needs a frame buffer
 * to decode the current frame and a function to be called when libavm does
 * not internally reference the frame buffer. This set function must
 * be called before the first call to decode or libavm will assume the
 * default behavior of allocating frame buffers internally.
 *
 * \param[in] ctx          Pointer to this instance's context
 * \param[in] cb_get       Pointer to the get callback function
 * \param[in] cb_release   Pointer to the release callback function
 * \param[in] cb_priv      Callback's private data
 *
 * \retval #AVM_CODEC_OK
 *     External frame buffers will be used by libavm.
 * \retval #AVM_CODEC_INVALID_PARAM
 *     One or more of the callbacks were NULL.
 * \retval #AVM_CODEC_ERROR
 *     Decoder context not initialized, or algorithm not capable of
 *     using external frame buffers.
 *
 * \note
 * When decoding AV2, the application may be required to pass in at least
 * #AVM_MAXIMUM_WORK_BUFFERS external frame
 * buffers.
 */
typedef avm_codec_err_t (*avm_codec_set_fb_fn_t)(
    avm_codec_alg_priv_t *ctx, avm_get_frame_buffer_cb_fn_t cb_get,
    avm_release_frame_buffer_cb_fn_t cb_release, void *cb_priv);

typedef avm_codec_err_t (*avm_codec_encode_fn_t)(avm_codec_alg_priv_t *ctx,
                                                 const avm_image_t *img,
                                                 avm_codec_pts_t pts,
                                                 unsigned long duration,
                                                 avm_enc_frame_flags_t flags);
typedef const avm_codec_cx_pkt_t *(*avm_codec_get_cx_data_fn_t)(
    avm_codec_alg_priv_t *ctx, avm_codec_iter_t *iter);

typedef avm_codec_err_t (*avm_codec_enc_config_set_fn_t)(
    avm_codec_alg_priv_t *ctx, const avm_codec_enc_cfg_t *cfg);
typedef avm_fixed_buf_t *(*avm_codec_get_global_headers_fn_t)(
    avm_codec_alg_priv_t *ctx);

typedef avm_image_t *(*avm_codec_get_preview_frame_fn_t)(
    avm_codec_alg_priv_t *ctx);

/*!\brief Decoder algorithm interface interface
 *
 * All decoders \ref MUST expose a variable of this type.
 */
struct avm_codec_iface {
  const char *name;                   /**< Identification String  */
  int abi_version;                    /**< Implemented ABI version */
  avm_codec_caps_t caps;              /**< Decoder capabilities */
  avm_codec_init_fn_t init;           /**< \copydoc ::avm_codec_init_fn_t */
  avm_codec_destroy_fn_t destroy;     /**< \copydoc ::avm_codec_destroy_fn_t */
  avm_codec_ctrl_fn_map_t *ctrl_maps; /**< \copydoc ::avm_codec_ctrl_fn_map_t */
  struct avm_codec_dec_iface {
    avm_codec_peek_si_fn_t peek_si; /**< \copydoc ::avm_codec_peek_si_fn_t */
    avm_codec_get_si_fn_t get_si;   /**< \copydoc ::avm_codec_get_si_fn_t */
    avm_codec_decode_fn_t decode;   /**< \copydoc ::avm_codec_decode_fn_t */
    avm_codec_get_frame_fn_t
        get_frame; /**< \copydoc ::avm_codec_get_frame_fn_t */
    avm_codec_peek_frame_fn_t
        peek_frame; /**< \copydoc ::avm_codec_peek_frame_fn_t */
    avm_codec_set_fb_fn_t set_fb_fn; /**< \copydoc ::avm_codec_set_fb_fn_t */
  } dec;
  struct avm_codec_enc_iface {
    int cfg_count;
    const avm_codec_enc_cfg_t *cfgs; /**< \copydoc ::avm_codec_enc_cfg_t */
    avm_codec_encode_fn_t encode;    /**< \copydoc ::avm_codec_encode_fn_t */
    avm_codec_get_cx_data_fn_t
        get_cx_data; /**< \copydoc ::avm_codec_get_cx_data_fn_t */
    avm_codec_enc_config_set_fn_t
        cfg_set; /**< \copydoc ::avm_codec_enc_config_set_fn_t */
    avm_codec_get_global_headers_fn_t
        get_glob_hdrs; /**< \copydoc ::avm_codec_get_global_headers_fn_t */
    avm_codec_get_preview_frame_fn_t
        get_preview; /**< \copydoc ::avm_codec_get_preview_frame_fn_t */
  } enc;
  avm_codec_set_option_fn_t set_option;
};

/*!\brief Instance private storage
 *
 * This structure is allocated by the algorithm's init function. It can be
 * extended in one of two ways. First, a second, algorithm specific structure
 * can be allocated and the priv member pointed to it. Alternatively, this
 * structure can be made the first member of the algorithm specific structure,
 * and the pointer cast to the proper type.
 */
struct avm_codec_priv {
  const char *err_detail;
  avm_codec_flags_t init_flags;
  IbpWeightsType ibp_directional_weights[IBP_WEIGHT_SIZE][IBP_WEIGHT_SIZE]
                                        [DIR_MODES_0_90];
  struct {
    avm_fixed_buf_t cx_data_dst_buf;
    unsigned int cx_data_pad_before;
    unsigned int cx_data_pad_after;
    avm_codec_cx_pkt_t cx_data_pkt;
  } enc;
};

#define CAST(id, arg) va_arg((arg), avm_codec_control_type_##id)

/* Internal Utility Functions
 *
 * The following functions are intended to be used inside algorithms as
 * utilities for manipulating avm_codec_* data structures.
 */
struct avm_codec_pkt_list {
  unsigned int cnt;
  unsigned int max;
  struct avm_codec_cx_pkt pkts[1];
};

#define avm_codec_pkt_list_decl(n)     \
  union {                              \
    struct avm_codec_pkt_list head;    \
    struct {                           \
      struct avm_codec_pkt_list head;  \
      struct avm_codec_cx_pkt pkts[n]; \
    } alloc;                           \
  }

#define avm_codec_pkt_list_init(m) \
  (m)->alloc.head.cnt = 0,         \
  (m)->alloc.head.max = sizeof((m)->alloc.pkts) / sizeof((m)->alloc.pkts[0])

int avm_codec_pkt_list_add(struct avm_codec_pkt_list *,
                           const struct avm_codec_cx_pkt *);

const avm_codec_cx_pkt_t *avm_codec_pkt_list_get(
    struct avm_codec_pkt_list *list, avm_codec_iter_t *iter);

#include <stdio.h>
#include <setjmp.h>

struct avm_internal_error_info {
  avm_codec_err_t error_code;
  int has_detail;
  char detail[ARG_ERR_MSG_MAX_LEN];
  int setjmp;  // Boolean: whether 'jmp' is valid.
  jmp_buf jmp;
};

#define CLANG_ANALYZER_NORETURN
#if defined(__has_feature)
#if __has_feature(attribute_analyzer_noreturn)
#undef CLANG_ANALYZER_NORETURN
#define CLANG_ANALYZER_NORETURN __attribute__((analyzer_noreturn))
#endif
#endif

void avm_internal_error(struct avm_internal_error_info *info,
                        avm_codec_err_t error, const char *fmt,
                        ...) CLANG_ANALYZER_NORETURN;

void avm_merge_corrupted_flag(int *corrupted, int value);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_INTERNAL_AVM_CODEC_INTERNAL_H_
