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
 * \brief This file has the implementation details of the grain table.
 *
 * The file format is an ascii representation for readability and
 * editability. Array parameters are separated from the non-array
 * parameters and prefixed with a few characters to make for easy
 * localization with a parameter set. Each entry is prefixed with "E"
 * and the other parameters are only specified if "update-parms" is
 * non-zero.
 *
 * filmgrn2
 * E <start-time> <end-time> <apply-grain> <random-seed> <update-parms>
 *  p <ar_coeff_lag> <ar_coeff_shift> <grain_scale_shift> ...
 *  sY <num_y_points> <point_0_x> <point_0_y> ...
 *  sCb <num_cb_points> <point_0_x> <point_0_y> ...
 *  sCr <num_cr_points> <point_0_x> <point_0_y> ...
 *  cY <ar_coeff_y_0> ....
 *  cCb <ar_coeff_cb_0> ....
 *  cCr <ar_coeff_cr_0> ....
 * E <start-time> ...
 */
#include <string.h>
#include <stdio.h>
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/grain_table.h"
#include "avm_mem/avm_mem.h"
static const char kFileMagic[8] = { 'f', 'i', 'l', 'm', 'g', 'r', 'n', '2' };
static void grain_table_entry_read(FILE *file,
                                   struct avm_internal_error_info *error_info,
                                   avm_film_grain_table_entry_t *entry) {
  avm_film_grain_t *pars = &entry->params;
  int num_read =
      fscanf(file, "E %" PRId64 " %" PRId64 " %d %hd %d\n", &entry->start_time,
             &entry->end_time, &pars->apply_grain, &pars->random_seed,
             &pars->update_parameters);
  if (num_read == 0 && feof(file)) return;
  if (num_read != 5) {
    avm_internal_error(error_info, AVM_CODEC_ERROR,
                       "Unable to read entry header. Read %d != 5", num_read);
    return;
  }
  if (pars->update_parameters) {
    num_read = fscanf(
        file, "p %d %d %d %d %d %d %d %d %d %d %d %d %d\n", &pars->ar_coeff_lag,
        &pars->ar_coeff_shift, &pars->grain_scale_shift, &pars->scaling_shift,
        &pars->fgm_scale_from_channel0_flag, &pars->overlap_flag,

        &pars->cb_mult, &pars->cb_luma_mult, &pars->cb_offset, &pars->cr_mult,
        &pars->cr_luma_mult, &pars->cr_offset, &pars->block_size);
    if (num_read != 13) {
      avm_internal_error(error_info, AVM_CODEC_ERROR,
                         "Unable to read entry params. Read %d != 13",
                         num_read);
      return;
    }
    if (!fscanf(file, "\tsY %d ", &pars->fgm_points[0])) {
      avm_internal_error(error_info, AVM_CODEC_ERROR,
                         "Unable to read num y points");
      return;
    }
    for (int i = 0; i < pars->fgm_points[0]; ++i) {
      if (2 != fscanf(file, "%d %d", &pars->fgm_scaling_points_0[i][0],
                      &pars->fgm_scaling_points_0[i][1])) {
        avm_internal_error(error_info, AVM_CODEC_ERROR,
                           "Unable to read y scaling points");
        return;
      }
    }
    if (!fscanf(file, "\n\tsCb %d", &pars->fgm_points[1])) {
      avm_internal_error(error_info, AVM_CODEC_ERROR,
                         "Unable to read num cb points");
      return;
    }
    for (int i = 0; i < pars->fgm_points[1]; ++i) {
      if (2 != fscanf(file, "%d %d", &pars->fgm_scaling_points_1[i][0],
                      &pars->fgm_scaling_points_1[i][1])) {
        avm_internal_error(error_info, AVM_CODEC_ERROR,
                           "Unable to read cb scaling points");
        return;
      }
    }
    if (!fscanf(file, "\n\tsCr %d", &pars->fgm_points[2])) {
      avm_internal_error(error_info, AVM_CODEC_ERROR,
                         "Unable to read num cr points");
      return;
    }
    for (int i = 0; i < pars->fgm_points[2]; ++i) {
      if (2 != fscanf(file, "%d %d", &pars->fgm_scaling_points_2[i][0],
                      &pars->fgm_scaling_points_2[i][1])) {
        avm_internal_error(error_info, AVM_CODEC_ERROR,
                           "Unable to read cr scaling points");
        return;
      }
    }

    fscanf(file, "\n\tcY");
    const int n = 2 * pars->ar_coeff_lag * (pars->ar_coeff_lag + 1);
    for (int i = 0; i < n; ++i) {
      if (1 != fscanf(file, "%d", &pars->ar_coeffs_y[i])) {
        avm_internal_error(error_info, AVM_CODEC_ERROR,
                           "Unable to read Y coeffs");
        return;
      }
    }
    fscanf(file, "\n\tcCb");
    for (int i = 0; i <= n; ++i) {
      if (1 != fscanf(file, "%d", &pars->ar_coeffs_cb[i])) {
        avm_internal_error(error_info, AVM_CODEC_ERROR,
                           "Unable to read Cb coeffs");
        return;
      }
    }
    fscanf(file, "\n\tcCr");
    for (int i = 0; i <= n; ++i) {
      if (1 != fscanf(file, "%d", &pars->ar_coeffs_cr[i])) {
        avm_internal_error(error_info, AVM_CODEC_ERROR,
                           "Unable to read Cr coeffs");
        return;
      }
    }
    fscanf(file, "\n");
  }
}

static void grain_table_entry_write(FILE *file,
                                    avm_film_grain_table_entry_t *entry) {
  const avm_film_grain_t *pars = &entry->params;
  fprintf(file, "E %" PRId64 " %" PRId64 " %d %d %d\n", entry->start_time,
          entry->end_time, pars->apply_grain, pars->random_seed,
          pars->update_parameters);
  if (pars->update_parameters) {
    fprintf(file, "\tp %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
            pars->ar_coeff_lag, pars->ar_coeff_shift, pars->grain_scale_shift,
            pars->scaling_shift, pars->fgm_scale_from_channel0_flag,

            pars->overlap_flag, pars->cb_mult, pars->cb_luma_mult,
            pars->cb_offset, pars->cr_mult, pars->cr_luma_mult, pars->cr_offset,
            pars->block_size);
    fprintf(file, "\tsY %d ", pars->fgm_points[0]);
    for (int i = 0; i < pars->fgm_points[0]; ++i) {
      fprintf(file, " %d %d", pars->fgm_scaling_points_0[i][0],
              pars->fgm_scaling_points_0[i][1]);
    }
    fprintf(file, "\n\tsCb %d", pars->fgm_points[1]);
    for (int i = 0; i < pars->fgm_points[1]; ++i) {
      fprintf(file, " %d %d", pars->fgm_scaling_points_1[i][0],
              pars->fgm_scaling_points_1[i][1]);
    }
    fprintf(file, "\n\tsCr %d", pars->fgm_points[2]);
    for (int i = 0; i < pars->fgm_points[2]; ++i) {
      fprintf(file, " %d %d", pars->fgm_scaling_points_2[i][0],
              pars->fgm_scaling_points_2[i][1]);
    }
    fprintf(file, "\n\tcY");
    const int n = 2 * pars->ar_coeff_lag * (pars->ar_coeff_lag + 1);
    for (int i = 0; i < n; ++i) {
      fprintf(file, " %d", pars->ar_coeffs_y[i]);
    }
    fprintf(file, "\n\tcCb");
    for (int i = 0; i <= n; ++i) {
      fprintf(file, " %d", pars->ar_coeffs_cb[i]);
    }
    fprintf(file, "\n\tcCr");
    for (int i = 0; i <= n; ++i) {
      fprintf(file, " %d", pars->ar_coeffs_cr[i]);
    }
    fprintf(file, "\n");
  }
}

void avm_film_grain_table_append(avm_film_grain_table_t *t, int64_t time_stamp,
                                 int64_t end_time,
                                 const avm_film_grain_t *grain) {
  if (!t->tail || memcmp(grain, &t->tail->params, sizeof(*grain))) {
    avm_film_grain_table_entry_t *new_tail = avm_malloc(sizeof(*new_tail));
    memset(new_tail, 0, sizeof(*new_tail));
    if (t->tail) t->tail->next = new_tail;
    if (!t->head) t->head = new_tail;
    t->tail = new_tail;

    new_tail->start_time = time_stamp;
    new_tail->end_time = end_time;
    new_tail->params = *grain;
  } else {
    t->tail->end_time = AVMMAX(t->tail->end_time, end_time);
    t->tail->start_time = AVMMIN(t->tail->start_time, time_stamp);
  }
}

int avm_film_grain_table_lookup(avm_film_grain_table_t *t, int64_t time_stamp,
                                int64_t end_time, int erase,
                                avm_film_grain_t *grain) {
  avm_film_grain_table_entry_t *entry = t->head;
  avm_film_grain_table_entry_t *prev_entry = 0;
  uint16_t random_seed = grain ? grain->random_seed : 0;
  if (grain) memset(grain, 0, sizeof(*grain));

  while (entry) {
    avm_film_grain_table_entry_t *next = entry->next;
    if (time_stamp >= entry->start_time && time_stamp < entry->end_time) {
      if (grain) {
        *grain = entry->params;
        if (time_stamp != 0) grain->random_seed = random_seed;
      }
      if (!erase) return 1;

      const int64_t entry_end_time = entry->end_time;
      if (time_stamp <= entry->start_time && end_time >= entry->end_time) {
        if (t->tail == entry) t->tail = prev_entry;
        if (prev_entry) {
          prev_entry->next = entry->next;
        } else {
          t->head = entry->next;
        }
        avm_free(entry);
      } else if (time_stamp <= entry->start_time &&
                 end_time < entry->end_time) {
        entry->start_time = end_time;
      } else if (time_stamp > entry->start_time &&
                 end_time >= entry->end_time) {
        entry->end_time = time_stamp;
      } else {
        avm_film_grain_table_entry_t *new_entry =
            avm_malloc(sizeof(*new_entry));
        new_entry->next = entry->next;
        new_entry->start_time = end_time;
        new_entry->end_time = entry->end_time;
        new_entry->params = entry->params;
        entry->next = new_entry;
        entry->end_time = time_stamp;
        if (t->tail == entry) t->tail = new_entry;
      }
      // If segments aren't aligned, delete from the beggining of subsequent
      // segments
      if (end_time > entry_end_time) {
        avm_film_grain_table_lookup(t, entry->end_time, end_time, 1, 0);
      }
      return 1;
    }
    prev_entry = entry;
    entry = next;
  }
  return 0;
}

avm_codec_err_t avm_film_grain_table_read(
    avm_film_grain_table_t *t, const char *filename,
    struct avm_internal_error_info *error_info) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    avm_internal_error(error_info, AVM_CODEC_ERROR, "Unable to open %s",
                       filename);
    return error_info->error_code;
  }
  error_info->error_code = AVM_CODEC_OK;

  // Read in one extra character as there should be white space after
  // the header.
  char magic[9];
  if (!fread(magic, 9, 1, file) || memcmp(magic, kFileMagic, 8)) {
    avm_internal_error(error_info, AVM_CODEC_ERROR,
                       "Unable to read (or invalid) file magic");
    fclose(file);
    return error_info->error_code;
  }

  avm_film_grain_table_entry_t *prev_entry = 0;
  while (!feof(file)) {
    avm_film_grain_table_entry_t *entry = avm_malloc(sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    grain_table_entry_read(file, error_info, entry);
    entry->next = 0;

    if (prev_entry) prev_entry->next = entry;
    if (!t->head) t->head = entry;
    t->tail = entry;
    prev_entry = entry;

    if (error_info->error_code != AVM_CODEC_OK) break;
  }

  fclose(file);
  return error_info->error_code;
}

avm_codec_err_t avm_film_grain_table_write(
    const avm_film_grain_table_t *t, const char *filename,
    struct avm_internal_error_info *error_info) {
  error_info->error_code = AVM_CODEC_OK;

  FILE *file = fopen(filename, "wb");
  if (!file) {
    avm_internal_error(error_info, AVM_CODEC_ERROR, "Unable to open file %s",
                       filename);
    return error_info->error_code;
  }

  if (!fwrite(kFileMagic, 8, 1, file)) {
    avm_internal_error(error_info, AVM_CODEC_ERROR,
                       "Unable to write file magic");
    fclose(file);
    return error_info->error_code;
  }

  fprintf(file, "\n");
  avm_film_grain_table_entry_t *entry = t->head;
  while (entry) {
    grain_table_entry_write(file, entry);
    entry = entry->next;
  }
  fclose(file);
  return error_info->error_code;
}

void avm_film_grain_table_free(avm_film_grain_table_t *t) {
  avm_film_grain_table_entry_t *entry = t->head;
  while (entry) {
    avm_film_grain_table_entry_t *next = entry->next;
    avm_free(entry);
    entry = next;
  }
  memset(t, 0, sizeof(*t));
}
