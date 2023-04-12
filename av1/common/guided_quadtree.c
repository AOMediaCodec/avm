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
#include "av1/common/guided_quadtree.h"
#include "av1/common/reconinter.h"
#include "av1/encoder/cost.h"

//#if CONFIG_CNN_GUIDED_QUADTREE
//
//#include "av1/tflite_models/intra_frame_model/qp235_quadtree.cc"
//#endif

// utils

int computeSSE_buf_tflite_hbd(uint16_t *buf_all, uint16_t *src, int startx,
                              int starty, int buf_width, int buf_height,
                              int height, int width, int buf_stride,
                              int src_stride) {
  int uiSSDtemp = 0;
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
        // int iDiff = (int)(dgd[y * dgd_stride + x] -
        //                  src[y * src_stride + x]);  //遍历获取diff
        //// c[y][x] = (int)(dgd[y * dgd_stride + x]);
        // uiSSDtemp += iDiff * iDiff;  // diff的平方
      }
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
                          src[y * src_stride + x]);  //遍历获取diff
        // c[y][x] = (int)(dgd[y * dgd_stride + x]);
        uiSSDtemp += iDiff * iDiff;  // diff的平方
      }
    }
  }
  // cv::Mat image;
  // image = cv::Mat(480, 832, CV_8UC1, (void *)c);
  // cv::imshow("psnr", image);
  // cv::waitKey();
  const int maxval = ((1 << bit_depth) -
                      1);  // MAXI是表示图像点颜色的最大数值，如果每个采样点用 8
                           // 位表示，那么就是
                           // 255。现在获取当前ch的采样点比特位数，并进行位移
  const double fRefValue =
      (double)maxval * maxval *
      iSize;  // MAXi的平方除以MSE，MSE=diff*diff/size，因此psnr等价于
              // （MAXi的平方*size）/（diff*diff）
  double psnr =
      (uiSSDtemp
           ? 10.0 * log10(fRefValue / (double)uiSSDtemp)
           : 999.99);  //这块没看懂为什么有个999.99，大概是设个INF，防止读取时为空吧
  return psnr;
}

double computePSNR_tflite_hbd(uint16_t *dgd, uint16_t *src, int height,
                              int width, int dgd_stride, int src_stride,
                              int bit_depth) {
  int iSize = height * width;  // m*nd

  double uiSSDtemp = 0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int iDiff = (int)(dgd[x] - src[x]);  //遍历获取diff
      uiSSDtemp += iDiff * iDiff;          // diff的平方
    }
    dgd += dgd_stride;  //算上图像的stride边界 边界没有内容，但是会在图像中
    src += src_stride;
  }
  int maxval;
  maxval = ((1 << bit_depth) - 1);
  const double fRefValue =
      (double)maxval * maxval *
      iSize;  // MAXi的平方除以MSE，MSE=diff*diff/size，因此psnr等价于
              // （MAXi的平方*size）/（diff*diff）
  double psnr =
      (uiSSDtemp
           ? 10.0 * log10(fRefValue / (double)uiSSDtemp)
           : 999.99);  //这块没看懂为什么有个999.99，大概是设个INF，防止读取时为空吧
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
      //只算规则块的index，MAX_SIZE的已经处理
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
// guided conv unet with attention intra
int qp255_quadtree_model_quantSet_intra[] = { 460, 356, -5, 1 };
int qp205_quadtree_model_quantSet_intra[] = { 26206, 22062, -17, -12 };
int qp175_quadtree_model_quantSet_intra[] = { 326, 626, 5, -8 };
int qp145_quadtree_model_quantSet_intra[] = { 11729, 21508, 2, -12 };
int qp120_quadtree_model_quantSet_intra[] = { 10913, 9068, -33, -7 };
int qp90_quadtree_model_quantSet_intra[] = { 211238, 307325, -3, 9 };

// guided conv unet with attention inter
int qp255_quadtree_model_quantSet_inter[] = { 821, 645, -18, -6 };
int qp205_quadtree_model_quantSet_inter[] = { 553, 489, 0, -6 };
int qp175_quadtree_model_quantSet_inter[] = { 1457, 1933, -11, -25 };
int qp145_quadtree_model_quantSet_inter[] = { 169214, 80256, -11, -28 };
int qp120_quadtree_model_quantSet_inter[] = { 3395, 3166, -30, -3 };
int qp90_quadtree_model_quantSet_inter[] = { 1920, 14281, -21, -7 };

int *get_quadparm_from_qindex(int qindex, int superres_denom, int is_intra_only,
                              int is_luma, int cnn_index) {
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
  return 0;
}
#if CONFIG_CNN_GUIDED_QUADTREE
int64_t count_guided_quad_bits(struct AV1Common *cm) {
  int64_t bits = 0;
  for (int i = 0; i < cm->cur_quad_info.split_info_length; i++) {
    if (i % 2 == 0) {
      if (cm->cur_quad_info.split_info[i].split == 0 &&
          cm->cur_quad_info.split_info[i + 1].split == 1) {
        // bits += (4 * 2 * 4 + 2);
        bits += (4 * 2 * 4 + 2) << AV1_PROB_COST_SHIFT;
        // printf("it'split\n");
      } else if (cm->cur_quad_info.split_info[i].split == 1 &&
                 cm->cur_quad_info.split_info[i + 1].split == 1) {
        // bits += (4 * 2 * 2 + 2);
        bits += (4 * 2 * 2 + 2) << AV1_PROB_COST_SHIFT;
        // printf("it's horz\n");
      } else if (cm->cur_quad_info.split_info[i].split == 1 &&
                 cm->cur_quad_info.split_info[i + 1].split == 0) {
        // bits += (4 * 2 * 2 + 2);
        bits += (4 * 2 * 2 + 2) << AV1_PROB_COST_SHIFT;
        // printf("it's vert\n");
      } else if (cm->cur_quad_info.split_info[i].split == 0 &&
                 cm->cur_quad_info.split_info[i + 1].split == 0) {
        // bits += (4 * 2 + 2);
        bits += (4 * 2 + 2) << AV1_PROB_COST_SHIFT;
        // printf("it's all\n");
      }
    }
  }
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
#endif  // CONFIG_CNN_GUIDED_QUADTREE
