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

#include <assert.h>
#include <math.h>
#include <string.h>

#include "config/aom_scale_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_dsp/binary_codes_writer.h"
#include "av1/common/cnn_tflite.h"
#include "av1/common/guided_quadtree.h"
#include "av1/common/reconinter.h"
#include "av1/encoder/cost.h"

//#if CONFIG_CNN_GUIDED_QUADTREE
//
//#include "av1/tflite_models/intra_frame_model/qp235_quadtree.cc"
//#endif

// utils

int64_t computeSSE_buf_tflite_hbd(uint16_t *buf_all, uint16_t *src, int startx,
                                  int starty, int buf_width, int buf_height,
                                  int buf_stride, int src_stride) {
  int64_t uiSSDtemp = 0;
  for (int y = starty; y < starty + buf_height; y++) {
    for (int x = startx; x < startx + buf_width; x++) {
      int iDiff = (int)(buf_all[y * buf_stride + x] - src[y * src_stride + x]);
      uiSSDtemp += iDiff * iDiff;
    }
  }
  return uiSSDtemp;
}

double min_tflite(double a, double b, double c, double d) {
  double res = a;
  res = AOMMIN(res, b);
  res = AOMMIN(res, c);
  res = AOMMIN(res, d);
  return res;
}

void replace_tflite_hbd(int startx, int starty, int width, int height,
                        uint16_t *rec, uint16_t *buf, int stride) {
  for (int i = starty; i < starty + height; i++) {
    for (int j = startx; j < startx + width; j++) {
      rec[i * stride + j] = buf[i * stride + j];
    }
  }
  return;
}

double computePSNR_buf_tflite_hbd(uint16_t *buf_all, uint16_t *dgd,
                                  uint16_t *src, int startx, int starty,
                                  int buf_width, int buf_height, int height,
                                  int width, int buf_stride, int dgd_stride,
                                  int src_stride, int bit_depth) {
  int iSize = height * width;  // m*nd
  double uiSSDtemp = 0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (y >= starty && y < starty + buf_height && x >= startx &&
          x < startx + buf_width) {
        int iDiff =
            (int)(buf_all[y * buf_stride + x] - src[y * src_stride + x]);
        // c[y][x] = (buf_all[y][x]);
        // c[y][x]=0;
        uiSSDtemp += iDiff * iDiff;
      } else {
        int iDiff = (int)(dgd[y * dgd_stride + x] -
                          src[y * src_stride + x]);  //������ȡdiff
        // c[y][x] = (int)(dgd[y * dgd_stride + x]);
        uiSSDtemp += iDiff * iDiff;  // diff��ƽ��
      }
    }
  }
  // cv::Mat image;
  // image = cv::Mat(480, 832, CV_8UC1, (void *)c);
  // cv::imshow("psnr", image);
  // cv::waitKey();
  const int maxval =
      ((1 << bit_depth) - 1);  // MAXI�Ǳ�ʾͼ�����ɫ�������ֵ�����ÿ���������� 8
                               // λ��ʾ����ô����
                               // 255�����ڻ�ȡ��ǰch�Ĳ��������λ����������λ��
  const double fRefValue =
      (double)maxval * maxval *
      iSize;  // MAXi��ƽ������MSE��MSE=diff*diff/size�����psnr�ȼ���
              // ��MAXi��ƽ��*size��/��diff*diff��
  double psnr =
      (uiSSDtemp ? 10.0 * log10(fRefValue / (double)uiSSDtemp)
                 : 999.99);  //���û����Ϊʲô�и�999.99����������INF����ֹ��ȡʱΪ�հ�
  return psnr;
}

double computePSNR_tflite_hbd(uint16_t *dgd, uint16_t *src, int height,
                              int width, int dgd_stride, int src_stride,
                              int bit_depth) {
  int iSize = height * width;  // m*nd

  double uiSSDtemp = 0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int iDiff = (int)(dgd[x] - src[x]);  //������ȡdiff
      uiSSDtemp += iDiff * iDiff;          // diff��ƽ��
    }
    dgd +=
        dgd_stride;  //����ͼ���stride�߽�
                     //�߽�û�����ݣ����ǻ���ͼ����
    src += src_stride;
  }
  int maxval;
  maxval = ((1 << bit_depth) - 1);
  const double fRefValue =
      (double)maxval * maxval *
      iSize;  // MAXi��ƽ������MSE��MSE=diff*diff/size�����psnr�ȼ���
              // ��MAXi��ƽ��*size��/��diff*diff��
  double psnr =
      (uiSSDtemp ? 10.0 * log10(fRefValue / (double)uiSSDtemp)
                 : 999.99);  //���û����Ϊʲô�и�999.99����������INF����ֹ��ȡʱΪ�հ�
  return psnr;
}

int CalculateIndex_tflite(int width, int block_size_h, int block_size_w,
                          int starty, int startx, int quadtree_max_size) {
  int index;
  if (block_size_h == block_size_w) {
    if (block_size_h == quadtree_max_size) {
      int stride = (int)ceil((float)width / block_size_w);
      index = stride * (starty / block_size_h) + startx / block_size_w;
    } else {
      //ֻ�������index��MAX_SIZE���Ѿ�����
      int stride = (int)floor((float)width / quadtree_max_size) *
                   quadtree_max_size / block_size_w;
      index = stride * (starty / block_size_h) + startx / block_size_w;
    }
  } else {
    int stride = (int)floor((float)width / quadtree_max_size) *
                 quadtree_max_size / block_size_w;
    index = stride * (starty / block_size_h) + startx / block_size_w;
  }

  return index;
}

// TODO(urvang@google.com): replace quantSet with struct.
// guided conv unet intra
int qp255_quadtree_model_quantSet_intra[] = { 25, 197, 0, -8 };
int qp205_quadtree_model_quantSet_intra[] = { 643189, 690747, -17, -5 };
int qp175_quadtree_model_quantSet_intra[] = { 1838, 2153, 3, -12 };
int qp145_quadtree_model_quantSet_intra[] = { 27230, 33505, -21, 3 };
int qp120_quadtree_model_quantSet_intra[] = { 133, 199, 1, -7 };
int qp90_quadtree_model_quantSet_intra[] = { 988, 1428, 0, -11 };

// guided conv unet with attention inter
int qp255_quadtree_model_quantSet_inter[] = { 56619, 15796, -9, -13 };
int qp205_quadtree_model_quantSet_inter[] = { 551342, 949167, -14, -5 };
int qp175_quadtree_model_quantSet_inter[] = { 2011, 3876, 0, -14 };
int qp145_quadtree_model_quantSet_inter[] = { 32668, 44115, -18, 2 };
int qp120_quadtree_model_quantSet_inter[] = { 20817, 19072, -12, -12 };
int qp90_quadtree_model_quantSet_inter[] = { 3455, 16494, -16, -8 };

#if CONFIG_EXT_SUPERRES
// Superres guided conv unet intra.
int sr2by1ai_1_quantset[] = { 17235, 16969, -10, -10 };
int sr2by1ai_2_quantset[] = { 76705024, 109138704, -10, -7 };
int sr2by1ai_3_quantset[] = { 335, 464, -4, -8 };

int sr3by2ai_1_quantset[] = { 385035, 450967, -8, -10 };
int sr3by2ai_2_quantset[] = { 54186, 53143, 2, -13 };
int sr3by2ai_3_quantset[] = { 608, 795, -5, -8 };

int sr5by4ai_1_quantset[] = { 57611, 154741, -2, 2 };
int sr5by4ai_2_quantset[] = { 166, 503, -12, -1 };
int sr5by4ai_3_quantset[] = { 11, 4, -11, -9 };

int sr7by4ai_1_quantset[] = { 2631403, 4529410, -8, -8 };
int sr7by4ai_2_quantset[] = { 28290, 12216, 1, -3 };
int sr7by4ai_3_quantset[] = { 11, 9, -7, -8 };

// Superres guided conv unet inter.
int sr2by1ra_1_quantset[] = { 15680, 12890, -10, -9 };
int sr2by1ra_2_quantset[] = { 85696504, 103679088, -8, -9 };
int sr2by1ra_3_quantset[] = { 2067139, 493382, -6, -6 };

int sr3by2ra_1_quantset[] = { 651704, 364241, -9, -9 };
int sr3by2ra_2_quantset[] = { 89884, 88521, 3, -12 };
int sr3by2ra_3_quantset[] = { 3133894, 9945744, -7, -11 };

int sr5by4ra_1_quantset[] = { 40478, 106889, -3, 3 };
int sr5by4ra_2_quantset[] = { 424139, 223163, 6, 0 };
int sr5by4ra_3_quantset[] = { 164313, 57994, -5, -7 };

int sr7by4ra_1_quantset[] = { 8734122, 6400229, -6, -7 };
int sr7by4ra_2_quantset[] = { 295, 36, -3, -9 };
int sr7by4ra_3_quantset[] = { 21017, 33976, -13, -6 };
#endif  // CONFIG_EXT_SUPERRES

int *get_quadparm_from_qindex(int qindex, int superres_denom, int is_intra_only,
                              int is_luma, int cnn_index) {
#if CONFIG_EXT_SUPERRES
  assert(superres_denom == SCALE_NUMERATOR || superres_denom == 10 ||
         superres_denom == 12 || superres_denom == 14 || superres_denom == 16);
#else
  assert(superres_denom == SCALE_NUMERATOR);
#endif                                      // CONFIG_EXT_SUPERRES
  if (superres_denom == SCALE_NUMERATOR) {  // quadtree
    if (is_luma) {
      if (is_intra_only) {
        if (qindex <= 90) {
          return (cnn_index == 0)   ? qp90_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_intra
                                    : qp145_quadtree_model_quantSet_intra;

        } else if (qindex <= 120) {
          return (cnn_index == 0)   ? qp120_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp90_quadtree_model_quantSet_intra
                                    : qp145_quadtree_model_quantSet_intra;
        } else if (qindex <= 145) {
          return (cnn_index == 0)   ? qp145_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_intra
                                    : qp175_quadtree_model_quantSet_intra;
        } else if (qindex <= 175) {
          return (cnn_index == 0)   ? qp175_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp145_quadtree_model_quantSet_intra
                                    : qp205_quadtree_model_quantSet_intra;
        } else if (qindex <= 205) {
          return (cnn_index == 0)   ? qp205_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp175_quadtree_model_quantSet_intra
                                    : qp255_quadtree_model_quantSet_intra;
        } else {
          return (cnn_index == 0)   ? qp255_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp205_quadtree_model_quantSet_intra
                                    : qp175_quadtree_model_quantSet_intra;
        }
      } else {
        if (qindex <= 90) {
          return (cnn_index == 0)   ? qp90_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_inter
                                    : qp145_quadtree_model_quantSet_inter;

        } else if (qindex <= 120) {
          return (cnn_index == 0)   ? qp120_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp90_quadtree_model_quantSet_inter
                                    : qp145_quadtree_model_quantSet_inter;
        } else if (qindex <= 145) {
          return (cnn_index == 0)   ? qp145_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_inter
                                    : qp175_quadtree_model_quantSet_inter;
        } else if (qindex <= 175) {
          return (cnn_index == 0)   ? qp175_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp145_quadtree_model_quantSet_inter
                                    : qp205_quadtree_model_quantSet_inter;
        } else if (qindex <= 205) {
          return (cnn_index == 0)   ? qp205_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp175_quadtree_model_quantSet_inter
                                    : qp255_quadtree_model_quantSet_inter;
        } else {
          return (cnn_index == 0)   ? qp255_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp205_quadtree_model_quantSet_inter
                                    : qp175_quadtree_model_quantSet_inter;
        }
      }
    }
  }
#if CONFIG_EXT_SUPERRES
  assert(is_luma);
  if (is_intra_only) {
#if SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        return (cnn_index == 0)   ? sr5by4ai_1_quantset
               : (cnn_index == 1) ? sr5by4ai_2_quantset
                                  : sr5by4ai_3_quantset;
      case 12:
        return (cnn_index == 0)   ? sr3by2ai_1_quantset
               : (cnn_index == 1) ? sr3by2ai_2_quantset
                                  : sr3by2ai_3_quantset;
      case 14:
        return (cnn_index == 0)   ? sr7by4ai_1_quantset
               : (cnn_index == 1) ? sr7by4ai_2_quantset
                                  : sr7by4ai_3_quantset;
      case 16:
        return (cnn_index == 0)   ? sr2by1ai_1_quantset
               : (cnn_index == 1) ? sr2by1ai_2_quantset
                                  : sr2by1ai_3_quantset;
      default: assert(0); return NULL;
    }
#else   // SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        if (qindex < 120)
          return sr5by4ai_1_quantset;
        else if (qindex < 180)
          return sr5by4ai_2_quantset;
        else
          return sr5by4ai_3_quantset;
      case 12:
        if (qindex < 120)
          return sr3by2ai_1_quantset;
        else if (qindex < 180)
          return sr3by2ai_2_quantset;
        else
          return sr3by2ai_3_quantset;
      case 14:
        if (qindex < 120)
          return sr7by4ai_1_quantset;
        else if (qindex < 180)
          return sr7by4ai_2_quantset;
        else
          return sr7by4ai_3_quantset;
      case 16:
        if (qindex < 120)
          return sr2by1ai_1_quantset;
        else if (qindex < 180)
          return sr2by1ai_2_quantset;
        else
          return sr2by1ai_3_quantset;
      default: assert(0); return NULL;
    }
#endif  // SELECT_CNN_FOR_SUPERRES
  } else {
#if SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        return (cnn_index == 0)   ? sr5by4ra_1_quantset
               : (cnn_index == 1) ? sr5by4ra_2_quantset
                                  : sr5by4ra_3_quantset;
      case 12:
        return (cnn_index == 0)   ? sr3by2ra_1_quantset
               : (cnn_index == 1) ? sr3by2ra_2_quantset
                                  : sr3by2ra_3_quantset;
      case 14:
        return (cnn_index == 0)   ? sr7by4ra_1_quantset
               : (cnn_index == 1) ? sr7by4ra_2_quantset
                                  : sr7by4ra_3_quantset;
      case 16:
        return (cnn_index == 0)   ? sr2by1ra_1_quantset
               : (cnn_index == 1) ? sr2by1ra_2_quantset
                                  : sr2by1ra_3_quantset;
      default: assert(0); return NULL;
    }
#else   // SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        if (qindex < 120)
          return sr5by4ra_1_quantset;
        else if (qindex < 180)
          return sr5by4ra_2_quantset;
        else
          return sr5by4ra_3_quantset;
      case 12:
        if (qindex < 120)
          return sr3by2ra_1_quantset;
        else if (qindex < 180)
          return sr3by2ra_2_quantset;
        else
          return sr3by2ra_3_quantset;
      case 14:
        if (qindex < 120)
          return sr7by4ra_1_quantset;
        else if (qindex < 180)
          return sr7by4ra_2_quantset;
        else
          return sr7by4ra_3_quantset;
      case 16:
        if (qindex < 120)
          return sr2by1ra_1_quantset;
        else if (qindex < 180)
          return sr2by1ra_2_quantset;
        else
          return sr2by1ra_3_quantset;
      default: assert(0); return NULL;
    }
#endif  // SELECT_CNN_FOR_SUPERRES
  }
#endif  // CONFIG_EXT_SUPERRES
  return NULL;
}

#if CONFIG_CNN_GUIDED_QUADTREE
int64_t count_guided_quad_bits(struct AV1Common *cm, int *splitcosts,
                               int (*norestorecost)[2]) {
  (void)norestorecost;
  int64_t bits = 0;
  for (int i = 0; i < cm->cur_quad_info.split_info_length; i += 2) {
    if (cm->cur_quad_info.split_info[i].split == 0 &&
        cm->cur_quad_info.split_info[i + 1].split == 1) {
      // bits += (4 * 2 * 4 + 2);
      bits += splitcosts[1];
      // printf("it'split\n");
    } else if (cm->cur_quad_info.split_info[i].split == 1 &&
               cm->cur_quad_info.split_info[i + 1].split == 1) {
      // bits += (4 * 2 * 2 + 2);
      bits += splitcosts[3];
      // printf("it's horz\n");
    } else if (cm->cur_quad_info.split_info[i].split == 1 &&
               cm->cur_quad_info.split_info[i + 1].split == 0) {
      // bits += (4 * 2 * 2 + 2);
      bits += splitcosts[2];
      // printf("it's vert\n");
    } else if (cm->cur_quad_info.split_info[i].split == 0 &&
               cm->cur_quad_info.split_info[i + 1].split == 0) {
      // bits += (4 * 2 + 2);
      bits += splitcosts[0];
      // printf("it's all\n");
    }
  }
  const int is_intra_only = frame_is_intra_only(cm);
  int *quadtset = get_quadparm_from_qindex(
      cm->quant_params.base_qindex, cm->superres_scale_denominator,
      is_intra_only, 1, cm->cnn_indices[0]);
  const int norestore_ctx =
      get_guided_norestore_ctx(cm->quant_params.base_qindex,
                               cm->superres_scale_denominator, is_intra_only);
  const int A0_min = quadtset[2];
  const int A1_min = quadtset[3];
  int ref0 = 8;
  int ref1 = 8;
  int bits_coeff = 0;
  for (int i = 0; i < cm->cur_quad_info.unit_info_length; i++) {
    if (cm->cur_quad_info.unit_info[i].xqd[0] == 0 &&
        cm->cur_quad_info.unit_info[i].xqd[1] == 0) {
      if (norestore_ctx != -1) bits_coeff += norestorecost[norestore_ctx][1];
      ref0 = AOMMIN(AOMMAX(0, A0_min), A0_min + 15) - A0_min;
      ref1 = AOMMIN(AOMMAX(0, A1_min), A1_min + 15) - A1_min;
    } else {
      const int a0 = cm->cur_quad_info.unit_info[i].xqd[0] - A0_min;
      const int a1 = cm->cur_quad_info.unit_info[i].xqd[1] - A1_min;
      if (norestore_ctx != -1) bits_coeff += norestorecost[norestore_ctx][0];
      bits_coeff += ((aom_count_primitive_refsubexpfin(16, 1, ref0, a0) +
                      aom_count_primitive_refsubexpfin(16, 1, ref1, a1))
                     << AV1_PROB_COST_SHIFT);
      ref0 = a0;
      ref1 = a1;
    }
  }
  /*
  printf("  Bits_coeff %d for %d units\n", bits_coeff,
         cm->cur_quad_info.unit_info_length);
         */
  bits += bits_coeff;
  return bits;
}

void quad_copy(QUADInfo *cur_quad_info, QUADInfo *postcnn_quad_info) {
  int split_length = cur_quad_info->split_info_length;
  int unit_info_length = cur_quad_info->unit_info_length;
  postcnn_quad_info->split_info_length = split_length;
  postcnn_quad_info->unit_info_length = unit_info_length;
  postcnn_quad_info->unit_size = cur_quad_info->unit_size;

  for (int i = 0; i < split_length; ++i) {
    postcnn_quad_info->split_info[i].split = cur_quad_info->split_info[i].split;
  }

  for (int i = 0; i < unit_info_length; ++i) {
    postcnn_quad_info->unit_info[i].xqd[0] = cur_quad_info->unit_info[i].xqd[0];
    postcnn_quad_info->unit_info[i].xqd[1] = cur_quad_info->unit_info[i].xqd[1];
  }
}

// Returns (int)floor(x / y),
#define DIVIDE_WITH_FLOOR(x, y) ((x) / (y))
// Returns (int)ceil(x / y),
#define DIVIDE_WITH_CEILING(x, y) (((x) + (y)-1) / (y))

int quad_tree_get_unit_info_length(int width, int height, int unit_length,
                                   const QUADSplitInfo *split_info,
                                   int split_info_length) {
  // We can compute total units as follows:
  // (1) regular units: they may / may not be split. So, compute length of
  // regular unit info by going through the split_info array. (2) unregular
  // units (blocks near boundary that are NOT unit_length in size): they are
  // never split. So, length of unregular unit info is same as number of
  // unregular units.
  const int regular_units = DIVIDE_WITH_FLOOR(width, unit_length) *
                            DIVIDE_WITH_FLOOR(height, unit_length);
  assert(regular_units * 2 == split_info_length);
  const int total_units = DIVIDE_WITH_CEILING(width, unit_length) *
                          DIVIDE_WITH_CEILING(height, unit_length);
  const int unregular_unit_info_len = total_units - regular_units;

  int regular_unit_info_len = 0;
  for (int i = 0; i < split_info_length; i += 2) {
    if (split_info[i].split == 0 && split_info[i + 1].split == 1) {
      regular_unit_info_len += 4;  // Split
    } else if (split_info[i].split == 1 && split_info[i + 1].split == 1) {
      regular_unit_info_len += 2;  // Horz
    } else if (split_info[i].split == 1 && split_info[i + 1].split == 0) {
      regular_unit_info_len += 2;  // Vert
    } else {
      assert(split_info[i].split == 0 && split_info[i + 1].split == 0);
      regular_unit_info_len += 1;  // No split
    }
  }

  return regular_unit_info_len + unregular_unit_info_len;
}

int quad_tree_get_split_info_length(int width, int height, int unit_length) {
  // Split info only signaled for units of full size. Blocks near boundaries are
  // never split, so no info is signaled for those.
  const int num_split_info_wide = DIVIDE_WITH_FLOOR(width, unit_length);
  const int num_split_info_high = DIVIDE_WITH_FLOOR(height, unit_length);
  // 2 bits signaled for each split info.
  return num_split_info_wide * num_split_info_high * 2;
}
#endif  // CONFIG_CNN_GUIDED_QUADTREE
