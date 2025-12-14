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
#include "avm/internal/avm_codec_internal.h"

#define SAVE_STATUS(ctx, var) (ctx ? (ctx->err = var) : var)

static avm_codec_alg_priv_t *get_alg_priv(avm_codec_ctx_t *ctx) {
  return (avm_codec_alg_priv_t *)ctx->priv;
}

avm_codec_err_t avm_codec_dec_init_ver(avm_codec_ctx_t *ctx,
                                       avm_codec_iface_t *iface,
                                       const avm_codec_dec_cfg_t *cfg,
                                       avm_codec_flags_t flags, int ver) {
  avm_codec_err_t res;

  if (ver != AVM_DECODER_ABI_VERSION)
    res = AVM_CODEC_ABI_MISMATCH;
  else if (!ctx || !iface)
    res = AVM_CODEC_INVALID_PARAM;
  else if (iface->abi_version != AVM_CODEC_INTERNAL_ABI_VERSION)
    res = AVM_CODEC_ABI_MISMATCH;
  else if (!(iface->caps & AVM_CODEC_CAP_DECODER))
    res = AVM_CODEC_INCAPABLE;
  else {
    memset(ctx, 0, sizeof(*ctx));
    ctx->iface = iface;
    ctx->name = iface->name;
    ctx->priv = NULL;
    ctx->init_flags = flags;
    ctx->config.dec = cfg;

    res = ctx->iface->init(ctx);
    if (res) {
      ctx->err_detail = ctx->priv ? ctx->priv->err_detail : NULL;
      avm_codec_destroy(ctx);
    }
  }

  return SAVE_STATUS(ctx, res);
}

avm_codec_err_t avm_codec_peek_stream_info(avm_codec_iface_t *iface,
                                           const uint8_t *data, size_t data_sz,
                                           avm_codec_stream_info_t *si) {
  avm_codec_err_t res;

  if (!iface || !data || !data_sz || !si) {
    res = AVM_CODEC_INVALID_PARAM;
  } else {
    /* Set default/unknown values */
    si->w = 0;
    si->h = 0;

    res = iface->dec.peek_si(data, data_sz, si);
  }

  return res;
}

avm_codec_err_t avm_codec_get_stream_info(avm_codec_ctx_t *ctx,
                                          avm_codec_stream_info_t *si) {
  avm_codec_err_t res;

  if (!ctx || !si) {
    res = AVM_CODEC_INVALID_PARAM;
  } else if (!ctx->iface || !ctx->priv) {
    res = AVM_CODEC_ERROR;
  } else {
    /* Set default/unknown values */
    si->w = 0;
    si->h = 0;

    res = ctx->iface->dec.get_si(get_alg_priv(ctx), si);
  }

  return SAVE_STATUS(ctx, res);
}

avm_codec_err_t avm_codec_decode(avm_codec_ctx_t *ctx, const uint8_t *data,
                                 size_t data_sz, void *user_priv) {
  avm_codec_err_t res;

  if (!ctx)
    res = AVM_CODEC_INVALID_PARAM;
  else if (!ctx->iface || !ctx->priv)
    res = AVM_CODEC_ERROR;
  else {
    res = ctx->iface->dec.decode(get_alg_priv(ctx), data, data_sz, user_priv);
  }

  return SAVE_STATUS(ctx, res);
}

avm_image_t *avm_codec_get_frame(avm_codec_ctx_t *ctx, avm_codec_iter_t *iter) {
  avm_image_t *img;

  if (!ctx || !iter || !ctx->iface || !ctx->priv)
    img = NULL;
  else
    img = ctx->iface->dec.get_frame(get_alg_priv(ctx), iter);

  return img;
}

avm_image_t *avm_codec_peek_frame(avm_codec_ctx_t *ctx,
                                  avm_codec_iter_t *iter) {
  avm_image_t *img;

  if (!ctx || !iter || !ctx->iface || !ctx->priv)
    img = NULL;
  else
    img = ctx->iface->dec.peek_frame(get_alg_priv(ctx), iter);

  return img;
}

avm_codec_err_t avm_codec_set_frame_buffer_functions(
    avm_codec_ctx_t *ctx, avm_get_frame_buffer_cb_fn_t cb_get,
    avm_release_frame_buffer_cb_fn_t cb_release, void *cb_priv) {
  avm_codec_err_t res;

  if (!ctx || !cb_get || !cb_release) {
    res = AVM_CODEC_INVALID_PARAM;
  } else if (!ctx->iface || !ctx->priv) {
    res = AVM_CODEC_ERROR;
  } else if (!(ctx->iface->caps & AVM_CODEC_CAP_EXTERNAL_FRAME_BUFFER)) {
    res = AVM_CODEC_INCAPABLE;
  } else {
    res = ctx->iface->dec.set_fb_fn(get_alg_priv(ctx), cb_get, cb_release,
                                    cb_priv);
  }

  return SAVE_STATUS(ctx, res);
}
