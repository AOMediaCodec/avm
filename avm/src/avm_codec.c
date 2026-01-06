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
#include <stdarg.h>
#include <stdlib.h>

#include "config/avm_config.h"
#include "config/avm_version.h"

#include "avm/avm_integer.h"
#include "avm/internal/avm_codec_internal.h"

int avm_codec_version(void) { return VERSION_PACKED; }

const char *avm_codec_version_str(void) { return VERSION_STRING_NOSP; }

const char *avm_codec_version_extra_str(void) { return VERSION_EXTRA; }

const char *avm_codec_iface_name(avm_codec_iface_t *iface) {
  return iface ? iface->name : "<invalid interface>";
}

const char *avm_codec_err_to_string(avm_codec_err_t err) {
  switch (err) {
    case AVM_CODEC_OK: return "Success";
    case AVM_CODEC_ERROR: return "Unspecified internal error";
    case AVM_CODEC_MEM_ERROR: return "Memory allocation error";
    case AVM_CODEC_ABI_MISMATCH: return "ABI version mismatch";
    case AVM_CODEC_INCAPABLE:
      return "Codec does not implement requested capability";
    case AVM_CODEC_UNSUP_BITSTREAM:
      return "Bitstream not supported by this decoder";
    case AVM_CODEC_UNSUP_FEATURE:
      return "Bitstream required feature not supported by this decoder";
    case AVM_CODEC_CORRUPT_FRAME: return "Corrupt frame detected";
    case AVM_CODEC_INVALID_PARAM: return "Invalid parameter";
    case AVM_CODEC_LIST_END: return "End of iterated list";
  }

  return "Unrecognized error code";
}

const char *avm_codec_error(avm_codec_ctx_t *ctx) {
  return (ctx) ? avm_codec_err_to_string(ctx->err)
               : avm_codec_err_to_string(AVM_CODEC_INVALID_PARAM);
}

const char *avm_codec_error_detail(avm_codec_ctx_t *ctx) {
  if (ctx && ctx->err)
    return ctx->priv ? ctx->priv->err_detail : ctx->err_detail;

  return NULL;
}

avm_codec_err_t avm_codec_destroy(avm_codec_ctx_t *ctx) {
  if (!ctx) {
    return AVM_CODEC_INVALID_PARAM;
  }
  if (!ctx->iface || !ctx->priv) {
    ctx->err = AVM_CODEC_ERROR;
    return AVM_CODEC_ERROR;
  }
  ctx->iface->destroy((avm_codec_alg_priv_t *)ctx->priv);
  ctx->iface = NULL;
  ctx->name = NULL;
  ctx->priv = NULL;
  ctx->err = AVM_CODEC_OK;
  return AVM_CODEC_OK;
}

avm_codec_caps_t avm_codec_get_caps(avm_codec_iface_t *iface) {
  return (iface) ? iface->caps : 0;
}

avm_codec_err_t avm_codec_control(avm_codec_ctx_t *ctx, int ctrl_id, ...) {
  if (!ctx) {
    return AVM_CODEC_INVALID_PARAM;
  }
  // Control ID must be non-zero.
  if (!ctrl_id) {
    ctx->err = AVM_CODEC_INVALID_PARAM;
    return AVM_CODEC_INVALID_PARAM;
  }
  if (!ctx->iface || !ctx->priv || !ctx->iface->ctrl_maps) {
    ctx->err = AVM_CODEC_ERROR;
    return AVM_CODEC_ERROR;
  }

  // "ctrl_maps" is an array of (control ID, function pointer) elements,
  // with CTRL_MAP_END as a sentinel.
  for (avm_codec_ctrl_fn_map_t *entry = ctx->iface->ctrl_maps;
       !at_ctrl_map_end(entry); ++entry) {
    if (entry->ctrl_id == ctrl_id) {
      va_list ap;
      va_start(ap, ctrl_id);
      ctx->err = entry->fn((avm_codec_alg_priv_t *)ctx->priv, ap);
      va_end(ap);
      return ctx->err;
    }
  }
  ctx->err = AVM_CODEC_ERROR;
  return AVM_CODEC_ERROR;
}

avm_codec_err_t avm_codec_set_option(avm_codec_ctx_t *ctx, const char *name,
                                     const char *value) {
  if (!ctx) {
    return AVM_CODEC_INVALID_PARAM;
  }
  if (!ctx->iface || !ctx->priv || !ctx->iface->set_option) {
    ctx->err = AVM_CODEC_ERROR;
    return AVM_CODEC_ERROR;
  }
  ctx->err =
      ctx->iface->set_option((avm_codec_alg_priv_t *)ctx->priv, name, value);
  return ctx->err;
}

void avm_internal_error(struct avm_internal_error_info *info,
                        avm_codec_err_t error, const char *fmt, ...) {
  va_list ap;

  info->error_code = error;
  info->has_detail = 0;

  if (fmt) {
    size_t sz = sizeof(info->detail);

    info->has_detail = 1;
    va_start(ap, fmt);
    vsnprintf(info->detail, sz - 1, fmt, ap);
    va_end(ap);
    info->detail[sz - 1] = '\0';
  }

  if (info->setjmp) longjmp(info->jmp, info->error_code);
}

void avm_merge_corrupted_flag(int *corrupted, int value) {
  *corrupted |= value;
}

const char *avm_obu_type_to_string(OBU_TYPE type) {
  switch (type) {
    case OBU_SEQUENCE_HEADER: return "OBU_SEQUENCE_HEADER";
#if CONFIG_CWG_F270_CI_OBU
    case OBU_CONTENT_INTERPRETATION: return "OBU_CONTENT_INTERPRETATION";
#endif  // CONFIG_CWG_F270_CI_OBU
    case OBU_TEMPORAL_DELIMITER: return "OBU_TEMPORAL_DELIMITER";
    case OBU_MULTI_FRAME_HEADER: return "OBU_MULTI_FRAME_HEADER";
    case OBU_SWITCH: return "OBU_SWITCH";
    case OBU_LEADING_SEF: return "OBU_LEADING_SEF";
    case OBU_REGULAR_SEF: return "OBU_REGULAR_SEF";
    case OBU_LEADING_TIP: return "OBU_LEADING_TIP";
    case OBU_REGULAR_TIP: return "OBU_REGULAR_TIP";
    case OBU_LEADING_TILE_GROUP: return "OBU_LEADING_TILE_GROUP";
    case OBU_REGULAR_TILE_GROUP: return "OBU_REGULAR_TILE_GROUP";
    case OBU_CLK: return "OBU_CLK";
    case OBU_OLK: return "OBU_OLK";
    case OBU_METADATA_SHORT: return "OBU_METADATA_SHORT";
    case OBU_METADATA_GROUP: return "OBU_METADATA_GROUP";
    case OBU_LAYER_CONFIGURATION_RECORD:
      return "OBU_LAYER_CONFIGURATION_RECORD";
    case OBU_ATLAS_SEGMENT: return "OBU_ATLAS_SEGMENT";
    case OBU_OPERATING_POINT_SET: return "OBU_OPERATING_POINT_SET";
    case OBU_BRIDGE_FRAME: return "OBU_BRIDGE_FRAME";
    case OBU_MSDO: return "OBU_MSDO";
    case OBU_RAS_FRAME: return "OBU_RAS_FRAME";
    case OBU_PADDING: return "OBU_PADDING";
    case OBU_QM: return "OBU_QM";
    case OBU_FGM: return "OBU_FGM";
    default: break;
  }
  return "<Invalid OBU Type>";
}
