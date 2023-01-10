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

int qp235_quadtree_model_quantSet[] = { 15, 512, -16, -12 };      // unet
int qp210_quadtree_model_quantSet[] = { 20, 500, -20, 3 };        // unet
int qp185_quadtree_model_quantSet[] = { 1024, 16, -4, -14 };      // unet
int qp160_quadtree_model_quantSet[] = { 2048, 256, 0, -25 };      // unet
int qp135_quadtree_model_quantSet[] = { 1024, 832, -33, 17 };     // unet
int qp110_quadtree_model_quantSet[] = { 40960, 20480, 7, -16 };   // unet
int qp85_quadtree_model_quantSet[] = { 12800, 12800, -16, -12 };  // unet

int *get_quadparm_from_qindex(int qindex, int superres_denom, int is_luma,
                              int cnn_index) {
  if (superres_denom == SCALE_NUMERATOR || superres_denom == 10 ||
         superres_denom == 12 || superres_denom == 14 || superres_denom == 16) {  // quadtree
    if (is_luma) {
      if (qindex <= 85) {
        return (cnn_index == 0)   ? qp85_quadtree_model_quantSet
               : (cnn_index == 1) ? qp110_quadtree_model_quantSet
                                  : qp135_quadtree_model_quantSet;

      } else if (qindex <= 110) {
        return (cnn_index == 0)   ? qp110_quadtree_model_quantSet
               : (cnn_index == 1) ? qp85_quadtree_model_quantSet
                                  : qp135_quadtree_model_quantSet;
      } else if (qindex <= 135) {
        return (cnn_index == 0)   ? qp135_quadtree_model_quantSet
               : (cnn_index == 1) ? qp110_quadtree_model_quantSet
                                  : qp160_quadtree_model_quantSet;
      } else if (qindex <= 160) {
        return (cnn_index == 0)   ? qp160_quadtree_model_quantSet
               : (cnn_index == 1) ? qp135_quadtree_model_quantSet
                                  : qp185_quadtree_model_quantSet;
      } else if (qindex <= 185) {
        return (cnn_index == 0)   ? qp185_quadtree_model_quantSet
               : (cnn_index == 1) ? qp160_quadtree_model_quantSet
                                  : qp210_quadtree_model_quantSet;
      } else if (qindex <= 210) {
        return (cnn_index == 0)   ? qp210_quadtree_model_quantSet
               : (cnn_index == 1) ? qp185_quadtree_model_quantSet
                                  : qp235_quadtree_model_quantSet;
      } else {
        return (cnn_index == 0)   ? qp235_quadtree_model_quantSet
               : (cnn_index == 1) ? qp210_quadtree_model_quantSet
                                  : qp185_quadtree_model_quantSet;
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
