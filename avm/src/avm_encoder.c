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
 * \brief Provides the high level interface to wrap encoder algorithms.
 *
 */
#include "config/avm_config.h"

#if HAVE_FEXCEPT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fenv.h>
#endif

#include <limits.h>
#include <string.h>

#include "avm/avm_encoder.h"
#include "avm/internal/avm_codec_internal.h"

#define SAVE_STATUS(ctx, var) (ctx ? (ctx->err = var) : var)

static avm_codec_alg_priv_t *get_alg_priv(avm_codec_ctx_t *ctx) {
  return (avm_codec_alg_priv_t *)ctx->priv;
}

avm_codec_err_t avm_codec_enc_init_ver(avm_codec_ctx_t *ctx,
                                       avm_codec_iface_t *iface,
                                       const avm_codec_enc_cfg_t *cfg,
                                       avm_codec_flags_t flags, int ver) {
  avm_codec_err_t res;

  if (ver != AVM_ENCODER_ABI_VERSION)
    res = AVM_CODEC_ABI_MISMATCH;
  else if (!ctx || !iface || !cfg)
    res = AVM_CODEC_INVALID_PARAM;
  else if (iface->abi_version != AVM_CODEC_INTERNAL_ABI_VERSION)
    res = AVM_CODEC_ABI_MISMATCH;
  else if (!(iface->caps & AVM_CODEC_CAP_ENCODER))
    res = AVM_CODEC_INCAPABLE;
  else if ((flags & AVM_CODEC_USE_PSNR) && !(iface->caps & AVM_CODEC_CAP_PSNR))
    res = AVM_CODEC_INCAPABLE;
  else {
    ctx->iface = iface;
    ctx->name = iface->name;
    ctx->priv = NULL;
    ctx->init_flags = flags;
    ctx->config.enc = cfg;
    res = ctx->iface->init(ctx);

    if (res) {
      ctx->err_detail = ctx->priv ? ctx->priv->err_detail : NULL;
      avm_codec_destroy(ctx);
    }
  }

  return SAVE_STATUS(ctx, res);
}

avm_codec_err_t avm_codec_enc_config_default(avm_codec_iface_t *iface,
                                             avm_codec_enc_cfg_t *cfg,
                                             unsigned int usage) {
  avm_codec_err_t res;
  int i;

  if (!iface || !cfg)
    res = AVM_CODEC_INVALID_PARAM;
  else if (!(iface->caps & AVM_CODEC_CAP_ENCODER))
    res = AVM_CODEC_INCAPABLE;
  else {
    res = AVM_CODEC_INVALID_PARAM;

    for (i = 0; i < iface->enc.cfg_count; ++i) {
      if (iface->enc.cfgs[i].g_usage == usage) {
        *cfg = iface->enc.cfgs[i];
        res = AVM_CODEC_OK;
        break;
      }
    }
  }
  /* default values */
  if (cfg) {
    memset(&cfg->encoder_cfg, 0, sizeof(cfg->encoder_cfg));
    cfg->encoder_cfg.superblock_size = 0;  // Dynamic
    cfg->encoder_cfg.max_partition_size = 256;
    cfg->encoder_cfg.min_partition_size = 4;
    cfg->encoder_cfg.enable_trellis_quant = 3;
  }
  return res;
}

#if ARCH_X86 || ARCH_X86_64
/* On X86, disable the x87 unit's internal 80 bit precision for better
 * consistency with the SSE unit's 64 bit precision.
 */
#include "avm_ports/x86.h"
#define FLOATING_POINT_SET_PRECISION \
  unsigned short x87_orig_mode = x87_set_double_precision();
#define FLOATING_POINT_RESTORE_PRECISION x87_set_control_word(x87_orig_mode);
#else
#define FLOATING_POINT_SET_PRECISION
#define FLOATING_POINT_RESTORE_PRECISION
#endif  // ARCH_X86 || ARCH_X86_64

#if HAVE_FEXCEPT && CONFIG_DEBUG
#define FLOATING_POINT_SET_EXCEPTIONS \
  const int float_excepts =           \
      feenableexcept(FE_DIVBYZERO | FE_UNDERFLOW | FE_OVERFLOW);
#define FLOATING_POINT_RESTORE_EXCEPTIONS \
  fedisableexcept(FE_ALL_EXCEPT);         \
  feenableexcept(float_excepts);
#else
#define FLOATING_POINT_SET_EXCEPTIONS
#define FLOATING_POINT_RESTORE_EXCEPTIONS
#endif  // HAVE_FEXCEPT && CONFIG_DEBUG

/* clang-format off */
#define FLOATING_POINT_INIT    \
  do {                         \
  FLOATING_POINT_SET_PRECISION \
  FLOATING_POINT_SET_EXCEPTIONS

#define FLOATING_POINT_RESTORE      \
  FLOATING_POINT_RESTORE_EXCEPTIONS \
  FLOATING_POINT_RESTORE_PRECISION  \
  } while (0);
/* clang-format on */

avm_codec_err_t avm_codec_encode(avm_codec_ctx_t *ctx, const avm_image_t *img,
                                 avm_codec_pts_t pts, unsigned long duration,
                                 avm_enc_frame_flags_t flags) {
  avm_codec_err_t res = AVM_CODEC_OK;

  if (!ctx || (img && !duration))
    res = AVM_CODEC_INVALID_PARAM;
  else if (!ctx->iface || !ctx->priv)
    res = AVM_CODEC_ERROR;
  else if (!(ctx->iface->caps & AVM_CODEC_CAP_ENCODER))
    res = AVM_CODEC_INCAPABLE;
  else {
    /* Execute in a normalized floating point environment, if the platform
     * requires it.
     */
    FLOATING_POINT_INIT
    res = ctx->iface->enc.encode(get_alg_priv(ctx), img, pts, duration, flags);
    FLOATING_POINT_RESTORE
  }

  return SAVE_STATUS(ctx, res);
}

const avm_codec_cx_pkt_t *avm_codec_get_cx_data(avm_codec_ctx_t *ctx,
                                                avm_codec_iter_t *iter) {
  const avm_codec_cx_pkt_t *pkt = NULL;

  if (ctx) {
    if (!iter)
      ctx->err = AVM_CODEC_INVALID_PARAM;
    else if (!ctx->iface || !ctx->priv)
      ctx->err = AVM_CODEC_ERROR;
    else if (!(ctx->iface->caps & AVM_CODEC_CAP_ENCODER))
      ctx->err = AVM_CODEC_INCAPABLE;
    else
      pkt = ctx->iface->enc.get_cx_data(get_alg_priv(ctx), iter);
  }

  if (pkt && pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
    // If the application has specified a destination area for the
    // compressed data, and the codec has not placed the data there,
    // and it fits, copy it.
    avm_codec_priv_t *const priv = ctx->priv;
    char *const dst_buf = (char *)priv->enc.cx_data_dst_buf.buf;

    if (dst_buf && pkt->data.raw.buf != dst_buf &&
        pkt->data.raw.sz + priv->enc.cx_data_pad_before +
                priv->enc.cx_data_pad_after <=
            priv->enc.cx_data_dst_buf.sz) {
      avm_codec_cx_pkt_t *modified_pkt = &priv->enc.cx_data_pkt;

      memcpy(dst_buf + priv->enc.cx_data_pad_before, pkt->data.raw.buf,
             pkt->data.raw.sz);
      *modified_pkt = *pkt;
      modified_pkt->data.raw.buf = dst_buf;
      modified_pkt->data.raw.sz +=
          priv->enc.cx_data_pad_before + priv->enc.cx_data_pad_after;
      pkt = modified_pkt;
    }

    if (dst_buf == pkt->data.raw.buf) {
      priv->enc.cx_data_dst_buf.buf = dst_buf + pkt->data.raw.sz;
      priv->enc.cx_data_dst_buf.sz -= pkt->data.raw.sz;
    }
  }

  return pkt;
}

avm_codec_err_t avm_codec_set_cx_data_buf(avm_codec_ctx_t *ctx,
                                          const avm_fixed_buf_t *buf,
                                          unsigned int pad_before,
                                          unsigned int pad_after) {
  if (!ctx || !ctx->priv) return AVM_CODEC_INVALID_PARAM;

  if (buf) {
    ctx->priv->enc.cx_data_dst_buf = *buf;
    ctx->priv->enc.cx_data_pad_before = pad_before;
    ctx->priv->enc.cx_data_pad_after = pad_after;
  } else {
    ctx->priv->enc.cx_data_dst_buf.buf = NULL;
    ctx->priv->enc.cx_data_dst_buf.sz = 0;
    ctx->priv->enc.cx_data_pad_before = 0;
    ctx->priv->enc.cx_data_pad_after = 0;
  }

  return AVM_CODEC_OK;
}

const avm_image_t *avm_codec_get_preview_frame(avm_codec_ctx_t *ctx) {
  avm_image_t *img = NULL;

  if (ctx) {
    if (!ctx->iface || !ctx->priv)
      ctx->err = AVM_CODEC_ERROR;
    else if (!(ctx->iface->caps & AVM_CODEC_CAP_ENCODER))
      ctx->err = AVM_CODEC_INCAPABLE;
    else if (!ctx->iface->enc.get_preview)
      ctx->err = AVM_CODEC_INCAPABLE;
    else
      img = ctx->iface->enc.get_preview(get_alg_priv(ctx));
  }

  return img;
}

avm_fixed_buf_t *avm_codec_get_global_headers(avm_codec_ctx_t *ctx) {
  avm_fixed_buf_t *buf = NULL;

  if (ctx) {
    if (!ctx->iface || !ctx->priv)
      ctx->err = AVM_CODEC_ERROR;
    else if (!(ctx->iface->caps & AVM_CODEC_CAP_ENCODER))
      ctx->err = AVM_CODEC_INCAPABLE;
    else if (!ctx->iface->enc.get_glob_hdrs)
      ctx->err = AVM_CODEC_INCAPABLE;
    else
      buf = ctx->iface->enc.get_glob_hdrs(get_alg_priv(ctx));
  }

  return buf;
}

avm_codec_err_t avm_codec_enc_config_set(avm_codec_ctx_t *ctx,
                                         const avm_codec_enc_cfg_t *cfg) {
  avm_codec_err_t res;

  if (!ctx || !ctx->iface || !ctx->priv || !cfg)
    res = AVM_CODEC_INVALID_PARAM;
  else if (!(ctx->iface->caps & AVM_CODEC_CAP_ENCODER))
    res = AVM_CODEC_INCAPABLE;
  else
    res = ctx->iface->enc.cfg_set(get_alg_priv(ctx), cfg);

  return SAVE_STATUS(ctx, res);
}

int avm_codec_pkt_list_add(struct avm_codec_pkt_list *list,
                           const struct avm_codec_cx_pkt *pkt) {
  if (list->cnt < list->max) {
    list->pkts[list->cnt++] = *pkt;
    return 0;
  }

  return 1;
}

const avm_codec_cx_pkt_t *avm_codec_pkt_list_get(
    struct avm_codec_pkt_list *list, avm_codec_iter_t *iter) {
  const avm_codec_cx_pkt_t *pkt;

  if (!(*iter)) {
    *iter = list->pkts;
  }

  pkt = (const avm_codec_cx_pkt_t *)*iter;

  if ((size_t)(pkt - list->pkts) < list->cnt)
    *iter = pkt + 1;
  else
    pkt = NULL;

  return pkt;
}
