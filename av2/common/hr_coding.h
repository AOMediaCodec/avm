/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_COMMON_HR_CODING_H_
#define AVM_AV2_COMMON_HR_CODING_H_

#include "config/avm_config.h"

#include "avm_dsp/bitreader.h"
#include "avm_dsp/bitwriter.h"
#include "av2/common/blockd.h"

/*!\brief Calculate the code length of an input symbol when it is coded
 * using Exp-Golomb code with order k
 *
 * \ingroup coefficient_coding
 *
 * This function derives the code length of the input symbol (level) when
 * it is coded using Exp-Golomb code with order k
 *
 * \param[in]    level          input symbol (i.e., a portion of the
 *                              coefficient level)
 * \param[in]    k              order of the Exp-Golomb distribution
 *
 */
static INLINE int get_exp_golomb_length(int level, int k) {
  return 2 * get_msb(level + (1 << k)) + 1 - k;
}

/*!\brief Calculate the difference in code length when coding the input integer
 * symbol as is vs. reduced by 1, using Exp-Golomb code with order k
 *
 * \ingroup coefficient_coding
 *
 * This function computes the difference in code length when coding the input
 * integer symbol as is vs. reduced by 1, using Exp-Golomb code with order k
 *
 * \param[in]    level          input integer symbol, corresponding to the
 *                              portion of the coefficient level coded using
 *                              Exp-Golomb code
 *
 * \param[in]    k              order of the Exp-Golomb distribution
 *
 * \param[out]   diff           the difference in output code length between
 *                              coding the symbol as is or reduce it by 1
 *
 */
static INLINE int get_exp_golomb_length_diff(int level, int k, int *diff) {
  if (level == 0) {
    *diff = k + 1;
    return k + 1;
  }

  int x = level + (1 << k);
  *diff = (x & (x - 1)) == 0 ? 2 : 0;
  return 2 * get_msb(x) + 1 - k;
}

/*!\brief Derive the Rice parameter m based on input context value
 *
 * \ingroup coefficient_coding
 *
 * This function derives the adaptive Rice parameter m by comparing the input
 * context value against a table of different thresholds. For an input context
 * value ctx between two threshold values in the table: t[i] <= ctx < t[i+1],
 * the Rice parameter is chosen as m = i + 1.
 *
 * \param[in]    ctx         context value
 *
 */
int get_adaptive_param(int ctx);

/*!\brief Calculate the code length of an input symbol when it is coded
 * using Truncated Rice
 *
 * \ingroup coefficient_coding
 *
 * This function derives the code length when the input symbol is coded
 * using Truncated Rice, with Rice parameter m and Exp-Golomb of order k
 *
 * \param[in]    level          input integer symbol (i.e., coefficient level)
 * \param[in]        m          Rice parameter
 * \param[in]        k          order of the Exp-Golomb distribution
 * \param[in]      cmax         maximum length for the unary prefix
 *
 */
int get_truncated_rice_length(int level, int m, int k, int cmax);

/*!\brief Calculate the difference in code length when coding an input integer
 * symbol as is vs. reduced by 1 using Truncated Rice
 *
 * \ingroup coefficient_coding
 *
 * This function derives the difference in code length when the input integer
 * symbol is coded as is vs. reduced by 1 using Truncated Rice, with Rice
 * parameter m and Exp-Golomb of order k
 *
 * \param[in]    level          input integer symbol (i.e., coefficient level)
 * \param[in]        m          Rice parameter
 * \param[in]        k          order of the Exp-Golomb distribution
 * \param[in]      cmax         maximum length for the unary prefix
 * \param[out]     diff         difference in code length
 */
int get_truncated_rice_length_diff(int level, int m, int k, int cmax,
                                   int *diff);

/*!\brief Calculate the code length of an input symbol when it is coded
 * using the adaptive high-range (HR) coding scheme
 *
 * \ingroup coefficient_coding
 *
 * This function derives the code length of the input symbol when it is coded
 * using the adaptive high-range (HR) coding scheme, where its parameters are
 * derived based on input context ctx.
 *
 * \param[in]    level          input integer symbol (i.e., coefficient level)
 * \param[in]    ctx            context value
 *
 */
int get_adaptive_hr_length(int level, int ctx);

/*!\brief Calculate the difference in code length when coding an input integer
 * symbol as is vs. reduced by 1 using the adaptive high-range (HR) coding
 * scheme
 *
 * \ingroup coefficient_coding
 *
 * This function calculates the difference in code length when the input integer
 * symbol is coded as is vs. reduced by 1, using the adaptive high-range (HR)
 * coding scheme based on Truncated Rice. The Rice parameter m is derived based
 * on input context value by comparing it against a set of thresholds; the
 * Exp-Golomb coding parameters (order k and maximum unary prefix length cmax)
 * are subsequently derived as k=m+1 and cmax = min(m+4, 6), respectively.
 *
 * \param[in]    level          input integer symbol (i.e., coefficient level)
 * \param[in]        m          Rice parameter
 * \param[in]        k          order of the Exp-Golomb distribution
 * \param[in]      cmax         maximum length for the unary prefix
 * \param[out]     diff         difference in code length
 *
 */
int get_adaptive_hr_length_diff(int level, int ctx, int *diff);

#endif  // AVM_AV2_COMMON_HR_CODING_H_
