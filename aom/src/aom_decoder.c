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
 * \brief Provides the high level interface to wrap decoder algorithms.
 *
 */
#include <string.h>
#include "aom/internal/aom_codec_internal.h"

#define SAVE_STATUS(ctx, var) (ctx ? (ctx->err = var) : var)

static aom_codec_alg_priv_t *get_alg_priv(aom_codec_ctx_t *ctx) {
  return (aom_codec_alg_priv_t *)ctx->priv;
}

aom_codec_err_t aom_codec_dec_init_ver(aom_codec_ctx_t *ctx,
                                       aom_codec_iface_t *iface,
                                       const aom_codec_dec_cfg_t *cfg,
                                       aom_codec_flags_t flags, int ver) {
  aom_codec_err_t res;

  if (ver != AOM_DECODER_ABI_VERSION)
    res = AOM_CODEC_ABI_MISMATCH;
  else if (!ctx || !iface)
    res = AOM_CODEC_INVALID_PARAM;
  else if (iface->abi_version != AOM_CODEC_INTERNAL_ABI_VERSION)
    res = AOM_CODEC_ABI_MISMATCH;
  else if (!(iface->caps & AOM_CODEC_CAP_DECODER))
    res = AOM_CODEC_INCAPABLE;
  else {
#if CONFIG_F281_OUTPUT
    int single_file = ctx->single_file;
    int use_y4m = ctx->use_y4m ;
    int opt_yv12 = ctx->opt_yv12;
    int opt_i420 = ctx->opt_i420;
    int opt_raw = ctx->opt_raw ;
    int flipuv = ctx->flipuv;
    int fixed_output_bit_depth = ctx->fixed_output_bit_depth ;
    int do_scale = ctx->do_scale;
    int noblit = ctx->noblit;
    int progress = ctx->progress;
    int do_md5 = ctx->do_md5;
    int do_verify = ctx->do_verify;
    FILE* outfile = ctx->outfile;
    MD5Context md5_ctx = ctx->md5_ctx;
    int aom_input_ctx_width = ctx->aom_input_ctx_width ;
    int aom_input_ctx_height = ctx->aom_input_ctx_height;
    int aom_input_ctx_framerate_numerator = ctx->aom_input_ctx_framerate_numerator;
    int aom_input_ctx_framerate_denominator = ctx->aom_input_ctx_framerate_denominator;
#endif
    memset(ctx, 0, sizeof(*ctx));
    ctx->iface = iface;
    ctx->name = iface->name;
    ctx->priv = NULL;
    ctx->init_flags = flags;
    ctx->config.dec = cfg;
#if CONFIG_F281_OUTPUT
    ctx->single_file = single_file;
    ctx->use_y4m = use_y4m;
    ctx->opt_yv12= opt_yv12;
    ctx->opt_i420= opt_i420;
    ctx->opt_raw = opt_raw;
    ctx->flipuv = flipuv;
    ctx->fixed_output_bit_depth = fixed_output_bit_depth;
    ctx->do_scale = do_scale;
    ctx->noblit = noblit;
    ctx->progress = progress;
    ctx->do_md5 = do_md5;
    ctx->do_verify = do_verify;
    ctx->outfile = outfile;
    ctx->md5_ctx = md5_ctx;
    ctx->aom_input_ctx_width = aom_input_ctx_width;
    ctx->aom_input_ctx_height = aom_input_ctx_height;
    ctx->aom_input_ctx_framerate_numerator = aom_input_ctx_framerate_numerator;
    ctx->aom_input_ctx_framerate_denominator = aom_input_ctx_framerate_denominator;
#endif
    res = ctx->iface->init(ctx);
    if (res) {
      ctx->err_detail = ctx->priv ? ctx->priv->err_detail : NULL;
      aom_codec_destroy(ctx);
    }
  }

  return SAVE_STATUS(ctx, res);
}

aom_codec_err_t aom_codec_peek_stream_info(aom_codec_iface_t *iface,
                                           const uint8_t *data, size_t data_sz,
                                           aom_codec_stream_info_t *si) {
  aom_codec_err_t res;

  if (!iface || !data || !data_sz || !si) {
    res = AOM_CODEC_INVALID_PARAM;
  } else {
    /* Set default/unknown values */
    si->w = 0;
    si->h = 0;

    res = iface->dec.peek_si(data, data_sz, si);
  }

  return res;
}

aom_codec_err_t aom_codec_get_stream_info(aom_codec_ctx_t *ctx,
                                          aom_codec_stream_info_t *si) {
  aom_codec_err_t res;

  if (!ctx || !si) {
    res = AOM_CODEC_INVALID_PARAM;
  } else if (!ctx->iface || !ctx->priv) {
    res = AOM_CODEC_ERROR;
  } else {
    /* Set default/unknown values */
    si->w = 0;
    si->h = 0;

    res = ctx->iface->dec.get_si(get_alg_priv(ctx), si);
  }

  return SAVE_STATUS(ctx, res);
}

aom_codec_err_t aom_codec_decode(aom_codec_ctx_t *ctx, const uint8_t *data,
                                 size_t data_sz,
#if CONFIG_F281_OUTPUT
                                 int is_test_decoder,
#endif
                                 void *user_priv) {
  aom_codec_err_t res;

  if (!ctx)
    res = AOM_CODEC_INVALID_PARAM;
  else if (!ctx->iface || !ctx->priv)
    res = AOM_CODEC_ERROR;
  else {
    res = ctx->iface->dec.decode(get_alg_priv(ctx), data, data_sz,
#if CONFIG_F281_OUTPUT
                                 is_test_decoder,
#endif
                                 user_priv);
  }

  return SAVE_STATUS(ctx, res);
}

aom_image_t *aom_codec_get_frame(aom_codec_ctx_t *ctx, aom_codec_iter_t *iter) {
  aom_image_t *img;

  if (!ctx || !iter || !ctx->iface || !ctx->priv)
    img = NULL;
  else
    img = ctx->iface->dec.get_frame(get_alg_priv(ctx), iter);

  return img;
}

aom_image_t *aom_codec_peek_frame(aom_codec_ctx_t *ctx,
                                  aom_codec_iter_t *iter) {
  aom_image_t *img;

  if (!ctx || !iter || !ctx->iface || !ctx->priv)
    img = NULL;
  else
    img = ctx->iface->dec.peek_frame(get_alg_priv(ctx), iter);

  return img;
}

aom_codec_err_t aom_codec_set_frame_buffer_functions(
    aom_codec_ctx_t *ctx, aom_get_frame_buffer_cb_fn_t cb_get,
    aom_release_frame_buffer_cb_fn_t cb_release, void *cb_priv) {
  aom_codec_err_t res;

  if (!ctx || !cb_get || !cb_release) {
    res = AOM_CODEC_INVALID_PARAM;
  } else if (!ctx->iface || !ctx->priv) {
    res = AOM_CODEC_ERROR;
  } else if (!(ctx->iface->caps & AOM_CODEC_CAP_EXTERNAL_FRAME_BUFFER)) {
    res = AOM_CODEC_INCAPABLE;
  } else {
    res = ctx->iface->dec.set_fb_fn(get_alg_priv(ctx), cb_get, cb_release,
                                    cb_priv);
  }

  return SAVE_STATUS(ctx, res);
}
