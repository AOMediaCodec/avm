# Complete Inline Functions Inventory

This document contains a complete inventory of all inline functions in the `av2/` directory.

## Summary Statistics

- **Total Inline Functions:** 1538
- **Header Files (.h):** 799
- **Source Files (.c):** 739

### Severity Distribution

| Severity | Count | Percentage |
|----------|-------|------------|
| Critical | 12 | 0.8% |
| High | 25 | 1.6% |
| Medium | 67 | 4.4% |
| Low | 149 | 9.7% |
| Info | 1285 | 83.6% |

### Directory Breakdown

| Directory | Header (.h) | Source (.c) | Total |
|-----------|-------------|-------------|-------|
| av2/av2_cx_iface.c | 0 | 2 | 2 |
| av2/av2_dx_iface.c | 0 | 1 | 1 |
| av2/common | 595 | 119 | 714 |
| av2/decoder | 8 | 109 | 117 |
| av2/encoder | 196 | 508 | 704 |

### Top 20 Files by Inline Function Count

| Rank | File | Count | TUs |
|------|------|-------|-----|
| 1 | av2/common/blockd.h | 128 | 30 |
| 2 | av2/common/av2_common_int.h | 125 | 50 |
| 3 | av2/encoder/bitstream.c | 95 | 0 |
| 4 | av2/decoder/decodeframe.c | 88 | 0 |
| 5 | av2/encoder/rdopt.c | 59 | 0 |
| 6 | av2/common/reconinter.h | 52 | 36 |
| 7 | av2/common/mvref_common.h | 51 | 14 |
| 8 | av2/common/txb_common.h | 44 | 9 |
| 9 | av2/encoder/encodetxb.c | 42 | 0 |
| 10 | av2/encoder/encoder.h | 38 | 31 |
| 11 | av2/encoder/mcomp.c | 37 | 0 |
| 12 | av2/common/mvref_common.c | 33 | 0 |
| 13 | av2/encoder/tx_search.c | 33 | 0 |
| 14 | av2/common/mv.h | 32 | 5 |
| 15 | av2/common/pred_common.h | 26 | 20 |
| 16 | av2/encoder/ethread.c | 24 | 0 |
| 17 | av2/encoder/encodeframe.c | 22 | 0 |
| 18 | av2/encoder/partition_search.c | 22 | 0 |
| 19 | av2/common/bru.h | 21 | 15 |
| 20 | av2/encoder/encoder_utils.h | 18 | 4 |

### Severity Rubric

| Severity | Criteria |
|----------|----------|
| Critical | >100 lines in .h, ≥2 TUs |
| High | 50-100 lines in .h, OR >100 lines with 1 TU |
| Medium | 30-50 lines in .h |
| Low | 15-30 lines in .h |
| Info | <15 lines OR in .c file (appropriate usage) |

---

## Complete Function Inventory

| ID | Severity | Function | Location | Lines | TU Count | Proposed Change |
|----|----------|----------|----------|-------|----------|-----------------|
| 1 | Critical | `ensure_mv_buffer` | av2/common/av2_common_int.h:3267 | 106 | 50 | Move to .c file |
| 2 | Critical | `set_mi_row_col` | av2/common/av2_common_int.h:3531 | 122 | 50 | Move to .c file |
| 3 | Critical | `init_allowed_partitions_for_signaling` | av2/common/av2_common_int.h:4491 | 143 | 50 | Move to .c file |
| 4 | Critical | `cluster_active_regions` | av2/common/bru.h:400 | 151 | 15 | Move to .c file |
| 5 | Critical | `bru_active_map_validation` | av2/common/bru.h:553 | 134 | 15 | Move to .c file |
| 6 | Critical | `av2_is_dv_in_local_range` | av2/common/mvref_common.h:634 | 227 | 14 | Move to .c file |
| 7 | Critical | `av2_is_dv_valid` | av2/common/mvref_common.h:862 | 133 | 14 | Move to .c file |
| 8 | Critical | `dealloc_compressor_data` | av2/encoder/encoder_alloc.h:145 | 159 | 5 | Move to .c file |
| 9 | Critical | `highbd_set_var_fns` | av2/encoder/encoder_utils.h:531 | 291 | 4 | Move to .c file |
| 10 | Critical | `recode_loop_update_q` | av2/encoder/rc_utils.h:227 | 165 | 3 | Move to .c file |
| 11 | Critical | `prune_ref_by_selective_ref_frame` | av2/encoder/rdopt.h:207 | 104 | 16 | Move to .c file |
| 12 | Critical | `set_mode_eval_params` | av2/encoder/rdopt_utils.h:283 | 109 | 6 | Move to .c file |
| 13 | High | `partition_plane_context_helper` | av2/common/av2_common_int.h:3778 | 70 | 50 | Move to .c file |
| 14 | High | `is_partition_implied_at_boundary` | av2/common/av2_common_int.h:4292 | 67 | 50 | Move to .c file |
| 15 | High | `get_tx_size` | av2/common/av2_common_int.h:4782 | 51 | 50 | Move to .c file |
| 16 | High | `get_partition` | av2/common/av2_common_int.h:5064 | 71 | 50 | Move to .c file |
| 17 | High | `opfl_allowed_cur_refs_bsize` | av2/common/av2_common_int.h:5349 | 52 | 50 | Move to .c file |
| 18 | High | `motion_mode_allowed` | av2/common/av2_common_int.h:5585 | 83 | 50 | Move to .c file |
| 19 | High | `is_sub_partition_chroma_ref` | av2/common/blockd.h:1256 | 53 | 30 | Move to .c file |
| 20 | High | `set_chroma_ref_offset_size` | av2/common/blockd.h:1310 | 89 | 30 | Move to .c file |
| 21 | High | `av2_get_tx_type` | av2/common/blockd.h:3063 | 98 | 30 | Move to .c file |
| 22 | High | `compute_directions` | av2/common/cdef_block_simd.h:62 | 68 | 5 | Move to .c file |
| 23 | High | `is_mhccp_allowed` | av2/common/cfl.h:96 | 53 | 14 | Move to .c file |
| 24 | High | `sdp_chroma_part_from_luma` | av2/common/common_data.h:331 | 60 | 5 | Move to .c file |
| 25 | High | `get_warp_motion_vector` | av2/common/mvref_common.h:216 | 51 | 14 | Move to .c file |
| 26 | High | `get_converter` | av2/common/pc_wiener_filters.h:1412 | 75 | 0 | Consider moving to .c file |
| 27 | High | `init_ref_map_pair` | av2/common/pred_common.h:25 | 59 | 20 | Move to .c file |
| 28 | High | `get_txb_ctx` | av2/common/txb_common.h:751 | 97 | 9 | Move to .c file |
| 29 | High | `av2_scale_warp_model` | av2/common/warped_motion.h:270 | 72 | 11 | Move to .c file |
| 30 | High | `check_mv_precision` | av2/encoder/encodemv.h:110 | 57 | 15 | Move to .c file |
| 31 | High | `av2_set_seq_tile_info` | av2/encoder/encoder_utils.h:969 | 54 | 4 | Move to .c file |
| 32 | High | `av2_set_tile_info` | av2/encoder/encoder_utils.h:1024 | 68 | 4 | Move to .c file |
| 33 | High | `intra_mode_info_cost_y` | av2/encoder/intra_mode_search_utils.h:205 | 63 | 2 | Move to .c file |
| 34 | High | `model_rd_for_sb` | av2/encoder/model_rd.h:151 | 50 | 4 | Move to .c file |
| 35 | High | `model_rd_for_sb_with_curvfit` | av2/encoder/model_rd.h:202 | 60 | 4 | Move to .c file |
| 36 | High | `config_target_level` | av2/encoder/rc_utils.h:37 | 53 | 3 | Move to .c file |
| 37 | High | `store_winner_mode_stats` | av2/encoder/rdopt_utils.h:421 | 81 | 6 | Move to .c file |
| 38 | Medium | `get_free_fb` | av2/common/av2_common_int.h:2961 | 33 | 50 | Remove INLINE keyword |
| 39 | Medium | `get_secondary_reference_frame_idx` | av2/common/av2_common_int.h:3152 | 35 | 50 | Remove INLINE keyword |
| 40 | Medium | `fetch_spatial_neighbors` | av2/common/av2_common_int.h:3473 | 30 | 50 | Remove INLINE keyword |
| 41 | Medium | `partition_plane_context` | av2/common/av2_common_int.h:3849 | 35 | 50 | Remove INLINE keyword |
| 42 | Medium | `av2_zero_above_context` | av2/common/av2_common_int.h:3885 | 43 | 50 | Remove INLINE keyword |
| 43 | Medium | `set_mi_offsets` | av2/common/av2_common_int.h:3961 | 32 | 50 | Remove INLINE keyword |
| 44 | Medium | `av2_reset_refmv_bank` | av2/common/av2_common_int.h:4028 | 49 | 50 | Remove INLINE keyword |
| 45 | Medium | `is_sdp_share_partition` | av2/common/av2_common_int.h:4109 | 31 | 50 | Remove INLINE keyword |
| 46 | Medium | `is_luma_chroma_share_same_partition` | av2/common/av2_common_int.h:4141 | 30 | 50 | Remove INLINE keyword |
| 47 | Medium | `is_cfl_allowed_for_this_luma_partition` | av2/common/av2_common_int.h:4179 | 39 | 50 | Remove INLINE keyword |
| 48 | Medium | `check_is_chroma_size_valid` | av2/common/av2_common_int.h:4249 | 38 | 50 | Remove INLINE keyword |
| 49 | Medium | `av2_get_normative_forced_partition_type` | av2/common/av2_common_int.h:4365 | 31 | 50 | Remove INLINE keyword |
| 50 | Medium | `get_chroma_ref_offsets` | av2/common/av2_common_int.h:4397 | 47 | 50 | Remove INLINE keyword |
| 51 | Medium | `get_tx_partition_sizes` | av2/common/av2_common_int.h:4878 | 37 | 50 | Remove INLINE keyword |
| 52 | Medium | `get_mi_location_from_collocated_mi` | av2/common/av2_common_int.h:5717 | 48 | 50 | Remove INLINE keyword |
| 53 | Medium | `compound_ref0_mode` | av2/common/blockd.h:104 | 36 | 30 | Remove INLINE keyword |
| 54 | Medium | `compound_ref1_mode` | av2/common/blockd.h:141 | 36 | 30 | Remove INLINE keyword |
| 55 | Medium | `is_ext_partition_allowed_at_bsize` | av2/common/blockd.h:833 | 30 | 30 | Remove INLINE keyword |
| 56 | Medium | `is_ext_partition_allowed` | av2/common/blockd.h:866 | 39 | 30 | Remove INLINE keyword |
| 57 | Medium | `get_h_partition_subsize` | av2/common/blockd.h:1060 | 43 | 30 | Remove INLINE keyword |
| 58 | Medium | `is_extended_sdp_allowed` | av2/common/blockd.h:1178 | 35 | 30 | Remove INLINE keyword |
| 59 | Medium | `have_nz_chroma_ref_offset` | av2/common/blockd.h:1216 | 35 | 30 | Remove INLINE keyword |
| 60 | Medium | `set_chroma_ref_info` | av2/common/blockd.h:1400 | 46 | 30 | Remove INLINE keyword |
| 61 | Medium | `block_signals_sec_tx_type` | av2/common/blockd.h:2995 | 31 | 30 | Remove INLINE keyword |
| 62 | Medium | `av2_get_block_dimensions` | av2/common/blockd.h:3370 | 43 | 30 | Remove INLINE keyword |
| 63 | Medium | `bru_is_valid_inter` | av2/common/bru.h:40 | 31 | 15 | Remove INLINE keyword |
| 64 | Medium | `realloc_bru_info` | av2/common/bru.h:73 | 36 | 15 | Remove INLINE keyword |
| 65 | Medium | `ARD_BFS` | av2/common/bru.h:356 | 42 | 15 | Remove INLINE keyword |
| 66 | Medium | `is_cfl_allowed` | av2/common/cfl.h:57 | 38 | 14 | Remove INLINE keyword |
| 67 | Medium | `get_larger_sqr_bsize` | av2/common/enums.h:411 | 31 | 32 | Remove INLINE keyword |
| 68 | Medium | `full_pel_lower_mv_precision` | av2/common/mv.h:316 | 34 | 5 | Remove INLINE keyword |
| 69 | Medium | `get_block_position` | av2/common/mvref_common.h:52 | 35 | 14 | Remove INLINE keyword |
| 70 | Medium | `get_int_warp_mv_for_fb` | av2/common/mvref_common.h:270 | 36 | 14 | Remove INLINE keyword |
| 71 | Medium | `av2_get_neighbor_warp_model` | av2/common/mvref_common.h:1075 | 46 | 14 | Remove INLINE keyword |
| 72 | Medium | `get_dir_rank` | av2/common/pred_common.h:179 | 32 | 20 | Remove INLINE keyword |
| 73 | Medium | `av2_get_spatial_seg_pred` | av2/common/pred_common.h:306 | 45 | 20 | Remove INLINE keyword |
| 74 | Medium | `divide_and_round_array` | av2/common/reconinter.h:568 | 33 | 36 | Remove INLINE keyword |
| 75 | Medium | `is_refinemv_allowed_reference` | av2/common/reconinter.h:691 | 49 | 36 | Remove INLINE keyword |
| 76 | Medium | `clamp_mv_to_umv_border_sb` | av2/common/reconinter.h:970 | 36 | 36 | Remove INLINE keyword |
| 77 | Medium | `av2_allow_bawp` | av2/common/reconinter.h:1188 | 34 | 36 | Remove INLINE keyword |
| 78 | Medium | `highbd_dc_predictor_subsampled` | av2/common/reconintra.h:148 | 36 | 28 | Remove INLINE keyword |
| 79 | Medium | `wide_angle_mapping` | av2/common/reconintra.h:223 | 35 | 28 | Remove INLINE keyword |
| 80 | Medium | `get_br_lf_ctx` | av2/common/txb_common.h:200 | 31 | 9 | Remove INLINE keyword |
| 81 | Medium | `get_nz_map_ctx_from_stats_lf` | av2/common/txb_common.h:421 | 47 | 9 | Remove INLINE keyword |
| 82 | Medium | `get_lower_levels_ctx_general` | av2/common/txb_common.h:698 | 31 | 9 | Remove INLINE keyword |
| 83 | Medium | `resolve_divisor_32_CfL` | av2/common/warped_motion.h:97 | 49 | 11 | Remove INLINE keyword |
| 84 | Medium | `is_rect_tx_allowed_bsize` | av2/encoder/block.h:1668 | 36 | 6 | Remove INLINE keyword |
| 85 | Medium | `update_wedge_mode_cdf` | av2/encoder/encodeframe_utils.h:169 | 36 | 7 | Remove INLINE keyword |
| 86 | Medium | `av2_check_newmv_joint_nonzero` | av2/encoder/encodemv.h:75 | 34 | 15 | Remove INLINE keyword |
| 87 | Medium | `get_component_name` | av2/encoder/encoder.h:1877 | 32 | 31 | Remove INLINE keyword |
| 88 | Medium | `av2_print_partition_stats` | av2/encoder/encoder.h:3466 | 35 | 31 | Remove INLINE keyword |
| 89 | Medium | `alloc_compressor_data` | av2/encoder/encoder_alloc.h:63 | 30 | 5 | Remove INLINE keyword |
| 90 | Medium | `alloc_util_frame_buffers` | av2/encoder/encoder_alloc.h:320 | 34 | 5 | Remove INLINE keyword |
| 91 | Medium | `get_hist_bin_idx` | av2/encoder/intra_mode_search_utils.h:87 | 36 | 2 | Consider removing INLINE |
| 92 | Medium | `generate_hog_hbd` | av2/encoder/intra_mode_search_utils.h:125 | 33 | 2 | Consider removing INLINE |
| 93 | Medium | `av2_set_subpel_mv_search_range` | av2/encoder/mcomp.h:632 | 43 | 11 | Remove INLINE keyword |
| 94 | Medium | `model_rd_from_sse` | av2/encoder/model_rd.h:73 | 30 | 4 | Remove INLINE keyword |
| 95 | Medium | `model_rd_with_curvfit` | av2/encoder/model_rd.h:106 | 44 | 4 | Remove INLINE keyword |
| 96 | Medium | `set_offsets_for_motion_search` | av2/encoder/partition_strategy.h:170 | 39 | 5 | Remove INLINE keyword |
| 97 | Medium | `av2_merge_rd_stats` | av2/encoder/rd.h:144 | 31 | 10 | Remove INLINE keyword |
| 98 | Medium | `reset_hash_records` | av2/encoder/rd.h:289 | 31 | 10 | Remove INLINE keyword |
| 99 | Medium | `av2_copy_mbmi_ext_to_mbmi_ext_frame` | av2/encoder/rdopt.h:314 | 42 | 16 | Remove INLINE keyword |
| 100 | Medium | `get_txb_dimensions` | av2/encoder/rdopt_utils.h:60 | 34 | 6 | Remove INLINE keyword |
| 101 | Medium | `get_visible_dimensions` | av2/encoder/rdopt_utils.h:95 | 41 | 6 | Remove INLINE keyword |
| 102 | Medium | `check_txfm_eval` | av2/encoder/rdopt_utils.h:142 | 35 | 6 | Remove INLINE keyword |
| 103 | Medium | `inter_tx_partition_cost` | av2/encoder/tx_search.h:37 | 46 | 5 | Remove INLINE keyword |
| 104 | Medium | `intra_tx_partition_cost` | av2/encoder/tx_search.h:84 | 46 | 5 | Remove INLINE keyword |
| 105 | Low | `av2_get_chroma_format_idc` | av2/common/av2_common_int.h:2897 | 16 | 50 | Consider removing INLINE |
| 106 | Low | `av2_get_chroma_format_idc` | av2/common/av2_common_int.h:2916 | 15 | 50 | Consider removing INLINE |
| 107 | Low | `assign_cur_frame_new_fb` | av2/common/av2_common_int.h:2995 | 22 | 50 | Consider removing INLINE |
| 108 | Low | `setup_default_temporal_layer_dependency_structure` | av2/common/av2_common_int.h:3067 | 16 | 50 | Consider removing INLINE |
| 109 | Low | `is_tlayer_scalable_and_dependent` | av2/common/av2_common_int.h:3084 | 28 | 50 | Consider removing INLINE |
| 110 | Low | `is_mlayer_scalable_and_dependent` | av2/common/av2_common_int.h:3124 | 27 | 50 | Consider removing INLINE |
| 111 | Low | `av2_init_macroblockd` | av2/common/av2_common_int.h:3389 | 29 | 50 | Consider removing INLINE |
| 112 | Low | `set_entropy_context` | av2/common/av2_common_int.h:3419 | 20 | 50 | Consider removing INLINE |
| 113 | Low | `set_plane_n4` | av2/common/av2_common_int.h:3445 | 22 | 50 | Consider removing INLINE |
| 114 | Low | `fetch_spatial_neighbors_with_line_buffer` | av2/common/av2_common_int.h:3503 | 27 | 50 | Consider removing INLINE |
| 115 | Low | `get_lp2tx_ctx` | av2/common/av2_common_int.h:3655 | 16 | 50 | Consider removing INLINE |
| 116 | Low | `get_fsc_mode_ctx` | av2/common/av2_common_int.h:3672 | 15 | 50 | Consider removing INLINE |
| 117 | Low | `update_partition_context` | av2/common/av2_common_int.h:3738 | 15 | 50 | Consider removing INLINE |
| 118 | Low | `set_blk_offsets` | av2/common/av2_common_int.h:3995 | 17 | 50 | Consider removing INLINE |
| 119 | Low | `tree_has_bsize_smaller_than` | av2/common/av2_common_int.h:4092 | 16 | 50 | Consider removing INLINE |
| 120 | Low | `is_cfl_allowed_for_sdp` | av2/common/av2_common_int.h:4219 | 29 | 50 | Consider removing INLINE |
| 121 | Low | `is_chroma_ref_within_boundary` | av2/common/av2_common_int.h:4445 | 17 | 50 | Consider removing INLINE |
| 122 | Low | `only_allowed_partition` | av2/common/av2_common_int.h:4637 | 16 | 50 | Consider removing INLINE |
| 123 | Low | `is_do_split_implied` | av2/common/av2_common_int.h:4654 | 25 | 50 | Consider removing INLINE |
| 124 | Low | `only_allowed_rect_type` | av2/common/av2_common_int.h:4680 | 20 | 50 | Consider removing INLINE |
| 125 | Low | `is_do_ext_partition_implied` | av2/common/av2_common_int.h:4701 | 28 | 50 | Consider removing INLINE |
| 126 | Low | `is_do_uneven_4way_partition_implied` | av2/common/av2_common_int.h:4730 | 26 | 50 | Consider removing INLINE |
| 127 | Low | `get_split4_partition` | av2/common/av2_common_int.h:4936 | 15 | 50 | Consider removing INLINE |
| 128 | Low | `use_tx_partition` | av2/common/av2_common_int.h:5039 | 22 | 50 | Consider removing INLINE |
| 129 | Low | `av2_set_lr_tools` | av2/common/av2_common_int.h:5159 | 16 | 50 | Consider removing INLINE |
| 130 | Low | `is_coded_lossless` | av2/common/av2_common_int.h:5199 | 17 | 50 | Consider removing INLINE |
| 131 | Low | `init_ibp_info` | av2/common/av2_common_int.h:5304 | 19 | 50 | Consider removing INLINE |
| 132 | Low | `get_relative_dist` | av2/common/av2_common_int.h:5329 | 17 | 50 | Consider removing INLINE |
| 133 | Low | `opfl_allowed_cur_pred_mode` | av2/common/av2_common_int.h:5407 | 20 | 50 | Consider removing INLINE |
| 134 | Low | `is_optflow_refinement_enabled` | av2/common/av2_common_int.h:5489 | 28 | 50 | Consider removing INLINE |
| 135 | Low | `av2_get_chroma_subsampling` | av2/common/av2_common_int.h:5768 | 19 | 50 | Consider removing INLINE |
| 136 | Low | `find_effective_tile_params` | av2/common/av2_common_int.h:5790 | 15 | 50 | Consider removing INLINE |
| 137 | Low | `av2_initialize_ci_params` | av2/common/av2_common_int.h:5850 | 15 | 50 | Consider removing INLINE |
| 138 | Low | `range_check_value` | av2/common/av2_txfm.h:65 | 18 | 4 | Review for optimization |
| 139 | Low | `scale_other_mvd` | av2/common/blockd.h:209 | 28 | 30 | Consider removing INLINE |
| 140 | Low | `get_uv_mode` | av2/common/blockd.h:743 | 22 | 30 | Consider removing INLINE |
| 141 | Low | `is_uneven_4way_partition_allowed_at_bsize` | av2/common/blockd.h:908 | 25 | 30 | Consider removing INLINE |
| 142 | Low | `is_uneven_4way_partition_allowed` | av2/common/blockd.h:936 | 21 | 30 | Consider removing INLINE |
| 143 | Low | `rect_type_implied_by_bsize` | av2/common/blockd.h:960 | 27 | 30 | Consider removing INLINE |
| 144 | Low | `get_h_partition_offset_mi_row` | av2/common/blockd.h:1106 | 21 | 30 | Consider removing INLINE |
| 145 | Low | `get_h_partition_offset_mi_col` | av2/common/blockd.h:1130 | 21 | 30 | Consider removing INLINE |
| 146 | Low | `most_probable_group` | av2/common/blockd.h:1586 | 16 | 30 | Consider removing INLINE |
| 147 | Low | `predict_group` | av2/common/blockd.h:1616 | 28 | 30 | Consider removing INLINE |
| 148 | Low | `av2_get_ext_tx_set_type` | av2/common/blockd.h:2515 | 25 | 30 | Consider removing INLINE |
| 149 | Low | `av2_get_txk_type_index` | av2/common/blockd.h:2745 | 29 | 30 | Consider removing INLINE |
| 150 | Low | `update_cctx_array` | av2/common/blockd.h:2799 | 25 | 30 | Consider removing INLINE |
| 151 | Low | `adjust_ext_tx_used_flag` | av2/common/blockd.h:3027 | 28 | 30 | Consider removing INLINE |
| 152 | Low | `is_nontrans_global_motion` | av2/common/blockd.h:3437 | 19 | 30 | Consider removing INLINE |
| 153 | Low | `av2_get_max_eob` | av2/common/blockd.h:3461 | 15 | 30 | Consider removing INLINE |
| 154 | Low | `bru_is_fu_skipped_mbmi` | av2/common/bru.h:127 | 25 | 15 | Consider removing INLINE |
| 155 | Low | `is_ru_bru_skip` | av2/common/bru.h:294 | 29 | 15 | Consider removing INLINE |
| 156 | Low | `fetch_cdef_mi_grid_index` | av2/common/cdef.h:115 | 15 | 9 | Consider removing INLINE |
| 157 | Low | `fold_mul_and_sum` | av2/common/cdef_block_simd.h:26 | 20 | 5 | Consider removing INLINE |
| 158 | Low | `array_reverse_transpose_8x8` | av2/common/cdef_block_simd.h:133 | 28 | 5 | Consider removing INLINE |
| 159 | Low | `store_cfl_required` | av2/common/cfl.h:152 | 19 | 14 | Consider removing INLINE |
| 160 | Low | `config2ncoeffs_select` | av2/common/convolve.h:77 | 17 | 2 | Review for optimization |
| 161 | Low | `get_conv_params_no_round` | av2/common/convolve.h:137 | 29 | 2 | Review for optimization |
| 162 | Low | `get_entropy_context_1d` | av2/common/entropy.h:131 | 15 | 14 | Consider removing INLINE |
| 163 | Low | `opfl_get_comp_idx` | av2/common/entropymode.h:569 | 16 | 15 | Consider removing INLINE |
| 164 | Low | `get_filter_tap` | av2/common/filter.h:247 | 18 | 7 | Consider removing INLINE |
| 165 | Low | `get_phase_from_mv` | av2/common/mv.h:152 | 20 | 5 | Consider removing INLINE |
| 166 | Low | `compression_mv` | av2/common/mv.h:196 | 28 | 5 | Consider removing INLINE |
| 167 | Low | `uncompression_mv` | av2/common/mv.h:239 | 29 | 5 | Consider removing INLINE |
| 168 | Low | `lower_mv_precision` | av2/common/mv.h:286 | 29 | 5 | Consider removing INLINE |
| 169 | Low | `full_pel_lower_mv_precision_one_comp` | av2/common/mv.h:361 | 19 | 5 | Consider removing INLINE |
| 170 | Low | `get_proc_size_and_offset` | av2/common/mvref_common.h:88 | 19 | 14 | Consider removing INLINE |
| 171 | Low | `check_block_position` | av2/common/mvref_common.h:108 | 19 | 14 | Consider removing INLINE |
| 172 | Low | `get_block_position_no_constraint` | av2/common/mvref_common.h:128 | 25 | 14 | Consider removing INLINE |
| 173 | Low | `comb2single` | av2/common/mvref_common.h:367 | 15 | 14 | Consider removing INLINE |
| 174 | Low | `av2_collect_neighbors_ref_counts` | av2/common/mvref_common.h:509 | 15 | 14 | Consider removing INLINE |
| 175 | Low | `get_amvd_context` | av2/common/mvref_common.h:547 | 16 | 14 | Consider removing INLINE |
| 176 | Low | `is_bv_valid_for_morph` | av2/common/mvref_common.h:996 | 15 | 14 | Consider removing INLINE |
| 177 | Low | `derive_row_mv_tpl_offset` | av2/common/mvref_common.h:1233 | 17 | 14 | Consider removing INLINE |
| 178 | Low | `get_tip_mv` | av2/common/mvref_common.h:1262 | 29 | 14 | Consider removing INLINE |
| 179 | Low | `derive_non_tip_mode_smvp_from_tip` | av2/common/mvref_common.h:1292 | 21 | 14 | Consider removing INLINE |
| 180 | Low | `is_skip_mode_allowed` | av2/common/pred_common.h:236 | 24 | 20 | Consider removing INLINE |
| 181 | Low | `set_skip_mode_ref_frame` | av2/common/pred_common.h:261 | 23 | 20 | Consider removing INLINE |
| 182 | Low | `get_segment_id` | av2/common/pred_common.h:285 | 20 | 20 | Consider removing INLINE |
| 183 | Low | `get_comp_group_idx_context` | av2/common/pred_common.h:375 | 26 | 20 | Consider removing INLINE |
| 184 | Low | `is_cctx_allowed` | av2/common/pred_common.h:462 | 20 | 20 | Consider removing INLINE |
| 185 | Low | `allow_warp_parameter_signaling` | av2/common/reconinter.h:299 | 15 | 36 | Consider removing INLINE |
| 186 | Low | `get_cwp_coding_idx` | av2/common/reconinter.h:316 | 23 | 36 | Consider removing INLINE |
| 187 | Low | `get_joint_mvd_base_ref_list` | av2/common/reconinter.h:352 | 17 | 36 | Consider removing INLINE |
| 188 | Low | `highbd_inter_predictor` | av2/common/reconinter.h:428 | 21 | 36 | Consider removing INLINE |
| 189 | Low | `is_any_mv_refinement_allowed_in_tip` | av2/common/reconinter.h:758 | 16 | 36 | Consider removing INLINE |
| 190 | Low | `is_tip_block_with_mv_refinement` | av2/common/reconinter.h:776 | 26 | 36 | Consider removing INLINE |
| 191 | Low | `use_border_aware_compound` | av2/common/reconinter.h:1013 | 19 | 36 | Consider removing INLINE |
| 192 | Low | `setup_pred_plane` | av2/common/reconinter.h:1045 | 23 | 36 | Consider removing INLINE |
| 193 | Low | `setup_pred_planes_for_tip` | av2/common/reconinter.h:1079 | 20 | 36 | Consider removing INLINE |
| 194 | Low | `av2_is_interp_needed` | av2/common/reconinter.h:1114 | 17 | 36 | Consider removing INLINE |
| 195 | Low | `is_mvd_sign_derive_allowed` | av2/common/reconinter.h:1314 | 23 | 36 | Consider removing INLINE |
| 196 | Low | `av2_allow_intrabc` | av2/common/reconintra.h:85 | 16 | 28 | Consider removing INLINE |
| 197 | Low | `set_default_wienerns` | av2/common/restoration.h:424 | 16 | 3 | Review for optimization |
| 198 | Low | `skip_sym_bit` | av2/common/restoration.h:679 | 21 | 3 | Review for optimization |
| 199 | Low | `get_top_stripe_idx_in_tile` | av2/common/restoration.h:702 | 16 | 3 | Review for optimization |
| 200 | Low | `segfeatures_copy` | av2/common/seg_common.h:80 | 15 | 20 | Consider removing INLINE |
| 201 | Low | `get_tip_block_width_with_same_mv` | av2/common/tip.h:54 | 28 | 8 | Consider removing INLINE |
| 202 | Low | `get_br_ctx_chroma` | av2/common/txb_common.h:162 | 16 | 9 | Consider removing INLINE |
| 203 | Low | `get_br_ctx` | av2/common/txb_common.h:235 | 19 | 9 | Consider removing INLINE |
| 204 | Low | `get_sign_skip` | av2/common/txb_common.h:295 | 15 | 9 | Consider removing INLINE |
| 205 | Low | `get_nz_mag_lf` | av2/common/txb_common.h:354 | 23 | 9 | Consider removing INLINE |
| 206 | Low | `get_nz_mag` | av2/common/txb_common.h:380 | 24 | 9 | Consider removing INLINE |
| 207 | Low | `get_nz_map_ctx_from_stats` | av2/common/txb_common.h:486 | 23 | 9 | Consider removing INLINE |
| 208 | Low | `get_lower_levels_ctx_2d_chroma` | av2/common/txb_common.h:574 | 17 | 9 | Consider removing INLINE |
| 209 | Low | `get_lower_levels_ctx_lf_2d` | av2/common/txb_common.h:594 | 25 | 9 | Consider removing INLINE |
| 210 | Low | `get_lower_levels_ctx_2d` | av2/common/txb_common.h:628 | 20 | 9 | Consider removing INLINE |
| 211 | Low | `resolve_divisor_64` | av2/common/warped_motion.h:64 | 16 | 11 | Consider removing INLINE |
| 212 | Low | `resolve_divisor_32` | av2/common/warped_motion.h:81 | 15 | 11 | Consider removing INLINE |
| 213 | Low | `decrease_ref_count` | av2/decoder/decoder.h:613 | 19 | 12 | Consider removing INLINE |
| 214 | Low | `check_ref_count_status_dec` | av2/decoder/decoder.h:639 | 26 | 12 | Consider removing INLINE |
| 215 | Low | `set_max_min_partition_size` | av2/encoder/encodeframe_utils.h:240 | 27 | 7 | Consider removing INLINE |
| 216 | Low | `get_true_pyr_level` | av2/encoder/encoder.h:108 | 15 | 31 | Consider removing INLINE |
| 217 | Low | `get_start_tok` | av2/encoder/encoder.h:3244 | 15 | 31 | Consider removing INLINE |
| 218 | Low | `find_partition_size` | av2/encoder/encoder.h:3356 | 16 | 31 | Consider removing INLINE |
| 219 | Low | `enforce_max_ref_frames` | av2/encoder/encoder.h:3391 | 28 | 31 | Consider removing INLINE |
| 220 | Low | `check_ref_count_status_enc` | av2/encoder/encoder.h:3542 | 24 | 31 | Consider removing INLINE |
| 221 | Low | `alloc_context_buffers_ext` | av2/encoder/encoder_alloc.h:40 | 22 | 5 | Consider removing INLINE |
| 222 | Low | `realloc_segmentation_maps` | av2/encoder/encoder_alloc.h:94 | 21 | 5 | Consider removing INLINE |
| 223 | Low | `alloc_compound_type_rd_buffers` | av2/encoder/encoder_alloc.h:116 | 18 | 5 | Consider removing INLINE |
| 224 | Low | `realloc_and_scale_source` | av2/encoder/encoder_alloc.h:355 | 24 | 5 | Consider removing INLINE |
| 225 | Low | `set_mb_mi` | av2/encoder/encoder_utils.h:46 | 25 | 4 | Review for optimization |
| 226 | Low | `enc_free_mi` | av2/encoder/encoder_utils.h:72 | 17 | 4 | Review for optimization |
| 227 | Low | `refresh_reference_frames` | av2/encoder/encoder_utils.h:883 | 25 | 4 | Review for optimization |
| 228 | Low | `update_subgop_stats` | av2/encoder/encoder_utils.h:909 | 16 | 4 | Review for optimization |
| 229 | Low | `av2_cost_skip_txb` | av2/encoder/encodetxb.h:565 | 21 | 14 | Consider removing INLINE |
| 230 | Low | `prune_intra_mode_with_hog` | av2/encoder/intra_mode_search_utils.h:159 | 28 | 2 | Review for optimization |
| 231 | Low | `av2_set_mv_search_method` | av2/encoder/mcomp.h:252 | 23 | 11 | Consider removing INLINE |
| 232 | Low | `av2_is_fullmv_in_range` | av2/encoder/mcomp.h:352 | 15 | 11 | Consider removing INLINE |
| 233 | Low | `is_valid_sign_mvd_single` | av2/encoder/mcomp.h:604 | 21 | 11 | Consider removing INLINE |
| 234 | Low | `init_mv_cost_params` | av2/encoder/mcomp.h:684 | 16 | 11 | Consider removing INLINE |
| 235 | Low | `init_simple_motion_search_mvs` | av2/encoder/partition_strategy.h:210 | 15 | 5 | Consider removing INLINE |
| 236 | Low | `use_auto_max_partition` | av2/encoder/partition_strategy.h:253 | 16 | 5 | Consider removing INLINE |
| 237 | Low | `check_wienerns_eq` | av2/encoder/pickrst.h:25 | 19 | 6 | Consider removing INLINE |
| 238 | Low | `check_wienerns_bank_eq` | av2/encoder/pickrst.h:45 | 26 | 6 | Consider removing INLINE |
| 239 | Low | `lcg_pick` | av2/encoder/random.h:59 | 21 | 7 | Consider removing INLINE |
| 240 | Low | `recode_loop_test` | av2/encoder/rc_utils.h:107 | 27 | 3 | Review for optimization |
| 241 | Low | `get_regulated_q_overshoot` | av2/encoder/rc_utils.h:162 | 22 | 3 | Review for optimization |
| 242 | Low | `get_regulated_q_undershoot` | av2/encoder/rc_utils.h:185 | 19 | 3 | Review for optimization |
| 243 | Low | `av2_init_rd_stats` | av2/encoder/rd.h:94 | 24 | 10 | Consider removing INLINE |
| 244 | Low | `av2_invalid_rd_stats` | av2/encoder/rd.h:119 | 24 | 10 | Consider removing INLINE |
| 245 | Low | `get_rd_opt_coeff_thresh` | av2/encoder/rd.h:267 | 20 | 10 | Consider removing INLINE |
| 246 | Low | `av2_copy_usable_ref_mv_stack_and_weight` | av2/encoder/rdopt.h:178 | 27 | 16 | Consider removing INLINE |
| 247 | Low | `is_winner_mode_processing_enabled` | av2/encoder/rdopt_utils.h:190 | 27 | 6 | Consider removing INLINE |
| 248 | Low | `set_tx_size_search_method` | av2/encoder/rdopt_utils.h:218 | 25 | 6 | Consider removing INLINE |
| 249 | Low | `set_tx_domain_dist_params` | av2/encoder/rdopt_utils.h:258 | 23 | 6 | Consider removing INLINE |
| 250 | Low | `store_cfl_required_rdo` | av2/encoder/rdopt_utils.h:395 | 19 | 6 | Consider removing INLINE |
| 251 | Low | `get_token_alloc` | av2/encoder/tokenize.h:98 | 15 | 14 | Consider removing INLINE |
| 252 | Low | `alloc_token_info` | av2/encoder/tokenize.h:115 | 20 | 14 | Consider removing INLINE |
| 253 | Low | `tx_size_cost` | av2/encoder/tx_search.h:131 | 15 | 5 | Consider removing INLINE |
| 254 | Info | `gcd` | av2/av2_cx_iface.c:595 | 10 | 1 | Keep as-is (local) |
| 255 | Info | `reduce_ratio` | av2/av2_cx_iface.c:606 | 5 | 1 | Keep as-is (local) |
| 256 | Info | `check_resync` | av2/av2_dx_iface.c:473 | 7 | 1 | Keep as-is (local) |
| 257 | Info | `free_cdef_linebuf_conditional` | av2/common/alloccommon.c:69 | 10 | 1 | Keep as-is (local) |
| 258 | Info | `free_cdef_bufs_conditional` | av2/common/alloccommon.c:82 | 17 | 1 | Keep as-is (local) |
| 259 | Info | `free_cdef_bufs` | av2/common/alloccommon.c:101 | 9 | 1 | Keep as-is (local) |
| 260 | Info | `free_cdef_row_sync` | av2/common/alloccommon.c:112 | 20 | 1 | Keep as-is (local) |
| 261 | Info | `alloc_cdef_linebuf` | av2/common/alloccommon.c:159 | 10 | 1 | Keep as-is (local) |
| 262 | Info | `alloc_cdef_bufs` | av2/common/alloccommon.c:171 | 13 | 1 | Keep as-is (local) |
| 263 | Info | `alloc_cdef_row_sync` | av2/common/alloccommon.c:186 | 19 | 1 | Keep as-is (local) |
| 264 | Info | `av2_get_max_mlayer_cnt_from_profile` | av2/common/annexA.c:136 | 4 | 1 | Keep as-is (local) |
| 265 | Info | `get_ref_frame` | av2/common/av2_common_int.h:2954 | 6 | 50 | Keep as-is (small utility) |
| 266 | Info | `assign_frame_buffer_p` | av2/common/av2_common_int.h:3020 | 13 | 50 | Keep as-is (small utility) |
| 267 | Info | `assign_output_frame_buffer_p` | av2/common/av2_common_int.h:3036 | 6 | 50 | Keep as-is (small utility) |
| 268 | Info | `frame_is_intra_only` | av2/common/av2_common_int.h:3043 | 4 | 50 | Keep as-is (small utility) |
| 269 | Info | `is_inter_sdp_chroma` | av2/common/av2_common_int.h:3049 | 6 | 50 | Keep as-is (small utility) |
| 270 | Info | `frame_is_sframe` | av2/common/av2_common_int.h:3056 | 3 | 50 | Keep as-is (small utility) |
| 271 | Info | `get_ref_frame_map_idx` | av2/common/av2_common_int.h:3060 | 6 | 50 | Keep as-is (small utility) |
| 272 | Info | `setup_default_embedded_layer_dependency_structure` | av2/common/av2_common_int.h:3113 | 10 | 50 | Keep as-is (small utility) |
| 273 | Info | `avg_primary_secondary_references` | av2/common/av2_common_int.h:3188 | 13 | 50 | Keep as-is (small utility) |
| 274 | Info | `get_ref_frame_map_idx_res_indep` | av2/common/av2_common_int.h:3202 | 6 | 50 | Keep as-is (small utility) |
| 275 | Info | `get_ref_frame_buf_res_indep` | av2/common/av2_common_int.h:3209 | 6 | 50 | Keep as-is (small utility) |
| 276 | Info | `get_ref_frame_buf` | av2/common/av2_common_int.h:3216 | 8 | 50 | Keep as-is (small utility) |
| 277 | Info | `get_ref_scale_factors_const` | av2/common/av2_common_int.h:3227 | 8 | 50 | Keep as-is (small utility) |
| 278 | Info | `get_ref_scale_factors` | av2/common/av2_common_int.h:3236 | 8 | 50 | Keep as-is (small utility) |
| 279 | Info | `get_primary_ref_frame_buf` | av2/common/av2_common_int.h:3245 | 14 | 50 | Keep as-is (small utility) |
| 280 | Info | `frame_might_allow_ref_frame_mvs` | av2/common/av2_common_int.h:3261 | 5 | 50 | Keep as-is (small utility) |
| 281 | Info | `av2_num_planes` | av2/common/av2_common_int.h:3376 | 3 | 50 | Keep as-is (small utility) |
| 282 | Info | `av2_init_above_context` | av2/common/av2_common_int.h:3380 | 8 | 50 | Keep as-is (small utility) |
| 283 | Info | `calc_mi_size` | av2/common/av2_common_int.h:3440 | 4 | 50 | Keep as-is (small utility) |
| 284 | Info | `is_at_sb_top_boundary` | av2/common/av2_common_int.h:3469 | 3 | 50 | Keep as-is (small utility) |
| 285 | Info | `get_mhccp_dir_cdf` | av2/common/av2_common_int.h:3689 | 8 | 50 | Keep as-is (small utility) |
| 286 | Info | `get_fsc_mode_cdf` | av2/common/av2_common_int.h:3698 | 9 | 50 | Keep as-is (small utility) |
| 287 | Info | `get_mrl_index_ctx` | av2/common/av2_common_int.h:3708 | 10 | 50 | Keep as-is (small utility) |
| 288 | Info | `get_multi_line_mrl_index_ctx` | av2/common/av2_common_int.h:3719 | 10 | 50 | Keep as-is (small utility) |
| 289 | Info | `update_ext_partition_context` | av2/common/av2_common_int.h:3754 | 9 | 50 | Keep as-is (small utility) |
| 290 | Info | `get_intra_region_context` | av2/common/av2_common_int.h:3764 | 13 | 50 | Keep as-is (small utility) |
| 291 | Info | `av2_zero_left_context` | av2/common/av2_common_int.h:3929 | 4 | 50 | Keep as-is (small utility) |
| 292 | Info | `get_mi_grid_idx` | av2/common/av2_common_int.h:3946 | 4 | 50 | Keep as-is (small utility) |
| 293 | Info | `get_alloc_mi_idx` | av2/common/av2_common_int.h:3951 | 8 | 50 | Keep as-is (small utility) |
| 294 | Info | `is_sdp_enabled_in_keyframe` | av2/common/av2_common_int.h:4078 | 4 | 50 | Keep as-is (small utility) |
| 295 | Info | `is_bsize_above_decoupled_thresh` | av2/common/av2_common_int.h:4086 | 3 | 50 | Keep as-is (small utility) |
| 296 | Info | `is_valid_partition_in_mixed_region` | av2/common/av2_common_int.h:4466 | 8 | 50 | Keep as-is (small utility) |
| 297 | Info | `txfm_partition_update` | av2/common/av2_common_int.h:4757 | 11 | 50 | Keep as-is (small utility) |
| 298 | Info | `get_sqr_tx_size` | av2/common/av2_common_int.h:4769 | 12 | 50 | Keep as-is (small utility) |
| 299 | Info | `get_tx_partition_one_size` | av2/common/av2_common_int.h:4919 | 12 | 50 | Keep as-is (small utility) |
| 300 | Info | `get_vert_and_horz_group` | av2/common/av2_common_int.h:4956 | 4 | 50 | Keep as-is (small utility) |
| 301 | Info | `get_vert_or_horz_group` | av2/common/av2_common_int.h:4965 | 4 | 50 | Keep as-is (small utility) |
| 302 | Info | `coding_block_disallows_tx_partitioning` | av2/common/av2_common_int.h:4970 | 4 | 50 | Keep as-is (small utility) |
| 303 | Info | `allow_tx_horz_split` | av2/common/av2_common_int.h:4975 | 8 | 50 | Keep as-is (small utility) |
| 304 | Info | `allow_tx_vert_split` | av2/common/av2_common_int.h:4984 | 8 | 50 | Keep as-is (small utility) |
| 305 | Info | `allow_tx_horz4_split` | av2/common/av2_common_int.h:4993 | 8 | 50 | Keep as-is (small utility) |
| 306 | Info | `allow_tx_vert4_split` | av2/common/av2_common_int.h:5002 | 8 | 50 | Keep as-is (small utility) |
| 307 | Info | `allow_tx_horzM_split` | av2/common/av2_common_int.h:5011 | 13 | 50 | Keep as-is (small utility) |
| 308 | Info | `allow_tx_vertM_split` | av2/common/av2_common_int.h:5025 | 13 | 50 | Keep as-is (small utility) |
| 309 | Info | `av2_set_frame_sb_size` | av2/common/av2_common_int.h:5136 | 11 | 50 | Keep as-is (small utility) |
| 310 | Info | `set_sb_size` | av2/common/av2_common_int.h:5148 | 8 | 50 | Keep as-is (small utility) |
| 311 | Info | `av2_get_sb_info` | av2/common/av2_common_int.h:5176 | 9 | 50 | Keep as-is (small utility) |
| 312 | Info | `av2_set_sb_info` | av2/common/av2_common_int.h:5186 | 9 | 50 | Keep as-is (small utility) |
| 313 | Info | `is_valid_seq_level_idx` | av2/common/av2_common_int.h:5217 | 10 | 50 | Keep as-is (small utility) |
| 314 | Info | `init_ibp_info_per_mode` | av2/common/av2_common_int.h:5293 | 9 | 50 | Keep as-is (small utility) |
| 315 | Info | `disable_opfl_for_16x16_tip_ref` | av2/common/av2_common_int.h:5430 | 7 | 50 | Keep as-is (small utility) |
| 316 | Info | `disable_opfl_for_tip_direct` | av2/common/av2_common_int.h:5440 | 8 | 50 | Keep as-is (small utility) |
| 317 | Info | `get_tip_bsize_from_bw_bh` | av2/common/av2_common_int.h:5450 | 14 | 50 | Keep as-is (small utility) |
| 318 | Info | `get_unit_bsize_for_tip_ref` | av2/common/av2_common_int.h:5466 | 9 | 50 | Keep as-is (small utility) |
| 319 | Info | `get_unit_bsize_for_tip_frame` | av2/common/av2_common_int.h:5477 | 10 | 50 | Keep as-is (small utility) |
| 320 | Info | `is_this_mv_precision_compliant` | av2/common/av2_common_int.h:5520 | 8 | 50 | Keep as-is (small utility) |
| 321 | Info | `is_warp_mode` | av2/common/av2_common_int.h:5529 | 3 | 50 | Keep as-is (small utility) |
| 322 | Info | `is_compound_warp_causal_allowed` | av2/common/av2_common_int.h:5558 | 12 | 50 | Keep as-is (small utility) |
| 323 | Info | `is_warp_newmv_allowed` | av2/common/av2_common_int.h:5571 | 13 | 50 | Keep as-is (small utility) |
| 324 | Info | `disable_pcwiener_filters_in_framefilters` | av2/common/av2_common_int.h:5671 | 5 | 50 | Keep as-is (small utility) |
| 325 | Info | `is_new_nearmv_pred_mode_disallowed` | av2/common/av2_common_int.h:5677 | 7 | 50 | Keep as-is (small utility) |
| 326 | Info | `compute_log2` | av2/common/av2_common_int.h:5688 | 9 | 50 | Keep as-is (small utility) |
| 327 | Info | `av2_compute_allowed_tiles_log2` | av2/common/av2_common_int.h:5699 | 7 | 50 | Keep as-is (small utility) |
| 328 | Info | `is_reduced_tx_set_used` | av2/common/av2_common_int.h:5707 | 7 | 50 | Keep as-is (small utility) |
| 329 | Info | `is_frame_tile_config_reuse_eligible` | av2/common/av2_common_int.h:5806 | 7 | 50 | Keep as-is (small utility) |
| 330 | Info | `is_frame_seg_config_reuse_eligible` | av2/common/av2_common_int.h:5814 | 10 | 50 | Keep as-is (small utility) |
| 331 | Info | `find_effective_seg_params` | av2/common/av2_common_int.h:5825 | 13 | 50 | Keep as-is (small utility) |
| 332 | Info | `derive_output_order_idx` | av2/common/av2_common_int.h:5840 | 7 | 50 | Keep as-is (small utility) |
| 333 | Info | `check_tip_edge` | av2/common/av2_loopfilter.c:342 | 22 | 1 | Keep as-is (local) |
| 334 | Info | `check_opfl_edge` | av2/common/av2_loopfilter.c:366 | 23 | 1 | Keep as-is (local) |
| 335 | Info | `check_rfmv_edge` | av2/common/av2_loopfilter.c:391 | 36 | 1 | Keep as-is (local) |
| 336 | Info | `check_sub_pu_edge` | av2/common/av2_loopfilter.c:429 | 48 | 1 | Keep as-is (local) |
| 337 | Info | `set_tip_filter_length` | av2/common/av2_loopfilter.c:1095 | 45 | 1 | Keep as-is (local) |
| 338 | Info | `setup_tip_dst_plane` | av2/common/av2_loopfilter.c:1223 | 14 | 1 | Keep as-is (local) |
| 339 | Info | `clamp_value` | av2/common/av2_txfm.h:58 | 6 | 4 | Keep as-is (small utility) |
| 340 | Info | `round_shift` | av2/common/av2_txfm.h:84 | 4 | 4 | Keep as-is (small utility) |
| 341 | Info | `highbd_clip_pixel_add` | av2/common/av2_txfm.h:89 | 4 | 4 | Keep as-is (small utility) |
| 342 | Info | `num_sampled_pc_wiener_filters` | av2/common/blockd.c:412 | 8 | 1 | Keep as-is (local) |
| 343 | Info | `is_thin_4xn_nx4_block` | av2/common/blockd.h:76 | 6 | 30 | Keep as-is (small utility) |
| 344 | Info | `is_comp_ref_allowed` | av2/common/blockd.h:83 | 4 | 30 | Keep as-is (small utility) |
| 345 | Info | `is_inter_mode` | av2/common/blockd.h:88 | 3 | 30 | Keep as-is (small utility) |
| 346 | Info | `is_inter_singleref_mode` | av2/common/blockd.h:97 | 3 | 30 | Keep as-is (small utility) |
| 347 | Info | `is_inter_compound_mode` | av2/common/blockd.h:100 | 3 | 30 | Keep as-is (small utility) |
| 348 | Info | `is_joint_mvd_coding_mode` | av2/common/blockd.h:179 | 3 | 30 | Keep as-is (small utility) |
| 349 | Info | `have_nearmv_in_inter_mode` | av2/common/blockd.h:183 | 5 | 30 | Keep as-is (small utility) |
| 350 | Info | `have_nearmv_newmv_in_inter_mode` | av2/common/blockd.h:189 | 5 | 30 | Keep as-is (small utility) |
| 351 | Info | `have_newmv_in_each_reference` | av2/common/blockd.h:195 | 4 | 30 | Keep as-is (small utility) |
| 352 | Info | `is_joint_amvd_coding_mode` | av2/common/blockd.h:201 | 4 | 30 | Keep as-is (small utility) |
| 353 | Info | `have_newmv_in_inter_mode` | av2/common/blockd.h:238 | 6 | 30 | Keep as-is (small utility) |
| 354 | Info | `have_drl_index` | av2/common/blockd.h:244 | 3 | 30 | Keep as-is (small utility) |
| 355 | Info | `is_masked_compound_type` | av2/common/blockd.h:248 | 3 | 30 | Keep as-is (small utility) |
| 356 | Info | `get_partition_plane_start` | av2/common/blockd.h:684 | 3 | 30 | Keep as-is (small utility) |
| 357 | Info | `get_partition_plane_end` | av2/common/blockd.h:689 | 3 | 30 | Keep as-is (small utility) |
| 358 | Info | `is_intrabc_block` | av2/common/blockd.h:739 | 3 | 30 | Keep as-is (small utility) |
| 359 | Info | `is_inter_ref_frame` | av2/common/blockd.h:766 | 4 | 30 | Keep as-is (small utility) |
| 360 | Info | `is_tip_ref_frame` | av2/common/blockd.h:771 | 3 | 30 | Keep as-is (small utility) |
| 361 | Info | `is_inter_block` | av2/common/blockd.h:775 | 4 | 30 | Keep as-is (small utility) |
| 362 | Info | `get_intra_mode` | av2/common/blockd.h:782 | 10 | 30 | Keep as-is (small utility) |
| 363 | Info | `get_mvd_from_ref_mv` | av2/common/blockd.h:794 | 7 | 30 | Keep as-is (small utility) |
| 364 | Info | `update_mv_component_from_mvd` | av2/common/blockd.h:803 | 13 | 30 | Keep as-is (small utility) |
| 365 | Info | `is_square_block` | av2/common/blockd.h:818 | 3 | 30 | Keep as-is (small utility) |
| 366 | Info | `is_tall_block` | av2/common/blockd.h:823 | 3 | 30 | Keep as-is (small utility) |
| 367 | Info | `is_wide_block` | av2/common/blockd.h:828 | 3 | 30 | Keep as-is (small utility) |
| 368 | Info | `is_square_split_eligible` | av2/common/blockd.h:989 | 5 | 30 | Keep as-is (small utility) |
| 369 | Info | `get_rect_part_type` | av2/common/blockd.h:997 | 11 | 30 | Keep as-is (small utility) |
| 370 | Info | `has_second_ref` | av2/common/blockd.h:1009 | 3 | 30 | Keep as-is (small utility) |
| 371 | Info | `has_second_drl` | av2/common/blockd.h:1014 | 5 | 30 | Keep as-is (small utility) |
| 372 | Info | `get_ref_mv_idx` | av2/common/blockd.h:1021 | 3 | 30 | Keep as-is (small utility) |
| 373 | Info | `is_global_mv_block` | av2/common/blockd.h:1027 | 9 | 30 | Keep as-is (small utility) |
| 374 | Info | `is_partition_point` | av2/common/blockd.h:1037 | 3 | 30 | Keep as-is (small utility) |
| 375 | Info | `get_partition_subsize` | av2/common/blockd.h:1046 | 11 | 30 | Keep as-is (small utility) |
| 376 | Info | `is_partition_valid` | av2/common/blockd.h:1152 | 6 | 30 | Keep as-is (small utility) |
| 377 | Info | `initialize_chroma_ref_info` | av2/common/blockd.h:1159 | 10 | 30 | Keep as-is (small utility) |
| 378 | Info | `is_bsize_allowed_for_extended_sdp` | av2/common/blockd.h:1170 | 7 | 30 | Keep as-is (small utility) |
| 379 | Info | `mi_to_pixel_loc` | av2/common/blockd.h:1448 | 8 | 30 | Keep as-is (small utility) |
| 380 | Info | `num_dictionary_slots` | av2/common/blockd.h:1546 | 4 | 30 | Keep as-is (small utility) |
| 381 | Info | `prev_filters_begin` | av2/common/blockd.h:1551 | 4 | 30 | Keep as-is (small utility) |
| 382 | Info | `prev_filters_end` | av2/common/blockd.h:1556 | 1 | 30 | Keep as-is (small utility) |
| 383 | Info | `reference_filters_begin` | av2/common/blockd.h:1558 | 3 | 30 | Keep as-is (small utility) |
| 384 | Info | `is_match_allowed` | av2/common/blockd.h:1562 | 7 | 30 | Keep as-is (small utility) |
| 385 | Info | `max_num_base_filters` | av2/common/blockd.h:1570 | 3 | 30 | Keep as-is (small utility) |
| 386 | Info | `index_to_group` | av2/common/blockd.h:1604 | 9 | 30 | Keep as-is (small utility) |
| 387 | Info | `get_group_base` | av2/common/blockd.h:1646 | 8 | 30 | Keep as-is (small utility) |
| 388 | Info | `predict_within_group` | av2/common/blockd.h:1658 | 9 | 30 | Keep as-is (small utility) |
| 389 | Info | `is_cur_buf_hbd` | av2/common/blockd.h:2326 | 3 | 30 | Keep as-is (small utility) |
| 390 | Info | `get_buf_by_bd` | av2/common/blockd.h:2330 | 5 | 30 | Keep as-is (small utility) |
| 391 | Info | `replace_adst_by_ddt` | av2/common/blockd.h:2358 | 8 | 30 | Keep as-is (small utility) |
| 392 | Info | `is_rect_tx` | av2/common/blockd.h:2389 | 1 | 30 | Keep as-is (small utility) |
| 393 | Info | `block_signals_txsize` | av2/common/blockd.h:2391 | 3 | 30 | Keep as-is (small utility) |
| 394 | Info | `get_ext_tx_set` | av2/common/blockd.h:2549 | 6 | 30 | Keep as-is (small utility) |
| 395 | Info | `get_ext_tx_types` | av2/common/blockd.h:2556 | 7 | 30 | Keep as-is (small utility) |
| 396 | Info | `tx_size_from_tx_mode` | av2/common/blockd.h:2564 | 10 | 30 | Keep as-is (small utility) |
| 397 | Info | `get_default_tx_type` | av2/common/blockd.h:2579 | 12 | 30 | Keep as-is (small utility) |
| 398 | Info | `get_plane_block_size` | av2/common/blockd.h:2594 | 8 | 30 | Keep as-is (small utility) |
| 399 | Info | `skip_parsing_recon` | av2/common/blockd.h:2603 | 5 | 30 | Keep as-is (small utility) |
| 400 | Info | `max_block_wide` | av2/common/blockd.h:2609 | 13 | 30 | Keep as-is (small utility) |
| 401 | Info | `max_block_high` | av2/common/blockd.h:2623 | 12 | 30 | Keep as-is (small utility) |
| 402 | Info | `get_plane_tx_unit_height` | av2/common/blockd.h:2636 | 11 | 30 | Keep as-is (small utility) |
| 403 | Info | `get_plane_tx_unit_width` | av2/common/blockd.h:2648 | 11 | 30 | Keep as-is (small utility) |
| 404 | Info | `av2_get_sdp_idx` | av2/common/blockd.h:2664 | 8 | 30 | Keep as-is (small utility) |
| 405 | Info | `get_bsize_base` | av2/common/blockd.h:2682 | 11 | 30 | Keep as-is (small utility) |
| 406 | Info | `get_mb_plane_block_size` | av2/common/blockd.h:2694 | 9 | 30 | Keep as-is (small utility) |
| 407 | Info | `get_bsize_base_from_tree_type` | av2/common/blockd.h:2707 | 12 | 30 | Keep as-is (small utility) |
| 408 | Info | `get_mb_plane_block_size_from_tree_type` | av2/common/blockd.h:2720 | 9 | 30 | Keep as-is (small utility) |
| 409 | Info | `av2_get_txb_size_index` | av2/common/blockd.h:2731 | 12 | 30 | Keep as-is (small utility) |
| 410 | Info | `update_txk_array` | av2/common/blockd.h:2776 | 14 | 30 | Keep as-is (small utility) |
| 411 | Info | `get_chroma_mi_offsets` | av2/common/blockd.h:2793 | 5 | 30 | Keep as-is (small utility) |
| 412 | Info | `av2_get_cctx_type` | av2/common/blockd.h:2825 | 7 | 30 | Keep as-is (small utility) |
| 413 | Info | `disable_secondary_tx_type` | av2/common/blockd.h:2852 | 3 | 30 | Keep as-is (small utility) |
| 414 | Info | `disable_primary_tx_type` | av2/common/blockd.h:2858 | 3 | 30 | Keep as-is (small utility) |
| 415 | Info | `get_primary_tx_type` | av2/common/blockd.h:2864 | 3 | 30 | Keep as-is (small utility) |
| 416 | Info | `get_idx_from_txtype_for_large_txfm` | av2/common/blockd.h:2888 | 5 | 30 | Keep as-is (small utility) |
| 417 | Info | `is_dct_type` | av2/common/blockd.h:2901 | 8 | 30 | Keep as-is (small utility) |
| 418 | Info | `get_txtype_from_idx_for_large_txfm` | av2/common/blockd.h:2951 | 11 | 30 | Keep as-is (small utility) |
| 419 | Info | `get_secondary_tx_type` | av2/common/blockd.h:2966 | 3 | 30 | Keep as-is (small utility) |
| 420 | Info | `set_secondary_tx_type` | av2/common/blockd.h:2970 | 3 | 30 | Keep as-is (small utility) |
| 421 | Info | `get_secondary_tx_set` | av2/common/blockd.h:2977 | 4 | 30 | Keep as-is (small utility) |
| 422 | Info | `set_secondary_tx_set` | av2/common/blockd.h:2986 | 4 | 30 | Keep as-is (small utility) |
| 423 | Info | `av2_get_adjusted_tx_size` | av2/common/blockd.h:3165 | 14 | 30 | Keep as-is (small utility) |
| 424 | Info | `get_lossless_tx_size` | av2/common/blockd.h:3180 | 11 | 30 | Keep as-is (small utility) |
| 425 | Info | `av2_get_max_uv_txsize` | av2/common/blockd.h:3192 | 8 | 30 | Keep as-is (small utility) |
| 426 | Info | `av2_get_max_uv_txsize_adjusted` | av2/common/blockd.h:3204 | 9 | 30 | Keep as-is (small utility) |
| 427 | Info | `av2_get_tx_size` | av2/common/blockd.h:3214 | 13 | 30 | Keep as-is (small utility) |
| 428 | Info | `is_interintra_mode` | av2/common/blockd.h:3285 | 4 | 30 | Keep as-is (small utility) |
| 429 | Info | `is_tip_allowed_bsize` | av2/common/blockd.h:3290 | 10 | 30 | Keep as-is (small utility) |
| 430 | Info | `is_interintra_allowed_bsize` | av2/common/blockd.h:3301 | 4 | 30 | Keep as-is (small utility) |
| 431 | Info | `is_interintra_allowed_mode` | av2/common/blockd.h:3306 | 3 | 30 | Keep as-is (small utility) |
| 432 | Info | `is_interintra_allowed_ref` | av2/common/blockd.h:3310 | 4 | 30 | Keep as-is (small utility) |
| 433 | Info | `is_interintra_allowed` | av2/common/blockd.h:3315 | 7 | 30 | Keep as-is (small utility) |
| 434 | Info | `is_interintra_pred` | av2/common/blockd.h:3323 | 4 | 30 | Keep as-is (small utility) |
| 435 | Info | `get_vartx_max_txsize` | av2/common/blockd.h:3328 | 9 | 30 | Keep as-is (small utility) |
| 436 | Info | `is_motion_variation_allowed_bsize` | av2/common/blockd.h:3338 | 12 | 30 | Keep as-is (small utility) |
| 437 | Info | `is_motion_variation_allowed_compound` | av2/common/blockd.h:3351 | 4 | 30 | Keep as-is (small utility) |
| 438 | Info | `av2_allow_palette` | av2/common/blockd.h:3356 | 8 | 30 | Keep as-is (small utility) |
| 439 | Info | `get_plane_type` | av2/common/blockd.h:3457 | 3 | 30 | Keep as-is (small utility) |
| 440 | Info | `get_partition_subtree_const` | av2/common/blockd.h:3477 | 7 | 30 | Keep as-is (small utility) |
| 441 | Info | `is_cwp_allowed` | av2/common/blockd.h:3486 | 11 | 30 | Keep as-is (small utility) |
| 442 | Info | `get_cwp_idx` | av2/common/blockd.h:3498 | 4 | 30 | Keep as-is (small utility) |
| 443 | Info | `free_bru_info` | av2/common/bru.h:110 | 7 | 15 | Keep as-is (small utility) |
| 444 | Info | `is_sb_start_mi` | av2/common/bru.h:118 | 7 | 15 | Keep as-is (small utility) |
| 445 | Info | `bru_is_sb_active` | av2/common/bru.h:154 | 8 | 15 | Keep as-is (small utility) |
| 446 | Info | `bru_is_sb_available` | av2/common/bru.h:164 | 8 | 15 | Keep as-is (small utility) |
| 447 | Info | `enc_get_cur_sb_active_mode` | av2/common/bru.h:174 | 11 | 15 | Keep as-is (small utility) |
| 448 | Info | `set_active_map` | av2/common/bru.h:187 | 12 | 15 | Keep as-is (small utility) |
| 449 | Info | `ard_create_queue` | av2/common/bru.h:224 | 6 | 15 | Keep as-is (small utility) |
| 450 | Info | `ard_is_queue_empty` | av2/common/bru.h:231 | 1 | 15 | Keep as-is (small utility) |
| 451 | Info | `ard_enqueue` | av2/common/bru.h:234 | 12 | 15 | Keep as-is (small utility) |
| 452 | Info | `ard_dequeue` | av2/common/bru.h:248 | 14 | 15 | Keep as-is (small utility) |
| 453 | Info | `is_valid_ard_location` | av2/common/bru.h:264 | 3 | 15 | Keep as-is (small utility) |
| 454 | Info | `is_bru_not_active_and_not_on_partial_border` | av2/common/bru.h:280 | 12 | 15 | Keep as-is (small utility) |
| 455 | Info | `bru_get_num_of_active_region` | av2/common/bru.h:324 | 6 | 15 | Keep as-is (small utility) |
| 456 | Info | `init_bru_params` | av2/common/bru.h:331 | 7 | 15 | Keep as-is (small utility) |
| 457 | Info | `fill_rect` | av2/common/cdef.c:123 | 8 | 1 | Keep as-is (local) |
| 458 | Info | `copy_rect` | av2/common/cdef.c:132 | 8 | 1 | Keep as-is (local) |
| 459 | Info | `cdef_filter_fb` | av2/common/cdef.c:142 | 9 | 1 | Keep as-is (local) |
| 460 | Info | `cdef_init_fb_col` | av2/common/cdef.c:303 | 17 | 1 | Keep as-is (local) |
| 461 | Info | `sign_int` | av2/common/cdef.h:64 | 1 | 9 | Keep as-is (small utility) |
| 462 | Info | `constrain` | av2/common/cdef.h:66 | 7 | 9 | Keep as-is (small utility) |
| 463 | Info | `av2_get_cdef_transmitted_index` | av2/common/cdef.h:86 | 11 | 9 | Keep as-is (small utility) |
| 464 | Info | `adjust_strength` | av2/common/cdef_block.c:261 | 5 | 1 | Keep as-is (local) |
| 465 | Info | `avm_av2_cdef_find_dir` | av2/common/cdef_block.c:271 | 27 | 1 | Keep as-is (local) |
| 466 | Info | `fold_mul_and_sum_avx2` | av2/common/cdef_block_avx2.c:23 | 28 | 1 | Keep as-is (local) |
| 467 | Info | `hsum4_avx2` | av2/common/cdef_block_avx2.c:53 | 14 | 1 | Keep as-is (local) |
| 468 | Info | `compute_directions_avx2` | av2/common/cdef_block_avx2.c:70 | 79 | 1 | Keep as-is (local) |
| 469 | Info | `array_reverse_transpose_8x8_avx2` | av2/common/cdef_block_avx2.c:152 | 29 | 1 | Keep as-is (local) |
| 470 | Info | `hsum4` | av2/common/cdef_block_simd.h:47 | 12 | 5 | Keep as-is (small utility) |
| 471 | Info | `mul_fixed32_adapt` | av2/common/cfl.c:33 | 37 | 1 | Keep as-is (local) |
| 472 | Info | `cfl_pad` | av2/common/cfl.c:120 | 31 | 1 | Keep as-is (local) |
| 473 | Info | `cfl_idx_to_alpha` | av2/common/cfl.c:174 | 9 | 1 | Keep as-is (local) |
| 474 | Info | `cfl_subsampling_hbd` | av2/common/cfl.c:748 | 11 | 1 | Keep as-is (local) |
| 475 | Info | `floorLog2Uint64` | av2/common/cfl.c:998 | 30 | 1 | Keep as-is (local) |
| 476 | Info | `ilog2_32` | av2/common/cfl.h:23 | 9 | 14 | Keep as-is (small utility) |
| 477 | Info | `derive_linear_parameters_alpha` | av2/common/cfl.h:36 | 10 | 14 | Keep as-is (small utility) |
| 478 | Info | `derive_linear_parameters_beta` | av2/common/cfl.h:49 | 6 | 14 | Keep as-is (small utility) |
| 479 | Info | `get_scaled_luma_q0` | av2/common/cfl.h:185 | 4 | 14 | Keep as-is (small utility) |
| 480 | Info | `get_cfl_pred_type` | av2/common/cfl.h:190 | 4 | 14 | Keep as-is (small utility) |
| 481 | Info | `get_unsigned_bits` | av2/common/common.h:49 | 3 | 28 | Keep as-is (small utility) |
| 482 | Info | `is_bsize_geq` | av2/common/common_data.h:805 | 7 | 5 | Keep as-is (small utility) |
| 483 | Info | `is_bsize_gt` | av2/common/common_data.h:813 | 7 | 5 | Keep as-is (small utility) |
| 484 | Info | `highbd_horz_scalar_product` | av2/common/convolve.c:575 | 6 | 1 | Keep as-is (local) |
| 485 | Info | `highbd_vert_scalar_product` | av2/common/convolve.c:582 | 7 | 1 | Keep as-is (local) |
| 486 | Info | `clip_base` | av2/common/convolve.h:43 | 4 | 2 | Keep as-is (small utility) |
| 487 | Info | `config2ncoeffs` | av2/common/convolve.h:65 | 11 | 2 | Keep as-is (small utility) |
| 488 | Info | `is_uneven_wtd_comp_avg` | av2/common/convolve.h:125 | 5 | 2 | Keep as-is (small utility) |
| 489 | Info | `init_conv_params` | av2/common/convolve.h:131 | 5 | 2 | Keep as-is (small utility) |
| 490 | Info | `get_conv_params` | av2/common/convolve.h:167 | 4 | 2 | Keep as-is (small utility) |
| 491 | Info | `get_conv_params_wiener` | av2/common/convolve.h:172 | 12 | 2 | Keep as-is (small utility) |
| 492 | Info | `av2_cost_symbol` | av2/common/cost.h:33 | 11 | 15 | Keep as-is (small utility) |
| 493 | Info | `reset_cdf_symbol_counter` | av2/common/entropy.c:82 | 8 | 1 | Keep as-is (local) |
| 494 | Info | `reset_nmv_counter` | av2/common/entropy.c:102 | 27 | 1 | Keep as-is (local) |
| 495 | Info | `combine_entropy_contexts` | av2/common/entropy.h:126 | 4 | 14 | Keep as-is (small utility) |
| 496 | Info | `get_entropy_context` | av2/common/entropy.h:147 | 11 | 14 | Keep as-is (small utility) |
| 497 | Info | `get_txsize_entropy_ctx` | av2/common/entropy.h:159 | 4 | 14 | Keep as-is (small utility) |
| 498 | Info | `get_eob_plane_ctx` | av2/common/entropy.h:166 | 3 | 14 | Keep as-is (small utility) |
| 499 | Info | `swap_color_order` | av2/common/entropymode.c:32 | 7 | 1 | Keep as-is (local) |
| 500 | Info | `derive_color_index_ctx` | av2/common/entropymode.c:40 | 80 | 1 | Keep as-is (local) |
| 501 | Info | `cumulative_avg_cdf_symbol` | av2/common/entropymode.c:290 | 12 | 1 | Keep as-is (local) |
| 502 | Info | `shift_cdf_symbol` | av2/common/entropymode.c:678 | 11 | 1 | Keep as-is (local) |
| 503 | Info | `av2_tx_type_to_idx` | av2/common/entropymode.h:525 | 6 | 15 | Keep as-is (small utility) |
| 504 | Info | `av2_tx_idx_to_type` | av2/common/entropymode.h:532 | 6 | 15 | Keep as-is (small utility) |
| 505 | Info | `inter_single_mode_ctx` | av2/common/entropymode.h:543 | 3 | 15 | Keep as-is (small utility) |
| 506 | Info | `av2_drl_ctx` | av2/common/entropymode.h:548 | 1 | 15 | Keep as-is (small utility) |
| 507 | Info | `mv_joint_vertical` | av2/common/entropymv.h:39 | 3 | 9 | Keep as-is (small utility) |
| 508 | Info | `mv_joint_horizontal` | av2/common/entropymv.h:43 | 3 | 9 | Keep as-is (small utility) |
| 509 | Info | `av2_unswitchable_filter` | av2/common/filter.h:58 | 3 | 7 | Keep as-is (small utility) |
| 510 | Info | `av2_get_interp_filter_params_with_block_size` | av2/common/filter.h:214 | 6 | 7 | Keep as-is (small utility) |
| 511 | Info | `av2_get_interp_filter_kernel` | av2/common/filter.h:221 | 9 | 7 | Keep as-is (small utility) |
| 512 | Info | `av2_get_interp_filter_subpel_kernel` | av2/common/filter.h:231 | 4 | 7 | Keep as-is (small utility) |
| 513 | Info | `av2_get_filter` | av2/common/filter.h:236 | 10 | 7 | Keep as-is (small utility) |
| 514 | Info | `get_ref_dst_max` | av2/common/gdf.c:464 | 19 | 1 | Keep as-is (local) |
| 515 | Info | `is_allow_gdf` | av2/common/gdf.h:96 | 3 | 6 | Keep as-is (small utility) |
| 516 | Info | `is_gdf_enabled` | av2/common/gdf.h:102 | 3 | 6 | Keep as-is (small utility) |
| 517 | Info | `gdf_block_adjust_and_validate` | av2/common/gdf.h:114 | 8 | 6 | Keep as-is (small utility) |
| 518 | Info | `gdf_intra_get_idx` | av2/common/gdf_block_avx2.c:299 | 6 | 1 | Keep as-is (local) |
| 519 | Info | `gdf_inter_get_idx` | av2/common/gdf_block_avx2.c:306 | 11 | 1 | Keep as-is (local) |
| 520 | Info | `get_exp_golomb_length` | av2/common/hr_coding.h:35 | 3 | 3 | Keep as-is (small utility) |
| 521 | Info | `get_exp_golomb_length_diff` | av2/common/hr_coding.h:57 | 10 | 3 | Keep as-is (small utility) |
| 522 | Info | `clamp_buf` | av2/common/idct.c:26 | 3 | 1 | Keep as-is (local) |
| 523 | Info | `av2_intra_dip_modes` | av2/common/intra_dip.h:23 | 4 | 9 | Keep as-is (small utility) |
| 524 | Info | `av2_intra_dip_has_transpose` | av2/common/intra_dip.h:28 | 4 | 9 | Keep as-is (small utility) |
| 525 | Info | `is_in_operating_point` | av2/common/level.h:170 | 7 | 3 | Keep as-is (small utility) |
| 526 | Info | `get_fullmv_from_mv` | av2/common/mv.h:145 | 6 | 5 | Keep as-is (small utility) |
| 527 | Info | `get_mv_from_fullmv` | av2/common/mv.h:173 | 5 | 5 | Keep as-is (small utility) |
| 528 | Info | `convert_fullmv_to_mv` | av2/common/mv.h:179 | 3 | 5 | Keep as-is (small utility) |
| 529 | Info | `compute_mapping_val` | av2/common/mv.h:184 | 10 | 5 | Keep as-is (small utility) |
| 530 | Info | `compute_inverse_mapping_val` | av2/common/mv.h:226 | 11 | 5 | Keep as-is (small utility) |
| 531 | Info | `process_mv_for_tmvp` | av2/common/mv.h:270 | 4 | 5 | Keep as-is (small utility) |
| 532 | Info | `fetch_mv_from_tmvp` | av2/common/mv.h:276 | 4 | 5 | Keep as-is (small utility) |
| 533 | Info | `get_default_num_shell_class` | av2/common/mv.h:351 | 3 | 5 | Keep as-is (small utility) |
| 534 | Info | `split_num_shell_class` | av2/common/mv.h:355 | 6 | 5 | Keep as-is (small utility) |
| 535 | Info | `get_index_from_amvd_mvd` | av2/common/mv.h:382 | 10 | 5 | Keep as-is (small utility) |
| 536 | Info | `get_mvd_from_amvd_index` | av2/common/mv.h:394 | 6 | 5 | Keep as-is (small utility) |
| 537 | Info | `is_valid_amvd_mvd` | av2/common/mv.h:402 | 10 | 5 | Keep as-is (small utility) |
| 538 | Info | `get_adaptive_mvd_from_ref_mv` | av2/common/mv.h:414 | 4 | 5 | Keep as-is (small utility) |
| 539 | Info | `get_amvd_index_from_mvd` | av2/common/mv.h:419 | 8 | 5 | Keep as-is (small utility) |
| 540 | Info | `check_mvd_valid_amvd` | av2/common/mv.h:428 | 9 | 5 | Keep as-is (small utility) |
| 541 | Info | `block_center_x` | av2/common/mv.h:564 | 4 | 5 | Keep as-is (small utility) |
| 542 | Info | `block_center_y` | av2/common/mv.h:569 | 4 | 5 | Keep as-is (small utility) |
| 543 | Info | `convert_to_trans_prec` | av2/common/mv.h:574 | 6 | 5 | Keep as-is (small utility) |
| 544 | Info | `get_gm_precision_loss` | av2/common/mv.h:583 | 9 | 5 | Keep as-is (small utility) |
| 545 | Info | `get_wmtype` | av2/common/mv.h:593 | 10 | 5 | Keep as-is (small utility) |
| 546 | Info | `is_zero_mv` | av2/common/mv.h:637 | 3 | 5 | Keep as-is (small utility) |
| 547 | Info | `is_equal_mv` | av2/common/mv.h:641 | 3 | 5 | Keep as-is (small utility) |
| 548 | Info | `clamp_mv` | av2/common/mv.h:645 | 4 | 5 | Keep as-is (small utility) |
| 549 | Info | `clamp_fullmv` | av2/common/mv.h:650 | 4 | 5 | Keep as-is (small utility) |
| 550 | Info | `convert_mv_to_1_16th_pel` | av2/common/mv.h:656 | 6 | 5 | Keep as-is (small utility) |
| 551 | Info | `get_map_shell_class` | av2/common/mv.h:663 | 4 | 5 | Keep as-is (small utility) |
| 552 | Info | `check_frame_mv_slot` | av2/common/mvref_common.c:57 | 49 | 1 | Keep as-is (local) |
| 553 | Info | `is_warp_affine_block` | av2/common/mvref_common.c:115 | 23 | 1 | Keep as-is (local) |
| 554 | Info | `get_sub_block_warp_mv` | av2/common/mvref_common.c:140 | 16 | 1 | Keep as-is (local) |
| 555 | Info | `fill_mvp_from_derived_smvp` | av2/common/mvref_common.c:651 | 79 | 1 | Keep as-is (local) |
| 556 | Info | `derive_ref_mv_candidate_from_tip_mode` | av2/common/mvref_common.c:731 | 47 | 1 | Keep as-is (local) |
| 557 | Info | `add_ref_mv_candidate_ctx` | av2/common/mvref_common.c:780 | 39 | 1 | Keep as-is (local) |
| 558 | Info | `is_valid_warp_parameters` | av2/common/mvref_common.c:1409 | 26 | 1 | Keep as-is (local) |
| 559 | Info | `is_valid_candidate` | av2/common/mvref_common.c:1466 | 21 | 1 | Keep as-is (local) |
| 560 | Info | `scan_blk_mbmi_ctx` | av2/common/mvref_common.c:1488 | 16 | 1 | Keep as-is (local) |
| 561 | Info | `compute_cur_to_ref_dist` | av2/common/mvref_common.c:1625 | 9 | 1 | Keep as-is (local) |
| 562 | Info | `get_rmb_list_index` | av2/common/mvref_common.c:1795 | 24 | 1 | Keep as-is (local) |
| 563 | Info | `check_rmb_cand` | av2/common/mvref_common.c:1820 | 39 | 1 | Keep as-is (local) |
| 564 | Info | `add_to_ref_bv_list` | av2/common/mvref_common.c:1861 | 11 | 1 | Keep as-is (local) |
| 565 | Info | `add_tmvp_candidate` | av2/common/mvref_common.c:1873 | 41 | 1 | Keep as-is (local) |
| 566 | Info | `assign_tmvp_high_priority` | av2/common/mvref_common.c:1917 | 14 | 1 | Keep as-is (local) |
| 567 | Info | `add_ref_mv_bank_candidates` | av2/common/mvref_common.c:1954 | 35 | 1 | Keep as-is (local) |
| 568 | Info | `compute_aligned_offset` | av2/common/mvref_common.c:1991 | 5 | 1 | Keep as-is (local) |
| 569 | Info | `is_above_smvp_available` | av2/common/mvref_common.c:1998 | 8 | 1 | Keep as-is (local) |
| 570 | Info | `get_row_smvp_states` | av2/common/mvref_common.c:2008 | 82 | 1 | Keep as-is (local) |
| 571 | Info | `fill_warp_corner_projected_point` | av2/common/mvref_common.c:2093 | 38 | 1 | Keep as-is (local) |
| 572 | Info | `generate_points_from_corners` | av2/common/mvref_common.c:2133 | 60 | 1 | Keep as-is (local) |
| 573 | Info | `setup_ref_mv_list` | av2/common/mvref_common.c:2244 | 507 | 1 | Keep as-is (local) |
| 574 | Info | `get_dist_to_closest_interp_ref` | av2/common/mvref_common.c:2970 | 36 | 1 | Keep as-is (local) |
| 575 | Info | `is_ref_overlay` | av2/common/mvref_common.c:3009 | 16 | 1 | Keep as-is (local) |
| 576 | Info | `get_blk_id_k` | av2/common/mvref_common.c:3141 | 3 | 1 | Keep as-is (local) |
| 577 | Info | `check_traj_intersect` | av2/common/mvref_common.c:3147 | 137 | 1 | Keep as-is (local) |
| 578 | Info | `calc_avg` | av2/common/mvref_common.c:3643 | 14 | 1 | Keep as-is (local) |
| 579 | Info | `record_samples` | av2/common/mvref_common.c:4353 | 13 | 1 | Keep as-is (local) |
| 580 | Info | `update_ref_mv_bank` | av2/common/mvref_common.c:4627 | 80 | 1 | Keep as-is (local) |
| 581 | Info | `update_warp_param_bank` | av2/common/mvref_common.c:4773 | 76 | 1 | Keep as-is (local) |
| 582 | Info | `get_pos_from_pos_idx` | av2/common/mvref_common.c:5077 | 45 | 1 | Keep as-is (local) |
| 583 | Info | `get_cand_from_pos_idx` | av2/common/mvref_common.c:5123 | 34 | 1 | Keep as-is (local) |
| 584 | Info | `check_pos_and_get_base_pos` | av2/common/mvref_common.c:5158 | 22 | 1 | Keep as-is (local) |
| 585 | Info | `get_mf_sb_size_log2` | av2/common/mvref_common.h:39 | 12 | 14 | Keep as-is (small utility) |
| 586 | Info | `clamp_mv_ref` | av2/common/mvref_common.h:157 | 9 | 14 | Keep as-is (small utility) |
| 587 | Info | `opfl_get_subblock_size` | av2/common/mvref_common.h:167 | 4 | 14 | Keep as-is (small utility) |
| 588 | Info | `opfl_subblock_size_plane` | av2/common/mvref_common.h:174 | 12 | 14 | Keep as-is (small utility) |
| 589 | Info | `get_subblk_offset_x_hp` | av2/common/mvref_common.h:187 | 10 | 14 | Keep as-is (small utility) |
| 590 | Info | `get_subblk_offset_y_hp` | av2/common/mvref_common.h:198 | 10 | 14 | Keep as-is (small utility) |
| 591 | Info | `get_block_mv` | av2/common/mvref_common.h:307 | 5 | 14 | Keep as-is (small utility) |
| 592 | Info | `get_mv_from_wrl` | av2/common/mvref_common.h:314 | 10 | 14 | Keep as-is (small utility) |
| 593 | Info | `is_inside` | av2/common/mvref_common.h:327 | 7 | 14 | Keep as-is (small utility) |
| 594 | Info | `find_valid_col_offset` | av2/common/mvref_common.h:335 | 5 | 14 | Keep as-is (small utility) |
| 595 | Info | `single2comb` | av2/common/mvref_common.h:349 | 14 | 14 | Keep as-is (small utility) |
| 596 | Info | `av2_ref_frame_type` | av2/common/mvref_common.h:383 | 14 | 14 | Keep as-is (small utility) |
| 597 | Info | `av2_ref_mv_idx_type` | av2/common/mvref_common.h:400 | 11 | 14 | Keep as-is (small utility) |
| 598 | Info | `av2_set_ref_mv_idx` | av2/common/mvref_common.h:413 | 7 | 14 | Keep as-is (small utility) |
| 599 | Info | `av2_set_ref_frame` | av2/common/mvref_common.h:421 | 12 | 14 | Keep as-is (small utility) |
| 600 | Info | `av2_mode_context_pristine` | av2/common/mvref_common.h:434 | 6 | 14 | Keep as-is (small utility) |
| 601 | Info | `av2_mode_context_analyzer` | av2/common/mvref_common.h:441 | 6 | 14 | Keep as-is (small utility) |
| 602 | Info | `get_optflow_context` | av2/common/mvref_common.h:448 | 7 | 14 | Keep as-is (small utility) |
| 603 | Info | `av2_get_drl_cdf` | av2/common/mvref_common.h:456 | 14 | 14 | Keep as-is (small utility) |
| 604 | Info | `av2_get_warp_ref_idx_cdf` | av2/common/mvref_common.h:472 | 5 | 14 | Keep as-is (small utility) |
| 605 | Info | `get_mv_projection_clamp` | av2/common/mvref_common.h:485 | 13 | 14 | Keep as-is (small utility) |
| 606 | Info | `get_mv_projection` | av2/common/mvref_common.h:499 | 3 | 14 | Keep as-is (small utility) |
| 607 | Info | `allow_amvd_mode` | av2/common/mvref_common.h:525 | 6 | 14 | Keep as-is (small utility) |
| 608 | Info | `amvd_mode_to_index` | av2/common/mvref_common.h:532 | 14 | 14 | Keep as-is (small utility) |
| 609 | Info | `av2_find_ref_dv` | av2/common/mvref_common.h:613 | 11 | 14 | Keep as-is (small utility) |
| 610 | Info | `is_two_blk_overlap` | av2/common/mvref_common.h:625 | 8 | 14 | Keep as-is (small utility) |
| 611 | Info | `av2_get_warp_base_params` | av2/common/mvref_common.h:1061 | 12 | 14 | Keep as-is (small utility) |
| 612 | Info | `av2_get_warp_causal_ctx` | av2/common/mvref_common.h:1122 | 13 | 14 | Keep as-is (small utility) |
| 613 | Info | `av2_get_warp_extend_ctx` | av2/common/mvref_common.h:1136 | 11 | 14 | Keep as-is (small utility) |
| 614 | Info | `is_ref_motion_field_eligible_by_frame_type` | av2/common/mvref_common.h:1167 | 10 | 14 | Keep as-is (small utility) |
| 615 | Info | `is_ref_motion_field_eligible_by_frame_size` | av2/common/mvref_common.h:1180 | 14 | 14 | Keep as-is (small utility) |
| 616 | Info | `is_ref_motion_field_eligible` | av2/common/mvref_common.h:1197 | 5 | 14 | Keep as-is (small utility) |
| 617 | Info | `tip_get_mv_projection` | av2/common/mvref_common.h:1204 | 11 | 14 | Keep as-is (small utility) |
| 618 | Info | `derive_col_mv_tpl_offset` | av2/common/mvref_common.h:1217 | 14 | 14 | Keep as-is (small utility) |
| 619 | Info | `derive_block_mv_tpl_offset` | av2/common/mvref_common.h:1252 | 9 | 14 | Keep as-is (small utility) |
| 620 | Info | `is_single_tile_vcl_obu` | av2/common/obu_util.h:40 | 5 | 13 | Keep as-is (small utility) |
| 621 | Info | `is_multi_tile_vcl_obu` | av2/common/obu_util.h:48 | 6 | 13 | Keep as-is (small utility) |
| 622 | Info | `is_tu_head_non_vcl_obu` | av2/common/obu_util.h:57 | 9 | 13 | Keep as-is (small utility) |
| 623 | Info | `get_filter_set_index` | av2/common/pc_wiener_filters.h:1295 | 11 | 0 | Keep as-is (small utility) |
| 624 | Info | `int16_t` | av2/common/pc_wiener_filters.h:1307 | 10 | 0 | Keep as-is (small utility) |
| 625 | Info | `get_filter_selector` | av2/common/pc_wiener_filters.h:1318 | 10 | 0 | Keep as-is (small utility) |
| 626 | Info | `get_sub_classifiers` | av2/common/pc_wiener_filters.h:1365 | 11 | 0 | Keep as-is (small utility) |
| 627 | Info | `encode_num_filter_classes` | av2/common/pc_wiener_filters.h:1381 | 14 | 0 | Keep as-is (small utility) |
| 628 | Info | `decode_num_filter_classes` | av2/common/pc_wiener_filters.h:1397 | 14 | 0 | Keep as-is (small utility) |
| 629 | Info | `get_furthest_future_ref_index` | av2/common/pred_common.h:124 | 14 | 20 | Keep as-is (small utility) |
| 630 | Info | `get_closest_past_ref_index` | av2/common/pred_common.h:140 | 13 | 20 | Keep as-is (small utility) |
| 631 | Info | `get_closest_pastcur_ref_or_ref0` | av2/common/pred_common.h:158 | 7 | 20 | Keep as-is (small utility) |
| 632 | Info | `get_best_past_ref_index` | av2/common/pred_common.h:166 | 7 | 20 | Keep as-is (small utility) |
| 633 | Info | `get_tip_ctx` | av2/common/pred_common.h:212 | 11 | 20 | Keep as-is (small utility) |
| 634 | Info | `is_tip_allowed` | av2/common/pred_common.h:224 | 11 | 20 | Keep as-is (small utility) |
| 635 | Info | `av2_get_pred_context_seg_id` | av2/common/pred_common.h:352 | 8 | 20 | Keep as-is (small utility) |
| 636 | Info | `derive_comp_one_ref_context` | av2/common/pred_common.h:361 | 13 | 20 | Keep as-is (small utility) |
| 637 | Info | `av2_get_pred_cdf_seg_id` | av2/common/pred_common.h:402 | 4 | 20 | Keep as-is (small utility) |
| 638 | Info | `av2_get_skip_mode_context` | av2/common/pred_common.h:407 | 11 | 20 | Keep as-is (small utility) |
| 639 | Info | `av2_get_skip_txfm_context` | av2/common/pred_common.h:419 | 12 | 20 | Keep as-is (small utility) |
| 640 | Info | `get_intrabc_ctx` | av2/common/pred_common.h:432 | 11 | 20 | Keep as-is (small utility) |
| 641 | Info | `get_morph_pred_ctx` | av2/common/pred_common.h:444 | 11 | 20 | Keep as-is (small utility) |
| 642 | Info | `is_cctx_enabled` | av2/common/pred_common.h:456 | 4 | 20 | Keep as-is (small utility) |
| 643 | Info | `av2_get_reference_mode_cdf` | av2/common/pred_common.h:505 | 4 | 20 | Keep as-is (small utility) |
| 644 | Info | `av2_get_pred_cdf_single_ref` | av2/common/pred_common.h:514 | 7 | 20 | Keep as-is (small utility) |
| 645 | Info | `av2_get_compound_ref_bit_type` | av2/common/pred_common.h:525 | 6 | 20 | Keep as-is (small utility) |
| 646 | Info | `av2_get_pred_cdf_compound_ref` | av2/common/pred_common.h:533 | 12 | 20 | Keep as-is (small utility) |
| 647 | Info | `is_2d_transform` | av2/common/quant_common.c:319 | 3 | 1 | Keep as-is (local) |
| 648 | Info | `tcq_quant` | av2/common/quant_common.h:71 | 1 | 22 | Keep as-is (small utility) |
| 649 | Info | `tcq_enable` | av2/common/quant_common.h:74 | 7 | 22 | Keep as-is (small utility) |
| 650 | Info | `avm_get_qmlevel` | av2/common/quant_common.h:108 | 7 | 22 | Keep as-is (small utility) |
| 651 | Info | `build_cwp_mask` | av2/common/reconinter.c:327 | 9 | 1 | Keep as-is (local) |
| 652 | Info | `diffwtd_mask_d16` | av2/common/reconinter.c:421 | 16 | 1 | Keep as-is (local) |
| 653 | Info | `diffwtd_mask_highbd` | av2/common/reconinter.c:455 | 62 | 1 | Keep as-is (local) |
| 654 | Info | `init_wedge_master_all_masks` | av2/common/reconinter.c:536 | 21 | 1 | Keep as-is (local) |
| 655 | Info | `init_wedge_masks` | av2/common/reconinter.c:558 | 39 | 1 | Keep as-is (local) |
| 656 | Info | `build_smooth_interintra_mask` | av2/common/reconinter.c:617 | 40 | 1 | Keep as-is (local) |
| 657 | Info | `init_smooth_interintra_masks` | av2/common/reconinter.c:658 | 11 | 1 | Keep as-is (local) |
| 658 | Info | `compute_pred_using_interp_grad_highbd` | av2/common/reconinter.c:1116 | 20 | 1 | Keep as-is (local) |
| 659 | Info | `build_masked_compound_no_round` | av2/common/reconinter.c:1282 | 20 | 1 | Keep as-is (local) |
| 660 | Info | `avm_memset16_optimized` | av2/common/reconinter.c:2183 | 27 | 1 | Keep as-is (local) |
| 661 | Info | `is_sub_block_refinemv_enabled` | av2/common/reconinter.c:2904 | 21 | 1 | Keep as-is (local) |
| 662 | Info | `is_mv_refine_allowed` | av2/common/reconinter.c:2928 | 7 | 1 | Keep as-is (local) |
| 663 | Info | `skip_opfl_refine_with_tip` | av2/common/reconinter.c:2940 | 41 | 1 | Keep as-is (local) |
| 664 | Info | `combine_interintra_highbd` | av2/common/reconinter.c:3823 | 29 | 1 | Keep as-is (local) |
| 665 | Info | `get_warp_model_steps` | av2/common/reconinter.h:271 | 6 | 36 | Keep as-is (small utility) |
| 666 | Info | `get_default_six_param_flag` | av2/common/reconinter.h:279 | 8 | 36 | Keep as-is (small utility) |
| 667 | Info | `allow_warp_inter_intra` | av2/common/reconinter.h:288 | 9 | 36 | Keep as-is (small utility) |
| 668 | Info | `enable_adaptive_mvd_resolution` | av2/common/reconinter.h:340 | 8 | 36 | Keep as-is (small utility) |
| 669 | Info | `is_ref_frame_same_side` | av2/common/reconinter.h:370 | 14 | 36 | Keep as-is (small utility) |
| 670 | Info | `is_ref_frame_same_dist` | av2/common/reconinter.h:386 | 13 | 36 | Keep as-is (small utility) |
| 671 | Info | `get_inter_compound_mode_is_joint_context` | av2/common/reconinter.h:401 | 5 | 36 | Keep as-is (small utility) |
| 672 | Info | `has_scale` | av2/common/reconinter.h:413 | 3 | 36 | Keep as-is (small utility) |
| 673 | Info | `revert_scale_extra_bits` | av2/common/reconinter.h:417 | 10 | 36 | Keep as-is (small utility) |
| 674 | Info | `is_interinter_compound_used` | av2/common/reconinter.h:450 | 12 | 36 | Keep as-is (small utility) |
| 675 | Info | `is_any_masked_compound_used` | av2/common/reconinter.h:463 | 12 | 36 | Keep as-is (small utility) |
| 676 | Info | `get_wedge_types_lookup` | av2/common/reconinter.h:476 | 3 | 36 | Keep as-is (small utility) |
| 677 | Info | `av2_is_wedge_used` | av2/common/reconinter.h:480 | 3 | 36 | Keep as-is (small utility) |
| 678 | Info | `is_translational_refinement_allowed` | av2/common/reconinter.h:602 | 11 | 36 | Keep as-is (small utility) |
| 679 | Info | `is_refinemv_allowed_bsize` | av2/common/reconinter.h:663 | 6 | 36 | Keep as-is (small utility) |
| 680 | Info | `is_refinemv_allowed_mode_precision` | av2/common/reconinter.h:671 | 13 | 36 | Keep as-is (small utility) |
| 681 | Info | `default_refinemv_modes` | av2/common/reconinter.h:686 | 4 | 36 | Keep as-is (small utility) |
| 682 | Info | `is_refinemv_allowed` | av2/common/reconinter.h:742 | 14 | 36 | Keep as-is (small utility) |
| 683 | Info | `is_tip_coded_as_16x16_block` | av2/common/reconinter.h:804 | 14 | 36 | Keep as-is (small utility) |
| 684 | Info | `is_unequal_weighted_tip_allowed` | av2/common/reconinter.h:820 | 11 | 36 | Keep as-is (small utility) |
| 685 | Info | `set_tip_interp_weight_factor` | av2/common/reconinter.h:833 | 13 | 36 | Keep as-is (small utility) |
| 686 | Info | `is_refinemv_allowed_tip_blocks` | av2/common/reconinter.h:848 | 7 | 36 | Keep as-is (small utility) |
| 687 | Info | `is_refinemv_allowed_skip_mode` | av2/common/reconinter.h:857 | 6 | 36 | Keep as-is (small utility) |
| 688 | Info | `get_default_refinemv_flag` | av2/common/reconinter.h:863 | 12 | 36 | Keep as-is (small utility) |
| 689 | Info | `switchable_refinemv_flag` | av2/common/reconinter.h:877 | 14 | 36 | Keep as-is (small utility) |
| 690 | Info | `enable_refined_mvs_in_tmvp` | av2/common/reconinter.h:893 | 9 | 36 | Keep as-is (small utility) |
| 691 | Info | `scaled_buffer_offset` | av2/common/reconinter.h:1035 | 9 | 36 | Keep as-is (small utility) |
| 692 | Info | `set_default_interp_filters` | av2/common/reconinter.h:1100 | 13 | 36 | Keep as-is (small utility) |
| 693 | Info | `av2_get_all_contiguous_soft_mask` | av2/common/reconinter.h:1137 | 6 | 36 | Keep as-is (small utility) |
| 694 | Info | `get_wedge_boundary_type` | av2/common/reconinter.h:1144 | 7 | 36 | Keep as-is (small utility) |
| 695 | Info | `av2_get_contiguous_soft_mask_decision` | av2/common/reconinter.h:1152 | 6 | 36 | Keep as-is (small utility) |
| 696 | Info | `av2_allow_explicit_bawp` | av2/common/reconinter.h:1223 | 3 | 36 | Keep as-is (small utility) |
| 697 | Info | `is_warpmv_allowed_bsize` | av2/common/reconinter.h:1282 | 4 | 36 | Keep as-is (small utility) |
| 698 | Info | `is_warpmv_mode_allowed` | av2/common/reconinter.h:1288 | 12 | 36 | Keep as-is (small utility) |
| 699 | Info | `allow_warpmv_with_mvd_coding` | av2/common/reconinter.h:1302 | 5 | 36 | Keep as-is (small utility) |
| 700 | Info | `get_derive_sign_nzero_th` | av2/common/reconinter.h:1310 | 3 | 36 | Keep as-is (small utility) |
| 701 | Info | `is_subblock_outside` | av2/common/reconinter.h:1342 | 5 | 36 | Keep as-is (small utility) |
| 702 | Info | `av2_is_directional_mode` | av2/common/reconintra.h:76 | 3 | 28 | Keep as-is (small utility) |
| 703 | Info | `av2_allow_intrabc_morph_pred` | av2/common/reconintra.h:81 | 4 | 28 | Keep as-is (small utility) |
| 704 | Info | `allow_fsc_intra` | av2/common/reconintra.h:102 | 10 | 28 | Keep as-is (small utility) |
| 705 | Info | `use_inter_fsc` | av2/common/reconintra.h:113 | 7 | 28 | Keep as-is (small utility) |
| 706 | Info | `av2_intra_dip_allowed_bsize` | av2/common/reconintra.h:121 | 10 | 28 | Keep as-is (small utility) |
| 707 | Info | `av2_intra_dip_allowed` | av2/common/reconintra.h:132 | 6 | 28 | Keep as-is (small utility) |
| 708 | Info | `get_intra_dip_ctx` | av2/common/reconintra.h:139 | 7 | 28 | Keep as-is (small utility) |
| 709 | Info | `av2_get_dx` | av2/common/reconintra.h:189 | 10 | 28 | Keep as-is (small utility) |
| 710 | Info | `av2_get_dy` | av2/common/reconintra.h:204 | 10 | 28 | Keep as-is (small utility) |
| 711 | Info | `get_tx_partition_idx` | av2/common/reconintra.h:215 | 4 | 28 | Keep as-is (small utility) |
| 712 | Info | `set_have_top_and_left` | av2/common/reconintra.h:267 | 7 | 28 | Keep as-is (small utility) |
| 713 | Info | `apply_wienerns_single_class_highbd` | av2/common/restoration.c:1331 | 20 | 1 | Keep as-is (local) |
| 714 | Info | `apply_wienerns_multi_class_highbd` | av2/common/restoration.c:1354 | 49 | 1 | Keep as-is (local) |
| 715 | Info | `check_lossless` | av2/common/restoration.c:1404 | 22 | 1 | Keep as-is (local) |
| 716 | Info | `make_wienerns_ds_luma` | av2/common/restoration.c:1648 | 16 | 1 | Keep as-is (local) |
| 717 | Info | `get_matching_filter` | av2/common/restoration.c:3116 | 11 | 1 | Keep as-is (local) |
| 718 | Info | `get_wienerns_parameters` | av2/common/restoration.h:114 | 5 | 3 | Keep as-is (small utility) |
| 719 | Info | `is_frame_filters_enabled` | av2/common/restoration.h:120 | 4 | 3 | Keep as-is (small utility) |
| 720 | Info | `alternate_ref_plane` | av2/common/restoration.h:127 | 8 | 3 | Keep as-is (small utility) |
| 721 | Info | `max_num_classes` | av2/common/restoration.h:136 | 4 | 3 | Keep as-is (small utility) |
| 722 | Info | `default_num_classes` | av2/common/restoration.h:141 | 3 | 3 | Keep as-is (small utility) |
| 723 | Info | `get_first_match_index` | av2/common/restoration.h:159 | 6 | 3 | Keep as-is (small utility) |
| 724 | Info | `clip_to_wienerns_range` | av2/common/restoration.h:417 | 6 | 3 | Keep as-is (small utility) |
| 725 | Info | `set_default_wienerns_fromparams` | av2/common/restoration.h:441 | 14 | 3 | Keep as-is (small utility) |
| 726 | Info | `to_readwrite_framefilters` | av2/common/restoration.h:663 | 7 | 3 | Keep as-is (small utility) |
| 727 | Info | `get_ru_index_for_tile_start` | av2/common/restoration.h:720 | 7 | 3 | Keep as-is (small utility) |
| 728 | Info | `scaled_x` | av2/common/scale.c:21 | 7 | 1 | Keep as-is (local) |
| 729 | Info | `scaled_y` | av2/common/scale.c:30 | 7 | 1 | Keep as-is (local) |
| 730 | Info | `scaled_warp_x` | av2/common/scale.c:39 | 5 | 1 | Keep as-is (local) |
| 731 | Info | `scaled_warp_y` | av2/common/scale.c:46 | 5 | 1 | Keep as-is (local) |
| 732 | Info | `un_scaled_warp` | av2/common/scale.c:52 | 5 | 1 | Keep as-is (local) |
| 733 | Info | `av2_is_valid_scale` | av2/common/scale.h:46 | 5 | 2 | Keep as-is (small utility) |
| 734 | Info | `av2_is_scaled` | av2/common/scale.h:52 | 5 | 2 | Keep as-is (small utility) |
| 735 | Info | `valid_ref_frame_size` | av2/common/scale.h:58 | 5 | 2 | Keep as-is (small utility) |
| 736 | Info | `get_default_scan` | av2/common/scan.h:39 | 4 | 14 | Keep as-is (small utility) |
| 737 | Info | `get_scan` | av2/common/scan.h:44 | 3 | 14 | Keep as-is (small utility) |
| 738 | Info | `segfeature_active` | av2/common/seg_common.h:74 | 5 | 20 | Keep as-is (small utility) |
| 739 | Info | `get_segdata` | av2/common/seg_common.h:119 | 4 | 20 | Keep as-is (small utility) |
| 740 | Info | `get_sync_range` | av2/common/thread_common.c:29 | 12 | 1 | Keep as-is (local) |
| 741 | Info | `cdef_row_mt_sync_read` | av2/common/thread_common.c:167 | 14 | 1 | Keep as-is (local) |
| 742 | Info | `cdef_row_mt_sync_write` | av2/common/thread_common.c:183 | 12 | 1 | Keep as-is (local) |
| 743 | Info | `sync_read` | av2/common/thread_common.c:196 | 21 | 1 | Keep as-is (local) |
| 744 | Info | `sync_write` | av2/common/thread_common.c:218 | 31 | 1 | Keep as-is (local) |
| 745 | Info | `thread_loop_filter_rows` | av2/common/thread_common.c:298 | 54 | 1 | Keep as-is (local) |
| 746 | Info | `ccso_data_reset` | av2/common/thread_common.c:441 | 7 | 1 | Keep as-is (local) |
| 747 | Info | `process_ccso_rows` | av2/common/thread_common.c:573 | 50 | 1 | Keep as-is (local) |
| 748 | Info | `process_tip_rows` | av2/common/thread_common.c:711 | 26 | 1 | Keep as-is (local) |
| 749 | Info | `reset_cdef_job_info` | av2/common/thread_common.c:1250 | 5 | 1 | Keep as-is (local) |
| 750 | Info | `launch_cdef_workers` | av2/common/thread_common.c:1257 | 11 | 1 | Keep as-is (local) |
| 751 | Info | `sync_cdef_workers` | av2/common/thread_common.c:1270 | 15 | 1 | Keep as-is (local) |
| 752 | Info | `get_cdef_row_next_job` | av2/common/thread_common.c:1297 | 18 | 1 | Keep as-is (local) |
| 753 | Info | `tile_log2` | av2/common/tile_common.h:84 | 6 | 9 | Keep as-is (small utility) |
| 754 | Info | `tip_build_inter_predictors_8x8` | av2/common/tip.c:397 | 199 | 1 | Keep as-is (local) |
| 755 | Info | `tip_build_inter_predictors_8x8_and_bigger` | av2/common/tip.c:597 | 160 | 1 | Keep as-is (local) |
| 756 | Info | `tip_component_build_inter_predictors` | av2/common/tip.c:758 | 8 | 1 | Keep as-is (local) |
| 757 | Info | `tip_setup_pred_plane` | av2/common/tip.c:767 | 16 | 1 | Keep as-is (local) |
| 758 | Info | `tip_component_setup_dst_planes` | av2/common/tip.c:784 | 18 | 1 | Keep as-is (local) |
| 759 | Info | `tip_setup_tip_frame_planes` | av2/common/tip.c:930 | 18 | 1 | Keep as-is (local) |
| 760 | Info | `is_tip_mv_refinement_disabled_for_unit_size_16x16` | av2/common/tip.h:84 | 5 | 8 | Keep as-is (small utility) |
| 761 | Info | `tip_derive_scale_factor` | av2/common/tip.h:91 | 8 | 8 | Keep as-is (small utility) |
| 762 | Info | `get_txb_bwl` | av2/common/txb_common.h:71 | 4 | 9 | Keep as-is (small utility) |
| 763 | Info | `get_txb_wide` | av2/common/txb_common.h:76 | 4 | 9 | Keep as-is (small utility) |
| 764 | Info | `get_txb_high` | av2/common/txb_common.h:81 | 4 | 9 | Keep as-is (small utility) |
| 765 | Info | `set_levels` | av2/common/txb_common.h:86 | 3 | 9 | Keep as-is (small utility) |
| 766 | Info | `get_padded_idx` | av2/common/txb_common.h:90 | 3 | 9 | Keep as-is (small utility) |
| 767 | Info | `set_signs` | av2/common/txb_common.h:95 | 3 | 9 | Keep as-is (small utility) |
| 768 | Info | `get_padded_idx_left` | av2/common/txb_common.h:100 | 3 | 9 | Keep as-is (small utility) |
| 769 | Info | `get_br_ctx_skip` | av2/common/txb_common.h:110 | 11 | 9 | Keep as-is (small utility) |
| 770 | Info | `get_br_ctx_2d` | av2/common/txb_common.h:122 | 14 | 9 | Keep as-is (small utility) |
| 771 | Info | `get_br_ctx_2d_chroma` | av2/common/txb_common.h:137 | 13 | 9 | Keep as-is (small utility) |
| 772 | Info | `get_br_ctx_lf_eob` | av2/common/txb_common.h:153 | 5 | 9 | Keep as-is (small utility) |
| 773 | Info | `get_br_lf_ctx_2d` | av2/common/txb_common.h:182 | 14 | 9 | Keep as-is (small utility) |
| 774 | Info | `get_nz_mag_skip` | av2/common/txb_common.h:282 | 7 | 9 | Keep as-is (small utility) |
| 775 | Info | `get_sign_ctx_skip` | av2/common/txb_common.h:312 | 9 | 9 | Keep as-is (small utility) |
| 776 | Info | `get_nz_mag_lf_chroma` | av2/common/txb_common.h:324 | 12 | 9 | Keep as-is (small utility) |
| 777 | Info | `get_nz_mag_chroma` | av2/common/txb_common.h:339 | 12 | 9 | Keep as-is (small utility) |
| 778 | Info | `get_nz_map_ctx_from_stats_lf_chroma` | av2/common/txb_common.h:408 | 10 | 9 | Keep as-is (small utility) |
| 779 | Info | `get_nz_map_ctx_from_stats_chroma` | av2/common/txb_common.h:472 | 11 | 9 | Keep as-is (small utility) |
| 780 | Info | `get_base_ctx_ph` | av2/common/txb_common.h:518 | 6 | 9 | Keep as-is (small utility) |
| 781 | Info | `get_lower_levels_ctx_eob` | av2/common/txb_common.h:525 | 7 | 9 | Keep as-is (small utility) |
| 782 | Info | `get_lower_levels_ctx_bob` | av2/common/txb_common.h:534 | 6 | 9 | Keep as-is (small utility) |
| 783 | Info | `get_upper_levels_ctx_2d` | av2/common/txb_common.h:541 | 9 | 9 | Keep as-is (small utility) |
| 784 | Info | `get_lower_levels_ctx_lf_2d_chroma` | av2/common/txb_common.h:553 | 12 | 9 | Keep as-is (small utility) |
| 785 | Info | `get_lower_levels_lf_ctx_chroma` | av2/common/txb_common.h:566 | 7 | 9 | Keep as-is (small utility) |
| 786 | Info | `get_lower_levels_lf_ctx` | av2/common/txb_common.h:620 | 7 | 9 | Keep as-is (small utility) |
| 787 | Info | `get_lf_limits` | av2/common/txb_common.h:653 | 13 | 9 | Keep as-is (small utility) |
| 788 | Info | `get_lower_levels_ctx_chroma` | av2/common/txb_common.h:667 | 8 | 9 | Keep as-is (small utility) |
| 789 | Info | `get_lower_levels_ctx` | av2/common/txb_common.h:676 | 7 | 9 | Keep as-is (small utility) |
| 790 | Info | `get_upper_levels_ctx_general` | av2/common/txb_common.h:686 | 11 | 9 | Keep as-is (small utility) |
| 791 | Info | `set_dc_sign` | av2/common/txb_common.h:730 | 6 | 9 | Keep as-is (small utility) |
| 792 | Info | `get_txb_ctx_skip` | av2/common/txb_common.h:737 | 13 | 9 | Keep as-is (small utility) |
| 793 | Info | `set_planes_to_neutral_grey` | av2/decoder/decodeframe.c:148 | 18 | 1 | Keep as-is (local) |
| 794 | Info | `inverse_transform_block` | av2/decoder/decodeframe.c:192 | 31 | 1 | Keep as-is (local) |
| 795 | Info | `read_coeffs_tx_intra_block` | av2/decoder/decodeframe.c:224 | 23 | 1 | Keep as-is (local) |
| 796 | Info | `decode_block_void` | av2/decoder/decodeframe.c:248 | 13 | 1 | Keep as-is (local) |
| 797 | Info | `predict_inter_block_void` | av2/decoder/decodeframe.c:262 | 7 | 1 | Keep as-is (local) |
| 798 | Info | `predict_and_reconstruct_intra_block` | av2/decoder/decodeframe.c:270 | 112 | 1 | Keep as-is (local) |
| 799 | Info | `inverse_cross_chroma_transform_block` | av2/decoder/decodeframe.c:384 | 16 | 1 | Keep as-is (local) |
| 800 | Info | `inverse_transform_inter_block` | av2/decoder/decodeframe.c:401 | 40 | 1 | Keep as-is (local) |
| 801 | Info | `set_cb_buffer_offsets` | av2/decoder/decodeframe.c:442 | 6 | 1 | Keep as-is (local) |
| 802 | Info | `decode_reconstruct_tx` | av2/decoder/decodeframe.c:449 | 64 | 1 | Keep as-is (local) |
| 803 | Info | `set_offsets` | av2/decoder/decodeframe.c:514 | 50 | 1 | Keep as-is (local) |
| 804 | Info | `extend_mc_border` | av2/decoder/decodeframe.c:565 | 27 | 1 | Keep as-is (local) |
| 805 | Info | `decode_mbmi_block` | av2/decoder/decodeframe.c:683 | 72 | 1 | Keep as-is (local) |
| 806 | Info | `dec_build_inter_predictor` | av2/decoder/decodeframe.c:767 | 53 | 1 | Keep as-is (local) |
| 807 | Info | `predict_inter_block` | av2/decoder/decodeframe.c:821 | 85 | 1 | Keep as-is (local) |
| 808 | Info | `copy_frame_mvs_inter_block` | av2/decoder/decodeframe.c:907 | 22 | 1 | Keep as-is (local) |
| 809 | Info | `set_color_index_map_offset` | av2/decoder/decodeframe.c:930 | 10 | 1 | Keep as-is (local) |
| 810 | Info | `bridge_frame_is_valid_inter` | av2/decoder/decodeframe.c:1017 | 16 | 1 | Keep as-is (local) |
| 811 | Info | `decode_token_recon_block` | av2/decoder/decodeframe.c:1034 | 322 | 1 | Keep as-is (local) |
| 812 | Info | `parse_decode_block` | av2/decoder/decodeframe.c:1488 | 136 | 1 | Keep as-is (local) |
| 813 | Info | `set_offsets_for_pred_and_recon` | av2/decoder/decodeframe.c:1625 | 41 | 1 | Keep as-is (local) |
| 814 | Info | `decode_block` | av2/decoder/decodeframe.c:1667 | 20 | 1 | Keep as-is (local) |
| 815 | Info | `decode_partition` | av2/decoder/decodeframe.c:1850 | 430 | 1 | Keep as-is (local) |
| 816 | Info | `setup_bool_decoder` | av2/decoder/decodeframe.c:2281 | 17 | 1 | Keep as-is (local) |
| 817 | Info | `decode_partition_sb` | av2/decoder/decodeframe.c:2299 | 62 | 1 | Keep as-is (local) |
| 818 | Info | `setup_bru_active_info` | av2/decoder/decodeframe.c:2361 | 29 | 1 | Keep as-is (local) |
| 819 | Info | `setup_segmentation` | av2/decoder/decodeframe.c:2420 | 66 | 1 | Keep as-is (local) |
| 820 | Info | `decode_restoration_mode` | av2/decoder/decodeframe.c:2514 | 209 | 1 | Keep as-is (local) |
| 821 | Info | `loop_restoration_read_sb_coeffs` | av2/decoder/decodeframe.c:2979 | 58 | 1 | Keep as-is (local) |
| 822 | Info | `setup_loopfilter` | av2/decoder/decodeframe.c:3038 | 112 | 1 | Keep as-is (local) |
| 823 | Info | `setup_gdf` | av2/decoder/decodeframe.c:3151 | 24 | 1 | Keep as-is (local) |
| 824 | Info | `setup_cdef` | av2/decoder/decodeframe.c:3176 | 52 | 1 | Keep as-is (local) |
| 825 | Info | `read_ccso_offset_idx` | av2/decoder/decodeframe.c:3230 | 9 | 1 | Keep as-is (local) |
| 826 | Info | `setup_ccso` | av2/decoder/decodeframe.c:3239 | 173 | 1 | Keep as-is (local) |
| 827 | Info | `read_delta_q` | av2/decoder/decodeframe.c:3413 | 3 | 1 | Keep as-is (local) |
| 828 | Info | `setup_quantization` | av2/decoder/decodeframe.c:3417 | 52 | 1 | Keep as-is (local) |
| 829 | Info | `setup_qm_params` | av2/decoder/decodeframe.c:3551 | 61 | 1 | Keep as-is (local) |
| 830 | Info | `setup_segmentation_dequant` | av2/decoder/decodeframe.c:3613 | 83 | 1 | Keep as-is (local) |
| 831 | Info | `setup_render_size` | av2/decoder/decodeframe.c:3702 | 24 | 1 | Keep as-is (local) |
| 832 | Info | `resize_context_buffers` | av2/decoder/decodeframe.c:3727 | 50 | 1 | Keep as-is (local) |
| 833 | Info | `setup_tip_frame_size` | av2/decoder/decodeframe.c:3778 | 57 | 1 | Keep as-is (local) |
| 834 | Info | `setup_buffer_pool` | av2/decoder/decodeframe.c:3836 | 55 | 1 | Keep as-is (local) |
| 835 | Info | `setup_frame_size` | av2/decoder/decodeframe.c:3949 | 47 | 1 | Keep as-is (local) |
| 836 | Info | `setup_seq_sb_size` | av2/decoder/decodeframe.c:3997 | 18 | 1 | Keep as-is (local) |
| 837 | Info | `valid_ref_frame_img_fmt` | av2/decoder/decodeframe.c:4016 | 7 | 1 | Keep as-is (local) |
| 838 | Info | `setup_frame_size_with_refs` | av2/decoder/decodeframe.c:4024 | 71 | 1 | Keep as-is (local) |
| 839 | Info | `read_tile_info_max_tile` | av2/decoder/decodeframe.c:4145 | 56 | 1 | Keep as-is (local) |
| 840 | Info | `read_tile_info` | av2/decoder/decodeframe.c:4202 | 47 | 1 | Keep as-is (local) |
| 841 | Info | `get_tile_buffer` | av2/decoder/decodeframe.c:4262 | 26 | 1 | Keep as-is (local) |
| 842 | Info | `get_tile_buffers` | av2/decoder/decodeframe.c:4289 | 42 | 1 | Keep as-is (local) |
| 843 | Info | `set_cb_buffer` | av2/decoder/decodeframe.c:4332 | 28 | 1 | Keep as-is (local) |
| 844 | Info | `decoder_alloc_tile_data` | av2/decoder/decodeframe.c:4361 | 13 | 1 | Keep as-is (local) |
| 845 | Info | `get_sync_range` | av2/decoder/decodeframe.c:4379 | 4 | 1 | Keep as-is (local) |
| 846 | Info | `dec_row_mt_alloc` | av2/decoder/decodeframe.c:4385 | 31 | 1 | Keep as-is (local) |
| 847 | Info | `sync_read` | av2/decoder/decodeframe.c:4443 | 20 | 1 | Keep as-is (local) |
| 848 | Info | `sync_write` | av2/decoder/decodeframe.c:4464 | 29 | 1 | Keep as-is (local) |
| 849 | Info | `decode_tile_sb_row` | av2/decoder/decodeframe.c:4494 | 37 | 1 | Keep as-is (local) |
| 850 | Info | `set_decode_func_pointers` | av2/decoder/decodeframe.c:4555 | 23 | 1 | Keep as-is (local) |
| 851 | Info | `decode_tile` | av2/decoder/decodeframe.c:4579 | 60 | 1 | Keep as-is (local) |
| 852 | Info | `tile_worker_hook_init` | av2/decoder/decodeframe.c:4827 | 44 | 1 | Keep as-is (local) |
| 853 | Info | `get_max_row_mt_workers_per_tile` | av2/decoder/decodeframe.c:4914 | 13 | 1 | Keep as-is (local) |
| 854 | Info | `signal_parse_sb_row_done` | av2/decoder/decodeframe.c:5044 | 20 | 1 | Keep as-is (local) |
| 855 | Info | `parse_tile_row_mt` | av2/decoder/decodeframe.c:5067 | 55 | 1 | Keep as-is (local) |
| 856 | Info | `enqueue_tile_jobs` | av2/decoder/decodeframe.c:5255 | 21 | 1 | Keep as-is (local) |
| 857 | Info | `alloc_dec_jobs` | av2/decoder/decodeframe.c:5277 | 19 | 1 | Keep as-is (local) |
| 858 | Info | `allocate_opfl_tmp_bufs` | av2/decoder/decodeframe.c:5323 | 14 | 1 | Keep as-is (local) |
| 859 | Info | `allocate_mc_tmp_buf` | av2/decoder/decodeframe.c:5338 | 20 | 1 | Keep as-is (local) |
| 860 | Info | `reset_dec_workers` | av2/decoder/decodeframe.c:5359 | 36 | 1 | Keep as-is (local) |
| 861 | Info | `launch_dec_workers` | av2/decoder/decodeframe.c:5396 | 19 | 1 | Keep as-is (local) |
| 862 | Info | `sync_dec_workers` | av2/decoder/decodeframe.c:5416 | 11 | 1 | Keep as-is (local) |
| 863 | Info | `decode_mt_init` | av2/decoder/decodeframe.c:5428 | 50 | 1 | Keep as-is (local) |
| 864 | Info | `tile_mt_queue` | av2/decoder/decodeframe.c:5479 | 16 | 1 | Keep as-is (local) |
| 865 | Info | `dec_alloc_cb_buf` | av2/decoder/decodeframe.c:5565 | 13 | 1 | Keep as-is (local) |
| 866 | Info | `row_mt_frame_init` | av2/decoder/decodeframe.c:5579 | 63 | 1 | Keep as-is (local) |
| 867 | Info | `error_handler` | av2/decoder/decodeframe.c:5743 | 5 | 1 | Keep as-is (local) |
| 868 | Info | `read_bitdepth` | av2/decoder/decodeframe.c:5760 | 12 | 1 | Keep as-is (local) |
| 869 | Info | `read_mfh_sb_size` | av2/decoder/decodeframe.c:6366 | 21 | 1 | Keep as-is (local) |
| 870 | Info | `read_multi_frame_header_seg_info` | av2/decoder/decodeframe.c:6388 | 9 | 1 | Keep as-is (local) |
| 871 | Info | `read_global_motion` | av2/decoder/decodeframe.c:6528 | 113 | 1 | Keep as-is (local) |
| 872 | Info | `reset_ref_frame_map` | av2/decoder/decodeframe.c:6644 | 7 | 1 | Keep as-is (local) |
| 873 | Info | `validate_refereces` | av2/decoder/decodeframe.c:6654 | 15 | 1 | Keep as-is (local) |
| 874 | Info | `reset_frame_buffers` | av2/decoder/decodeframe.c:6670 | 20 | 1 | Keep as-is (local) |
| 875 | Info | `get_disp_order_hint` | av2/decoder/decodeframe.c:6691 | 76 | 1 | Keep as-is (local) |
| 876 | Info | `read_intrabc_params` | av2/decoder/decodeframe.c:6781 | 19 | 1 | Keep as-is (local) |
| 877 | Info | `read_screen_content_params` | av2/decoder/decodeframe.c:6800 | 22 | 1 | Keep as-is (local) |
| 878 | Info | `tip_mode_legal_check` | av2/decoder/decodeframe.c:8810 | 50 | 1 | Keep as-is (local) |
| 879 | Info | `process_tip_mode` | av2/decoder/decodeframe.c:8861 | 46 | 1 | Keep as-is (local) |
| 880 | Info | `setup_frame_info` | av2/decoder/decodeframe.c:9183 | 21 | 1 | Keep as-is (local) |
| 881 | Info | `mv_clamp_to_integer` | av2/decoder/decodemv.c:1354 | 9 | 1 | Keep as-is (local) |
| 882 | Info | `assign_dv` | av2/decoder/decodemv.c:1364 | 39 | 1 | Keep as-is (local) |
| 883 | Info | `read_vq_amvd` | av2/decoder/decodemv.c:1846 | 27 | 1 | Keep as-is (local) |
| 884 | Info | `read_mv` | av2/decoder/decodemv.c:1874 | 116 | 1 | Keep as-is (local) |
| 885 | Info | `read_single_ref` | av2/decoder/decodemv.c:2007 | 14 | 1 | Keep as-is (local) |
| 886 | Info | `read_compound_ref` | av2/decoder/decodemv.c:2022 | 50 | 1 | Keep as-is (local) |
| 887 | Info | `read_mb_interp_filter` | av2/decoder/decodemv.c:2118 | 22 | 1 | Keep as-is (local) |
| 888 | Info | `assign_mv` | av2/decoder/decodemv.c:2299 | 230 | 1 | Keep as-is (local) |
| 889 | Info | `dec_init_tip_ref_frame` | av2/decoder/decoder.c:142 | 5 | 1 | Keep as-is (local) |
| 890 | Info | `dec_free_tip_ref_frame` | av2/decoder/decoder.c:148 | 9 | 1 | Keep as-is (local) |
| 891 | Info | `update_long_term_frame_id` | av2/decoder/decoder.c:759 | 17 | 1 | Keep as-is (local) |
| 892 | Info | `get_component_name` | av2/decoder/decoder.h:257 | 13 | 12 | Keep as-is (small utility) |
| 893 | Info | `is_frame_eligible_for_output` | av2/decoder/decoder.h:634 | 4 | 12 | Keep as-is (small utility) |
| 894 | Info | `av2_read_uniform` | av2/decoder/decoder.h:668 | 10 | 12 | Keep as-is (small utility) |
| 895 | Info | `start_timing` | av2/decoder/decoder.h:693 | 3 | 12 | Keep as-is (small utility) |
| 896 | Info | `end_timing` | av2/decoder/decoder.h:696 | 5 | 12 | Keep as-is (small utility) |
| 897 | Info | `get_frame_type_enum` | av2/decoder/decoder.h:702 | 10 | 12 | Keep as-is (small utility) |
| 898 | Info | `read_high_range` | av2/decoder/decodetxb.c:118 | 15 | 1 | Keep as-is (local) |
| 899 | Info | `rec_eob_pos` | av2/decoder/decodetxb.c:134 | 7 | 1 | Keep as-is (local) |
| 900 | Info | `get_dqv` | av2/decoder/decodetxb.c:142 | 8 | 1 | Keep as-is (local) |
| 901 | Info | `read_coeffs_reverse_2d` | av2/decoder/decodetxb.c:162 | 57 | 1 | Keep as-is (local) |
| 902 | Info | `read_coeffs_reverse` | av2/decoder/decodetxb.c:220 | 60 | 1 | Keep as-is (local) |
| 903 | Info | `read_coeffs_forward_2d` | av2/decoder/decodetxb.c:281 | 19 | 1 | Keep as-is (local) |
| 904 | Info | `decode_eob` | av2/decoder/decodetxb.c:302 | 119 | 1 | Keep as-is (local) |
| 905 | Info | `read_coeff_hidden` | av2/decoder/decodetxb.c:645 | 14 | 1 | Keep as-is (local) |
| 906 | Info | `av2_read_color_info` | av2/decoder/obu_ci.c:140 | 16 | 1 | Keep as-is (local) |
| 907 | Info | `av2_read_sample_aspect_ratio_information` | av2/decoder/obu_ci.c:157 | 9 | 1 | Keep as-is (local) |
| 908 | Info | `cyclic_refresh_segment_id_boosted` | av2/encoder/aq_cyclicrefresh.h:255 | 4 | 4 | Keep as-is (small utility) |
| 909 | Info | `cyclic_refresh_segment_id` | av2/encoder/aq_cyclicrefresh.h:260 | 8 | 4 | Keep as-is (small utility) |
| 910 | Info | `highbd_quantize_dc` | av2/encoder/av2_quantize.c:200 | 36 | 1 | Keep as-is (local) |
| 911 | Info | `write_uniform` | av2/encoder/bitstream.c:76 | 11 | 1 | Keep as-is (local) |
| 912 | Info | `write_inter_mode` | av2/encoder/bitstream.c:99 | 35 | 1 | Keep as-is (local) |
| 913 | Info | `write_jmvd_scale_mode` | av2/encoder/bitstream.c:195 | 14 | 1 | Keep as-is (local) |
| 914 | Info | `write_cwp_idx` | av2/encoder/bitstream.c:211 | 14 | 1 | Keep as-is (local) |
| 915 | Info | `write_inter_compound_mode` | av2/encoder/bitstream.c:226 | 44 | 1 | Keep as-is (local) |
| 916 | Info | `write_is_inter` | av2/encoder/bitstream.c:364 | 23 | 1 | Keep as-is (local) |
| 917 | Info | `write_motion_mode` | av2/encoder/bitstream.c:472 | 74 | 1 | Keep as-is (local) |
| 918 | Info | `write_delta_qindex` | av2/encoder/bitstream.c:547 | 21 | 1 | Keep as-is (local) |
| 919 | Info | `pack_map_tokens` | av2/encoder/bitstream.c:569 | 6894 | 1 | Keep as-is (local) |
| 920 | Info | `av2_write_coeffs_txb_facade` | av2/encoder/bitstream.c:619 | 23 | 1 | Keep as-is (local) |
| 921 | Info | `pack_txb_tokens` | av2/encoder/bitstream.c:643 | 50 | 1 | Keep as-is (local) |
| 922 | Info | `set_spatial_segment_id` | av2/encoder/bitstream.c:694 | 15 | 1 | Keep as-is (local) |
| 923 | Info | `write_segment_id` | av2/encoder/bitstream.c:734 | 62 | 1 | Keep as-is (local) |
| 924 | Info | `write_single_ref` | av2/encoder/bitstream.c:797 | 15 | 1 | Keep as-is (local) |
| 925 | Info | `write_compound_ref` | av2/encoder/bitstream.c:813 | 55 | 1 | Keep as-is (local) |
| 926 | Info | `write_ref_frames` | av2/encoder/bitstream.c:870 | 30 | 1 | Keep as-is (local) |
| 927 | Info | `write_intra_dip_mode_info` | av2/encoder/bitstream.c:901 | 21 | 1 | Keep as-is (local) |
| 928 | Info | `write_mb_interp_filter` | av2/encoder/bitstream.c:923 | 33 | 1 | Keep as-is (local) |
| 929 | Info | `delta_encode_palette_colors` | av2/encoder/bitstream.c:960 | 28 | 1 | Keep as-is (local) |
| 930 | Info | `write_palette_colors_y` | av2/encoder/bitstream.c:992 | 20 | 1 | Keep as-is (local) |
| 931 | Info | `write_palette_mode_info` | av2/encoder/bitstream.c:1013 | 19 | 1 | Keep as-is (local) |
| 932 | Info | `write_mrl_index` | av2/encoder/bitstream.c:1232 | 8 | 1 | Keep as-is (local) |
| 933 | Info | `write_multi_line_mrl` | av2/encoder/bitstream.c:1241 | 10 | 1 | Keep as-is (local) |
| 934 | Info | `write_dpcm_index` | av2/encoder/bitstream.c:1252 | 4 | 1 | Keep as-is (local) |
| 935 | Info | `write_dpcm_vert_horz_mode` | av2/encoder/bitstream.c:1257 | 5 | 1 | Keep as-is (local) |
| 936 | Info | `write_dpcm_uv_index` | av2/encoder/bitstream.c:1263 | 5 | 1 | Keep as-is (local) |
| 937 | Info | `write_dpcm_uv_vert_horz_mode` | av2/encoder/bitstream.c:1269 | 4 | 1 | Keep as-is (local) |
| 938 | Info | `write_fsc_mode` | av2/encoder/bitstream.c:1274 | 4 | 1 | Keep as-is (local) |
| 939 | Info | `write_cfl_mhccp_switch` | av2/encoder/bitstream.c:1279 | 6 | 1 | Keep as-is (local) |
| 940 | Info | `write_cfl_index` | av2/encoder/bitstream.c:1286 | 4 | 1 | Keep as-is (local) |
| 941 | Info | `write_mh_dir` | av2/encoder/bitstream.c:1292 | 4 | 1 | Keep as-is (local) |
| 942 | Info | `write_cfl_alphas` | av2/encoder/bitstream.c:1297 | 14 | 1 | Keep as-is (local) |
| 943 | Info | `write_gdf` | av2/encoder/bitstream.c:1312 | 19 | 1 | Keep as-is (local) |
| 944 | Info | `write_cdef` | av2/encoder/bitstream.c:1332 | 42 | 1 | Keep as-is (local) |
| 945 | Info | `write_ccso` | av2/encoder/bitstream.c:1375 | 44 | 1 | Keep as-is (local) |
| 946 | Info | `write_inter_segment_id` | av2/encoder/bitstream.c:1420 | 37 | 1 | Keep as-is (local) |
| 947 | Info | `write_delta_q_params` | av2/encoder/bitstream.c:1460 | 24 | 1 | Keep as-is (local) |
| 948 | Info | `write_intra_luma_mode` | av2/encoder/bitstream.c:1486 | 31 | 1 | Keep as-is (local) |
| 949 | Info | `write_intra_uv_mode` | av2/encoder/bitstream.c:1518 | 21 | 1 | Keep as-is (local) |
| 950 | Info | `write_intra_prediction_modes` | av2/encoder/bitstream.c:1540 | 102 | 1 | Keep as-is (local) |
| 951 | Info | `mode_context_analyzer` | av2/encoder/bitstream.c:1643 | 5 | 1 | Keep as-is (local) |
| 952 | Info | `get_ref_mv_from_stack` | av2/encoder/bitstream.c:1649 | 26 | 1 | Keep as-is (local) |
| 953 | Info | `get_ref_mv` | av2/encoder/bitstream.c:1676 | 9 | 1 | Keep as-is (local) |
| 954 | Info | `pack_inter_mode_mvs` | av2/encoder/bitstream.c:1748 | 423 | 1 | Keep as-is (local) |
| 955 | Info | `write_intrabc_info` | av2/encoder/bitstream.c:2186 | 55 | 1 | Keep as-is (local) |
| 956 | Info | `write_mb_modes_kf` | av2/encoder/bitstream.c:2242 | 91 | 1 | Keep as-is (local) |
| 957 | Info | `dump_mode_info` | av2/encoder/bitstream.c:2335 | 8 | 1 | Keep as-is (local) |
| 958 | Info | `enc_dump_logs` | av2/encoder/bitstream.c:2372 | 55 | 1 | Keep as-is (local) |
| 959 | Info | `write_mbmi_b` | av2/encoder/bitstream.c:2429 | 20 | 1 | Keep as-is (local) |
| 960 | Info | `write_inter_txb_coeff` | av2/encoder/bitstream.c:2450 | 42 | 1 | Keep as-is (local) |
| 961 | Info | `write_tokens_b` | av2/encoder/bitstream.c:2493 | 71 | 1 | Keep as-is (local) |
| 962 | Info | `write_modes_b` | av2/encoder/bitstream.c:2565 | 141 | 1 | Keep as-is (local) |
| 963 | Info | `write_partition` | av2/encoder/bitstream.c:2706 | 112 | 1 | Keep as-is (local) |
| 964 | Info | `write_modes_sb` | av2/encoder/bitstream.c:2819 | 329 | 1 | Keep as-is (local) |
| 965 | Info | `write_modes` | av2/encoder/bitstream.c:3149 | 59 | 1 | Keep as-is (local) |
| 966 | Info | `wb_write_uniform` | av2/encoder/bitstream.c:3210 | 12 | 1 | Keep as-is (local) |
| 967 | Info | `encode_restoration_mode` | av2/encoder/bitstream.c:3235 | 122 | 1 | Keep as-is (local) |
| 968 | Info | `write_match_indices_hdr` | av2/encoder/bitstream.c:3401 | 41 | 1 | Keep as-is (local) |
| 969 | Info | `write_wienerns_framefilters_hdr` | av2/encoder/bitstream.c:3444 | 121 | 1 | Keep as-is (local) |
| 970 | Info | `write_wienerns_filter` | av2/encoder/bitstream.c:3566 | 105 | 1 | Keep as-is (local) |
| 971 | Info | `loop_restoration_write_sb_coeffs` | av2/encoder/bitstream.c:3672 | 55 | 1 | Keep as-is (local) |
| 972 | Info | `encode_loopfilter` | av2/encoder/bitstream.c:3728 | 79 | 1 | Keep as-is (local) |
| 973 | Info | `encode_gdf` | av2/encoder/bitstream.c:3808 | 22 | 1 | Keep as-is (local) |
| 974 | Info | `encode_cdef` | av2/encoder/bitstream.c:3831 | 41 | 1 | Keep as-is (local) |
| 975 | Info | `write_ccso_offset_idx` | av2/encoder/bitstream.c:3874 | 7 | 1 | Keep as-is (local) |
| 976 | Info | `encode_ccso` | av2/encoder/bitstream.c:3881 | 89 | 1 | Keep as-is (local) |
| 977 | Info | `write_delta_q` | av2/encoder/bitstream.c:3971 | 9 | 1 | Keep as-is (local) |
| 978 | Info | `encode_quantization` | av2/encoder/bitstream.c:3981 | 56 | 1 | Keep as-is (local) |
| 979 | Info | `encode_qm_params` | av2/encoder/bitstream.c:4038 | 49 | 1 | Keep as-is (local) |
| 980 | Info | `encode_bru_active_info` | av2/encoder/bitstream.c:4088 | 33 | 1 | Keep as-is (local) |
| 981 | Info | `encode_segmentation` | av2/encoder/bitstream.c:4177 | 43 | 1 | Keep as-is (local) |
| 982 | Info | `write_frame_interp_filter` | av2/encoder/bitstream.c:4221 | 6 | 1 | Keep as-is (local) |
| 983 | Info | `write_tile_info_max_tile` | av2/encoder/bitstream.c:4228 | 47 | 1 | Keep as-is (local) |
| 984 | Info | `write_tile_info` | av2/encoder/bitstream.c:4301 | 30 | 1 | Keep as-is (local) |
| 985 | Info | `write_frame_size` | av2/encoder/bitstream.c:4339 | 21 | 1 | Keep as-is (local) |
| 986 | Info | `write_frame_size_with_refs` | av2/encoder/bitstream.c:4361 | 34 | 1 | Keep as-is (local) |
| 987 | Info | `write_profile` | av2/encoder/bitstream.c:4396 | 5 | 1 | Keep as-is (local) |
| 988 | Info | `write_seq_chroma_format` | av2/encoder/bitstream.c:4403 | 15 | 1 | Keep as-is (local) |
| 989 | Info | `write_bitdepth` | av2/encoder/bitstream.c:4430 | 7 | 1 | Keep as-is (local) |
| 990 | Info | `write_chroma_format_bitdepth` | av2/encoder/bitstream.c:4452 | 5 | 1 | Keep as-is (local) |
| 991 | Info | `write_tile_mfh` | av2/encoder/bitstream.c:4516 | 4 | 1 | Keep as-is (local) |
| 992 | Info | `encode_film_grain` | av2/encoder/bitstream.c:4521 | 14 | 1 | Keep as-is (local) |
| 993 | Info | `write_sb_size` | av2/encoder/bitstream.c:4536 | 17 | 1 | Keep as-is (local) |
| 994 | Info | `write_sequence_header` | av2/encoder/bitstream.c:4884 | 14 | 1 | Keep as-is (local) |
| 995 | Info | `write_mfh_sb_size` | av2/encoder/bitstream.c:4927 | 14 | 1 | Keep as-is (local) |
| 996 | Info | `write_multi_frame_header` | av2/encoder/bitstream.c:4942 | 38 | 1 | Keep as-is (local) |
| 997 | Info | `write_global_motion_params` | av2/encoder/bitstream.c:4981 | 54 | 1 | Keep as-is (local) |
| 998 | Info | `write_global_motion` | av2/encoder/bitstream.c:5036 | 98 | 1 | Keep as-is (local) |
| 999 | Info | `write_screen_content_params` | av2/encoder/bitstream.c:5135 | 22 | 1 | Keep as-is (local) |
| 1000 | Info | `write_intrabc_params` | av2/encoder/bitstream.c:5158 | 18 | 1 | Keep as-is (local) |
| 1001 | Info | `write_show_existing_frame` | av2/encoder/bitstream.c:5176 | 15 | 1 | Keep as-is (local) |
| 1002 | Info | `write_frame_opfl_refine_type` | av2/encoder/bitstream.c:5192 | 11 | 1 | Keep as-is (local) |
| 1003 | Info | `write_uncompressed_header` | av2/encoder/bitstream.c:5205 | 562 | 1 | Keep as-is (local) |
| 1004 | Info | `mem_put_varsize` | av2/encoder/bitstream.c:5788 | 10 | 1 | Keep as-is (local) |
| 1005 | Info | `write_bitstream_level` | av2/encoder/bitstream.c:5914 | 5 | 1 | Keep as-is (local) |
| 1006 | Info | `is_rect_tx_allowed` | av2/encoder/block.h:1705 | 6 | 6 | Keep as-is (small utility) |
| 1007 | Info | `set_blk_skip` | av2/encoder/block.h:1712 | 10 | 6 | Keep as-is (small utility) |
| 1008 | Info | `is_blk_skip` | av2/encoder/block.h:1723 | 10 | 6 | Keep as-is (small utility) |
| 1009 | Info | `should_reuse_mode` | av2/encoder/block.h:1734 | 3 | 6 | Keep as-is (small utility) |
| 1010 | Info | `cnn_has_at_least_one_output` | av2/encoder/cnn.c:222 | 11 | 1 | Keep as-is (local) |
| 1011 | Info | `get_start_shift_convolve` | av2/encoder/cnn.c:301 | 7 | 1 | Keep as-is (local) |
| 1012 | Info | `get_start_shift_deconvolve` | av2/encoder/cnn.c:690 | 4 | 1 | Keep as-is (local) |
| 1013 | Info | `is_comp_rd_match` | av2/encoder/compound_type.c:29 | 55 | 1 | Keep as-is (local) |
| 1014 | Info | `find_comp_rd_in_stats` | av2/encoder/compound_type.c:87 | 19 | 1 | Keep as-is (local) |
| 1015 | Info | `enable_wedge_search` | av2/encoder/compound_type.c:107 | 6 | 1 | Keep as-is (local) |
| 1016 | Info | `enable_wedge_interinter_search` | av2/encoder/compound_type.c:114 | 6 | 1 | Keep as-is (local) |
| 1017 | Info | `enable_wedge_interintra_search` | av2/encoder/compound_type.c:121 | 6 | 1 | Keep as-is (local) |
| 1018 | Info | `get_inter_predictor_masked_compound_y` | av2/encoder/compound_type.c:436 | 15 | 1 | Keep as-is (local) |
| 1019 | Info | `compute_best_interintra_mode` | av2/encoder/compound_type.c:453 | 27 | 1 | Keep as-is (local) |
| 1020 | Info | `compute_rd_thresh` | av2/encoder/compound_type.c:511 | 9 | 1 | Keep as-is (local) |
| 1021 | Info | `compute_best_wedge_interintra` | av2/encoder/compound_type.c:522 | 32 | 1 | Keep as-is (local) |
| 1022 | Info | `compute_valid_comp_types` | av2/encoder/compound_type.c:884 | 47 | 1 | Keep as-is (local) |
| 1023 | Info | `calc_masked_type_cost` | av2/encoder/compound_type.c:933 | 23 | 1 | Keep as-is (local) |
| 1024 | Info | `update_mbmi_for_compound_type` | av2/encoder/compound_type.c:958 | 6 | 1 | Keep as-is (local) |
| 1025 | Info | `populate_reuse_comp_type_data` | av2/encoder/compound_type.c:968 | 19 | 1 | Keep as-is (local) |
| 1026 | Info | `update_best_info` | av2/encoder/compound_type.c:989 | 10 | 1 | Keep as-is (local) |
| 1027 | Info | `update_mask_best_mv` | av2/encoder/compound_type.c:1001 | 16 | 1 | Keep as-is (local) |
| 1028 | Info | `save_comp_rd_search_stat` | av2/encoder/compound_type.c:1018 | 31 | 1 | Keep as-is (local) |
| 1029 | Info | `get_interinter_compound_mask_rate` | av2/encoder/compound_type.c:1050 | 15 | 1 | Keep as-is (local) |
| 1030 | Info | `backup_stats` | av2/encoder/compound_type.c:1067 | 11 | 1 | Keep as-is (local) |
| 1031 | Info | `get_num_pix_bsize_base` | av2/encoder/context_tree.c:89 | 18 | 1 | Keep as-is (local) |
| 1032 | Info | `get_pc_tree_nodes` | av2/encoder/context_tree.c:719 | 11 | 1 | Keep as-is (local) |
| 1033 | Info | `update_keyframe_counters` | av2/encoder/encode_strategy.c:131 | 6 | 1 | Keep as-is (local) |
| 1034 | Info | `is_frame_droppable` | av2/encoder/encode_strategy.c:138 | 9 | 1 | Keep as-is (local) |
| 1035 | Info | `update_frames_till_gf_update` | av2/encoder/encode_strategy.c:148 | 12 | 1 | Keep as-is (local) |
| 1036 | Info | `update_gf_group_index` | av2/encoder/encode_strategy.c:161 | 7 | 1 | Keep as-is (local) |
| 1037 | Info | `set_show_existing_alt_ref` | av2/encoder/encode_strategy.c:171 | 20 | 1 | Keep as-is (local) |
| 1038 | Info | `setup_delta_q` | av2/encoder/encodeframe.c:215 | 69 | 1 | Keep as-is (local) |
| 1039 | Info | `adjust_rdmult_tpl_model` | av2/encoder/encodeframe.c:383 | 17 | 1 | Keep as-is (local) |
| 1040 | Info | `init_encode_rd_sb` | av2/encoder/encodeframe.c:428 | 54 | 1 | Keep as-is (local) |
| 1041 | Info | `perform_one_partition_pass` | av2/encoder/encodeframe.c:497 | 60 | 1 | Keep as-is (local) |
| 1042 | Info | `perform_two_partition_passes` | av2/encoder/encodeframe.c:564 | 32 | 1 | Keep as-is (local) |
| 1043 | Info | `set_min_none_to_invalid` | av2/encoder/encodeframe.c:601 | 35 | 1 | Keep as-is (local) |
| 1044 | Info | `perform_two_pass_partition_search` | av2/encoder/encodeframe.c:645 | 37 | 1 | Keep as-is (local) |
| 1045 | Info | `encode_rd_sb` | av2/encoder/encodeframe.c:689 | 149 | 1 | Keep as-is (local) |
| 1046 | Info | `bridge_frame_set_offsets` | av2/encoder/encodeframe.c:839 | 65 | 1 | Keep as-is (local) |
| 1047 | Info | `bridge_frame_predict_inter_block` | av2/encoder/encodeframe.c:905 | 36 | 1 | Keep as-is (local) |
| 1048 | Info | `bridge_frame_decode_partition` | av2/encoder/encodeframe.c:972 | 356 | 1 | Keep as-is (local) |
| 1049 | Info | `bridge_frame_decode_partition_sb` | av2/encoder/encodeframe.c:1329 | 15 | 1 | Keep as-is (local) |
| 1050 | Info | `encode_sb_row` | av2/encoder/encodeframe.c:1353 | 173 | 1 | Keep as-is (local) |
| 1051 | Info | `init_encode_frame_mb_context` | av2/encoder/encodeframe.c:1527 | 12 | 1 | Keep as-is (local) |
| 1052 | Info | `encode_tiles` | av2/encoder/encodeframe.c:1724 | 29 | 1 | Keep as-is (local) |
| 1053 | Info | `set_rel_frame_dist` | av2/encoder/encodeframe.c:1755 | 28 | 1 | Keep as-is (local) |
| 1054 | Info | `refs_are_one_sided` | av2/encoder/encodeframe.c:1784 | 7 | 1 | Keep as-is (local) |
| 1055 | Info | `set_default_interp_skip_flags` | av2/encoder/encodeframe.c:1823 | 7 | 1 | Keep as-is (local) |
| 1056 | Info | `could_tip_mode_be_selected` | av2/encoder/encodeframe.c:1835 | 27 | 1 | Keep as-is (local) |
| 1057 | Info | `decide_tip_setting_and_setup_tip_frame` | av2/encoder/encodeframe.c:1863 | 38 | 1 | Keep as-is (local) |
| 1058 | Info | `av2_enc_setup_tip_frame` | av2/encoder/encodeframe.c:1902 | 38 | 1 | Keep as-is (local) |
| 1059 | Info | `encode_frame_internal` | av2/encoder/encodeframe.c:2035 | 300 | 1 | Keep as-is (local) |
| 1060 | Info | `set_deltaq_rdmult` | av2/encoder/encodeframe_utils.c:23 | 7 | 1 | Keep as-is (local) |
| 1061 | Info | `update_filter_type_count` | av2/encoder/encodeframe_utils.c:138 | 6 | 1 | Keep as-is (local) |
| 1062 | Info | `copy_mbmi_ext_frame_to_mbmi_ext` | av2/encoder/encodeframe_utils.c:147 | 41 | 1 | Keep as-is (local) |
| 1063 | Info | `update_fsc_cdf` | av2/encoder/encodeframe_utils.c:432 | 18 | 1 | Keep as-is (local) |
| 1064 | Info | `update_filter_type_cdf` | av2/encoder/encodeframe_utils.h:206 | 6 | 7 | Keep as-is (small utility) |
| 1065 | Info | `set_segment_rdmult` | av2/encoder/encodeframe_utils.h:213 | 13 | 7 | Keep as-is (small utility) |
| 1066 | Info | `alloc_inter_modes_info_data` | av2/encoder/encodeframe_utils.h:271 | 6 | 7 | Keep as-is (small utility) |
| 1067 | Info | `dealloc_inter_modes_info_data` | av2/encoder/encodeframe_utils.h:279 | 4 | 7 | Keep as-is (small utility) |
| 1068 | Info | `is_bsize_square` | av2/encoder/encodeframe_utils.h:352 | 3 | 7 | Keep as-is (small utility) |
| 1069 | Info | `avg_wxh_block_c` | av2/encoder/encodemb.c:42 | 11 | 1 | Keep as-is (local) |
| 1070 | Info | `avg_wxh_block_horiz_c` | av2/encoder/encodemb.c:55 | 12 | 1 | Keep as-is (local) |
| 1071 | Info | `avg_wxh_block_vert_c` | av2/encoder/encodemb.c:69 | 14 | 1 | Keep as-is (local) |
| 1072 | Info | `fill_residue_outside_frame` | av2/encoder/encodemb.c:86 | 64 | 1 | Keep as-is (local) |
| 1073 | Info | `get_dqv` | av2/encoder/encodemb.c:358 | 8 | 1 | Keep as-is (local) |
| 1074 | Info | `av2_set_txb_context` | av2/encoder/encodemb.h:182 | 7 | 5 | Keep as-is (small utility) |
| 1075 | Info | `is_trellis_used` | av2/encoder/encodemb.h:201 | 7 | 5 | Keep as-is (small utility) |
| 1076 | Info | `av2_get_mv_joint` | av2/encoder/encodemv.h:47 | 7 | 15 | Keep as-is (small utility) |
| 1077 | Info | `av2_log_in_base_2` | av2/encoder/encodemv.h:56 | 4 | 15 | Keep as-is (small utility) |
| 1078 | Info | `get_shell_class_with_precision` | av2/encoder/encodemv.h:62 | 12 | 15 | Keep as-is (small utility) |
| 1079 | Info | `Scale2Ratio` | av2/encoder/encoder.c:111 | 37 | 1 | Keep as-is (local) |
| 1080 | Info | `does_level_match` | av2/encoder/encoder.c:269 | 12 | 1 | Keep as-is (local) |
| 1081 | Info | `init_frame_info` | av2/encoder/encoder.c:1436 | 15 | 1 | Keep as-is (local) |
| 1082 | Info | `init_tip_ref_frame` | av2/encoder/encoder.c:1452 | 4 | 1 | Keep as-is (local) |
| 1083 | Info | `free_tip_ref_frame` | av2/encoder/encoder.c:1457 | 6 | 1 | Keep as-is (local) |
| 1084 | Info | `terminate_worker_data` | av2/encoder/encoder.c:1718 | 7 | 1 | Keep as-is (local) |
| 1085 | Info | `free_thread_data` | av2/encoder/encoder.c:1727 | 43 | 1 | Keep as-is (local) |
| 1086 | Info | `allow_tip_direct_output` | av2/encoder/encoder.c:3674 | 8 | 1 | Keep as-is (local) |
| 1087 | Info | `compute_tip_direct_output_mode_RD` | av2/encoder/encoder.c:3683 | 165 | 1 | Keep as-is (local) |
| 1088 | Info | `finalize_tip_mode` | av2/encoder/encoder.c:3849 | 131 | 1 | Keep as-is (local) |
| 1089 | Info | `is_lossless_requested` | av2/encoder/encoder.h:1196 | 3 | 31 | Keep as-is (small utility) |
| 1090 | Info | `timebase_units_to_ticks` | av2/encoder/encoder.h:3187 | 4 | 31 | Keep as-is (small utility) |
| 1091 | Info | `ticks_to_timebase_units` | av2/encoder/encoder.h:3192 | 6 | 31 | Keep as-is (small utility) |
| 1092 | Info | `frame_is_kf_gf_arf` | av2/encoder/encoder.h:3199 | 7 | 31 | Keep as-is (small utility) |
| 1093 | Info | `av2_use_hash_me` | av2/encoder/encoder.h:3208 | 6 | 31 | Keep as-is (small utility) |
| 1094 | Info | `get_ref_frame_yv12_buf_res_indep` | av2/encoder/encoder.h:3215 | 5 | 31 | Keep as-is (small utility) |
| 1095 | Info | `get_ref_frame_yv12_buf` | av2/encoder/encoder.h:3221 | 5 | 31 | Keep as-is (small utility) |
| 1096 | Info | `alloc_frame_mvs` | av2/encoder/encoder.h:3227 | 6 | 31 | Keep as-is (small utility) |
| 1097 | Info | `allocated_tokens` | av2/encoder/encoder.h:3236 | 7 | 31 | Keep as-is (small utility) |
| 1098 | Info | `is_altref_enabled` | av2/encoder/encoder.h:3263 | 3 | 31 | Keep as-is (small utility) |
| 1099 | Info | `is_stat_generation_stage` | av2/encoder/encoder.h:3268 | 5 | 31 | Keep as-is (small utility) |
| 1100 | Info | `is_stat_consumption_stage` | av2/encoder/encoder.h:3275 | 3 | 31 | Keep as-is (small utility) |
| 1101 | Info | `has_no_stats_stage` | av2/encoder/encoder.h:3288 | 4 | 31 | Keep as-is (small utility) |
| 1102 | Info | `get_stats_buf_size` | av2/encoder/encoder.h:3295 | 4 | 31 | Keep as-is (small utility) |
| 1103 | Info | `set_ref_ptrs` | av2/encoder/encoder.h:3302 | 8 | 31 | Keep as-is (small utility) |
| 1104 | Info | `cond_cost_list_const` | av2/encoder/encoder.h:3311 | 6 | 31 | Keep as-is (small utility) |
| 1105 | Info | `cond_cost_list` | av2/encoder/encoder.h:3318 | 5 | 31 | Keep as-is (small utility) |
| 1106 | Info | `get_mi_ext_idx` | av2/encoder/encoder.h:3328 | 8 | 31 | Keep as-is (small utility) |
| 1107 | Info | `set_mode_info_offsets` | av2/encoder/encoder.h:3339 | 13 | 31 | Keep as-is (small utility) |
| 1108 | Info | `get_max_allowed_ref_frames` | av2/encoder/encoder.h:3373 | 7 | 31 | Keep as-is (small utility) |
| 1109 | Info | `has_second_drl_by_mode` | av2/encoder/encoder.h:3383 | 5 | 31 | Keep as-is (small utility) |
| 1110 | Info | `is_frame_tpl_eligible` | av2/encoder/encoder.h:3433 | 6 | 31 | Keep as-is (small utility) |
| 1111 | Info | `is_frame_eligible_for_ref_pruning` | av2/encoder/encoder.h:3440 | 7 | 31 | Keep as-is (small utility) |
| 1112 | Info | `get_frame_update_type` | av2/encoder/encoder.h:3449 | 4 | 31 | Keep as-is (small utility) |
| 1113 | Info | `av2_pixels_to_mi` | av2/encoder/encoder.h:3454 | 3 | 31 | Keep as-is (small utility) |
| 1114 | Info | `is_psnr_calc_enabled` | av2/encoder/encoder.h:3458 | 6 | 31 | Keep as-is (small utility) |
| 1115 | Info | `av2_get_bsize_idx_for_part_stats` | av2/encoder/encoder.h:3502 | 14 | 31 | Keep as-is (small utility) |
| 1116 | Info | `start_timing` | av2/encoder/encoder.h:3519 | 3 | 31 | Keep as-is (small utility) |
| 1117 | Info | `end_timing` | av2/encoder/encoder.h:3522 | 5 | 31 | Keep as-is (small utility) |
| 1118 | Info | `get_frame_type_enum` | av2/encoder/encoder.h:3528 | 10 | 31 | Keep as-is (small utility) |
| 1119 | Info | `av2_is_shown_keyframe` | av2/encoder/encoder.h:3568 | 4 | 31 | Keep as-is (small utility) |
| 1120 | Info | `dealloc_context_buffers_ext` | av2/encoder/encoder_alloc.h:31 | 8 | 5 | Keep as-is (small utility) |
| 1121 | Info | `release_compound_type_rd_buffers` | av2/encoder/encoder_alloc.h:135 | 9 | 5 | Keep as-is (small utility) |
| 1122 | Info | `alloc_altref_frame_buffer` | av2/encoder/encoder_alloc.h:305 | 14 | 5 | Keep as-is (small utility) |
| 1123 | Info | `suppress_active_map` | av2/encoder/encoder_utils.h:36 | 9 | 4 | Keep as-is (small utility) |
| 1124 | Info | `enc_set_mb_mi` | av2/encoder/encoder_utils.h:90 | 7 | 4 | Keep as-is (small utility) |
| 1125 | Info | `stat_stage_set_mb_mi` | av2/encoder/encoder_utils.h:98 | 6 | 4 | Keep as-is (small utility) |
| 1126 | Info | `enc_setup_mi` | av2/encoder/encoder_utils.h:105 | 13 | 4 | Keep as-is (small utility) |
| 1127 | Info | `init_buffer_indices` | av2/encoder/encoder_utils.h:119 | 7 | 4 | Keep as-is (small utility) |
| 1128 | Info | `copy_frame_prob_info` | av2/encoder/encoder_utils.h:823 | 10 | 4 | Keep as-is (small utility) |
| 1129 | Info | `equal_dimensions_and_border` | av2/encoder/encoder_utils.h:834 | 7 | 4 | Keep as-is (small utility) |
| 1130 | Info | `combine_prior_with_tpl_boost` | av2/encoder/encoder_utils.h:842 | 14 | 4 | Keep as-is (small utility) |
| 1131 | Info | `set_size_independent_vars` | av2/encoder/encoder_utils.h:857 | 13 | 4 | Keep as-is (small utility) |
| 1132 | Info | `release_scaled_references` | av2/encoder/encoder_utils.h:871 | 10 | 4 | Keep as-is (small utility) |
| 1133 | Info | `update_subgop_ref_stats` | av2/encoder/encoder_utils.h:926 | 12 | 4 | Keep as-is (small utility) |
| 1134 | Info | `get_dqv` | av2/encoder/encodetxb.c:60 | 8 | 1 | Keep as-is (local) |
| 1135 | Info | `get_coeff_dist` | av2/encoder/encodetxb.c:173 | 6 | 1 | Keep as-is (local) |
| 1136 | Info | `get_eob_pos_token` | av2/encoder/encodetxb.c:197 | 14 | 1 | Keep as-is (local) |
| 1137 | Info | `get_br_ph_cost` | av2/encoder/encodetxb.c:328 | 11 | 1 | Keep as-is (local) |
| 1138 | Info | `get_br_cost` | av2/encoder/encodetxb.c:340 | 13 | 1 | Keep as-is (local) |
| 1139 | Info | `get_br_lf_cost_uv` | av2/encoder/encodetxb.c:354 | 11 | 1 | Keep as-is (local) |
| 1140 | Info | `get_br_lf_cost` | av2/encoder/encodetxb.c:366 | 15 | 1 | Keep as-is (local) |
| 1141 | Info | `get_br_cost_with_diff` | av2/encoder/encodetxb.c:382 | 23 | 1 | Keep as-is (local) |
| 1142 | Info | `get_br_lf_cost_with_diff_uv` | av2/encoder/encodetxb.c:406 | 15 | 1 | Keep as-is (local) |
| 1143 | Info | `get_br_lf_cost_with_diff` | av2/encoder/encodetxb.c:422 | 23 | 1 | Keep as-is (local) |
| 1144 | Info | `get_low_range` | av2/encoder/encodetxb.c:446 | 14 | 1 | Keep as-is (local) |
| 1145 | Info | `get_high_range_uv` | av2/encoder/encodetxb.c:461 | 7 | 1 | Keep as-is (local) |
| 1146 | Info | `get_high_range` | av2/encoder/encodetxb.c:469 | 6 | 1 | Keep as-is (local) |
| 1147 | Info | `get_nz_map_ctx_chroma` | av2/encoder/encodetxb.c:476 | 23 | 1 | Keep as-is (local) |
| 1148 | Info | `get_nz_map_ctx` | av2/encoder/encodetxb.c:500 | 24 | 1 | Keep as-is (local) |
| 1149 | Info | `get_nz_map_ctx_skip` | av2/encoder/encodetxb.c:525 | 9 | 1 | Keep as-is (local) |
| 1150 | Info | `code_eob` | av2/encoder/encodetxb.c:622 | 58 | 1 | Keep as-is (local) |
| 1151 | Info | `write_coeff_hidden` | av2/encoder/encodetxb.c:858 | 10 | 1 | Keep as-is (local) |
| 1152 | Info | `update_coeff_eob_fast` | av2/encoder/encodetxb.c:1534 | 30 | 1 | Keep as-is (local) |
| 1153 | Info | `warehouse_efficients_txb_skip` | av2/encoder/encodetxb.c:1565 | 77 | 1 | Keep as-is (local) |
| 1154 | Info | `warehouse_efficients_txb` | av2/encoder/encodetxb.c:1643 | 311 | 1 | Keep as-is (local) |
| 1155 | Info | `warehouse_efficients_txb_laplacian` | av2/encoder/encodetxb.c:1955 | 51 | 1 | Keep as-is (local) |
| 1156 | Info | `get_two_coeff_cost_simple` | av2/encoder/encodetxb.c:2175 | 78 | 1 | Keep as-is (local) |
| 1157 | Info | `get_coeff_cost_bob` | av2/encoder/encodetxb.c:2255 | 19 | 1 | Keep as-is (local) |
| 1158 | Info | `get_coeff_cost_fsc` | av2/encoder/encodetxb.c:2277 | 23 | 1 | Keep as-is (local) |
| 1159 | Info | `get_coeff_cost_eob` | av2/encoder/encodetxb.c:2301 | 68 | 1 | Keep as-is (local) |
| 1160 | Info | `get_coeff_cost_general` | av2/encoder/encodetxb.c:2370 | 85 | 1 | Keep as-is (local) |
| 1161 | Info | `get_qc_dqc_low` | av2/encoder/encodetxb.c:2456 | 16 | 1 | Keep as-is (local) |
| 1162 | Info | `update_coeff_fsc_general` | av2/encoder/encodetxb.c:2475 | 67 | 1 | Keep as-is (local) |
| 1163 | Info | `update_coeff_general` | av2/encoder/encodetxb.c:2543 | 78 | 1 | Keep as-is (local) |
| 1164 | Info | `update_coeff_simple` | av2/encoder/encodetxb.c:2622 | 97 | 1 | Keep as-is (local) |
| 1165 | Info | `update_coeff_bob` | av2/encoder/encodetxb.c:2720 | 118 | 1 | Keep as-is (local) |
| 1166 | Info | `update_coeff_simple_facade` | av2/encoder/encodetxb.c:2842 | 14 | 1 | Keep as-is (local) |
| 1167 | Info | `update_coeff_eob` | av2/encoder/encodetxb.c:2857 | 180 | 1 | Keep as-is (local) |
| 1168 | Info | `update_coeff_eob_facade` | av2/encoder/encodetxb.c:3041 | 17 | 1 | Keep as-is (local) |
| 1169 | Info | `update_skip` | av2/encoder/encodetxb.c:3059 | 19 | 1 | Keep as-is (local) |
| 1170 | Info | `rate_save` | av2/encoder/encodetxb.c:3081 | 32 | 1 | Keep as-is (local) |
| 1171 | Info | `cost_hide_par` | av2/encoder/encodetxb.c:3124 | 40 | 1 | Keep as-is (local) |
| 1172 | Info | `region_nz_minus` | av2/encoder/encodetxb.c:3168 | 26 | 1 | Keep as-is (local) |
| 1173 | Info | `region_nz_equal` | av2/encoder/encodetxb.c:3197 | 54 | 1 | Keep as-is (local) |
| 1174 | Info | `region_nz_plus` | av2/encoder/encodetxb.c:3255 | 23 | 1 | Keep as-is (local) |
| 1175 | Info | `parity_hide_tb` | av2/encoder/encodetxb.c:3279 | 72 | 1 | Keep as-is (local) |
| 1176 | Info | `av2_compute_rdmult_for_plane` | av2/encoder/encodetxb.h:597 | 9 | 14 | Keep as-is (small utility) |
| 1177 | Info | `normalize` | av2/encoder/erp_ml.c:105 | 14 | 1 | Keep as-is (local) |
| 1178 | Info | `accumulate_rd_opt` | av2/encoder/ethread.c:27 | 17 | 1 | Keep as-is (local) |
| 1179 | Info | `assign_tile_to_thread` | av2/encoder/ethread.c:227 | 10 | 1 | Keep as-is (local) |
| 1180 | Info | `get_next_job` | av2/encoder/ethread.c:238 | 13 | 1 | Keep as-is (local) |
| 1181 | Info | `switch_tile_and_get_next_job` | av2/encoder/ethread.c:252 | 61 | 1 | Keep as-is (local) |
| 1182 | Info | `create_enc_workers` | av2/encoder/ethread.c:467 | 114 | 1 | Keep as-is (local) |
| 1183 | Info | `fp_create_enc_workers` | av2/encoder/ethread.c:613 | 44 | 1 | Keep as-is (local) |
| 1184 | Info | `launch_enc_workers` | av2/encoder/ethread.c:658 | 17 | 1 | Keep as-is (local) |
| 1185 | Info | `sync_enc_workers` | av2/encoder/ethread.c:676 | 15 | 1 | Keep as-is (local) |
| 1186 | Info | `accumulate_counters_enc_workers` | av2/encoder/ethread.c:692 | 23 | 1 | Keep as-is (local) |
| 1187 | Info | `prepare_enc_workers` | av2/encoder/ethread.c:716 | 83 | 1 | Keep as-is (local) |
| 1188 | Info | `fp_prepare_enc_workers` | av2/encoder/ethread.c:800 | 28 | 1 | Keep as-is (local) |
| 1189 | Info | `compute_num_enc_row_mt_workers` | av2/encoder/ethread.c:830 | 17 | 1 | Keep as-is (local) |
| 1190 | Info | `compute_num_enc_tile_mt_workers` | av2/encoder/ethread.c:849 | 6 | 1 | Keep as-is (local) |
| 1191 | Info | `compute_max_sb_rows_cols` | av2/encoder/ethread.c:903 | 16 | 1 | Keep as-is (local) |
| 1192 | Info | `fp_compute_max_mb_rows` | av2/encoder/ethread.c:944 | 15 | 1 | Keep as-is (local) |
| 1193 | Info | `prepare_tpl_workers` | av2/encoder/ethread.c:1241 | 29 | 1 | Keep as-is (local) |
| 1194 | Info | `compute_num_tpl_workers` | av2/encoder/ethread.c:1272 | 3 | 1 | Keep as-is (local) |
| 1195 | Info | `get_next_gm_job` | av2/encoder/ethread.c:1308 | 15 | 1 | Keep as-is (local) |
| 1196 | Info | `switch_direction` | av2/encoder/ethread.c:1326 | 7 | 1 | Keep as-is (local) |
| 1197 | Info | `init_gm_thread_data` | av2/encoder/ethread.c:1335 | 11 | 1 | Keep as-is (local) |
| 1198 | Info | `prepare_gm_workers` | av2/encoder/ethread.c:1422 | 14 | 1 | Keep as-is (local) |
| 1199 | Info | `assign_thread_to_dir` | av2/encoder/ethread.c:1438 | 9 | 1 | Keep as-is (local) |
| 1200 | Info | `compute_gm_workers` | av2/encoder/ethread.c:1449 | 10 | 1 | Keep as-is (local) |
| 1201 | Info | `gm_alloc` | av2/encoder/ethread.c:1476 | 27 | 1 | Keep as-is (local) |
| 1202 | Info | `output_stats` | av2/encoder/firstpass.c:59 | 29 | 1 | Keep as-is (local) |
| 1203 | Info | `first_pass_motion_search` | av2/encoder/firstpass.c:200 | 54 | 1 | Keep as-is (local) |
| 1204 | Info | `generic_sad_highbd` | av2/encoder/global_motion.c:86 | 16 | 1 | Keep as-is (local) |
| 1205 | Info | `compute_global_motion_for_ref_frame` | av2/encoder/global_motion_facade.c:79 | 109 | 1 | Keep as-is (local) |
| 1206 | Info | `compute_global_motion_for_references` | av2/encoder/global_motion_facade.c:201 | 26 | 1 | Keep as-is (local) |
| 1207 | Info | `update_valid_ref_frames_for_gm` | av2/encoder/global_motion_facade.c:264 | 66 | 1 | Keep as-is (local) |
| 1208 | Info | `alloc_global_motion_data` | av2/encoder/global_motion_facade.c:332 | 14 | 1 | Keep as-is (local) |
| 1209 | Info | `dealloc_global_motion_data` | av2/encoder/global_motion_facade.c:348 | 8 | 1 | Keep as-is (local) |
| 1210 | Info | `pick_base_gm_params` | av2/encoder/global_motion_facade.c:358 | 144 | 1 | Keep as-is (local) |
| 1211 | Info | `setup_global_motion_info_params` | av2/encoder/global_motion_facade.c:504 | 27 | 1 | Keep as-is (local) |
| 1212 | Info | `global_motion_estimation` | av2/encoder/global_motion_facade.c:533 | 21 | 1 | Keep as-is (local) |
| 1213 | Info | `update_gm_stats` | av2/encoder/global_motion_facade.c:556 | 23 | 1 | Keep as-is (local) |
| 1214 | Info | `has_enough_frames_for_key_filtering` | av2/encoder/gop_structure.h:76 | 6 | 4 | Keep as-is (small utility) |
| 1215 | Info | `is_interp_filter_good_match` | av2/encoder/interp_search.c:20 | 21 | 1 | Keep as-is (local) |
| 1216 | Info | `save_interp_filter_search_stat` | av2/encoder/interp_search.c:42 | 17 | 1 | Keep as-is (local) |
| 1217 | Info | `find_interp_filter_in_stats` | av2/encoder/interp_search.c:60 | 31 | 1 | Keep as-is (local) |
| 1218 | Info | `swap_dst_buf` | av2/encoder/interp_search.c:108 | 7 | 1 | Keep as-is (local) |
| 1219 | Info | `get_switchable_rate` | av2/encoder/interp_search.c:116 | 11 | 1 | Keep as-is (local) |
| 1220 | Info | `interp_model_rd_eval` | av2/encoder/interp_search.c:130 | 31 | 1 | Keep as-is (local) |
| 1221 | Info | `interpolation_filter_rd` | av2/encoder/interp_search.c:163 | 118 | 1 | Keep as-is (local) |
| 1222 | Info | `find_best_non_dual_interp_filter` | av2/encoder/interp_search.c:283 | 14 | 1 | Keep as-is (local) |
| 1223 | Info | `calc_interp_skip_pred_flag` | av2/encoder/interp_search.c:298 | 50 | 1 | Keep as-is (local) |
| 1224 | Info | `get_chroma_idx_for_reuse_uvrd` | av2/encoder/intra_mode_search.c:529 | 14 | 1 | Keep as-is (local) |
| 1225 | Info | `store_uv_mode_rd_info` | av2/encoder/intra_mode_search.c:545 | 7 | 1 | Keep as-is (local) |
| 1226 | Info | `fetch_uv_mode_rd_info` | av2/encoder/intra_mode_search.c:553 | 7 | 1 | Keep as-is (local) |
| 1227 | Info | `intra_block_yrd` | av2/encoder/intra_mode_search.c:931 | 40 | 1 | Keep as-is (local) |
| 1228 | Info | `handle_intra_dip_mode` | av2/encoder/intra_mode_search.c:980 | 87 | 1 | Keep as-is (local) |
| 1229 | Info | `write_uniform_cost` | av2/encoder/intra_mode_search_utils.h:190 | 9 | 2 | Keep as-is (small utility) |
| 1230 | Info | `model_intra_yrd_and_prune` | av2/encoder/intra_mode_search_utils.h:354 | 13 | 2 | Keep as-is (small utility) |
| 1231 | Info | `init_ms_buffers` | av2/encoder/mcomp.c:39 | 6 | 1 | Keep as-is (local) |
| 1232 | Info | `get_faster_search_method` | av2/encoder/mcomp.c:46 | 20 | 1 | Keep as-is (local) |
| 1233 | Info | `get_offset_from_fullmv` | av2/encoder/mcomp.c:184 | 3 | 1 | Keep as-is (local) |
| 1234 | Info | `get_buf_from_fullmv` | av2/encoder/mcomp.c:188 | 4 | 1 | Keep as-is (local) |
| 1235 | Info | `get_vq_col_mvd_cost` | av2/encoder/mcomp.c:381 | 31 | 1 | Keep as-is (local) |
| 1236 | Info | `get_vq_amvd_cost` | av2/encoder/mcomp.c:413 | 19 | 1 | Keep as-is (local) |
| 1237 | Info | `get_vq_mvd_cost` | av2/encoder/mcomp.c:433 | 95 | 1 | Keep as-is (local) |
| 1238 | Info | `get_mv_cost_with_precision` | av2/encoder/mcomp.c:529 | 18 | 1 | Keep as-is (local) |
| 1239 | Info | `get_intrabc_mv_cost_with_precision` | av2/encoder/mcomp.c:548 | 11 | 1 | Keep as-is (local) |
| 1240 | Info | `mv_err_cost` | av2/encoder/mcomp.c:600 | 36 | 1 | Keep as-is (local) |
| 1241 | Info | `mvsad_err_cost` | av2/encoder/mcomp.c:640 | 39 | 1 | Keep as-is (local) |
| 1242 | Info | `check_bounds` | av2/encoder/mcomp.c:998 | 7 | 1 | Keep as-is (local) |
| 1243 | Info | `get_mvpred_var_cost` | av2/encoder/mcomp.c:1010 | 26 | 1 | Keep as-is (local) |
| 1244 | Info | `get_mvpred_sad` | av2/encoder/mcomp.c:1037 | 9 | 1 | Keep as-is (local) |
| 1245 | Info | `get_mvpred_compound_var_cost` | av2/encoder/mcomp.c:1047 | 40 | 1 | Keep as-is (local) |
| 1246 | Info | `set_cmp_weight` | av2/encoder/mcomp.c:1089 | 7 | 1 | Keep as-is (local) |
| 1247 | Info | `get_mvpred_compound_sad` | av2/encoder/mcomp.c:1097 | 30 | 1 | Keep as-is (local) |
| 1248 | Info | `calc_int_cost_list` | av2/encoder/mcomp.c:1135 | 34 | 1 | Keep as-is (local) |
| 1249 | Info | `calc_int_sad_list` | av2/encoder/mcomp.c:1172 | 102 | 1 | Keep as-is (local) |
| 1250 | Info | `get_subpel_part` | av2/encoder/mcomp.c:2789 | 1 | 1 | Keep as-is (local) |
| 1251 | Info | `get_buf_from_mv` | av2/encoder/mcomp.c:2794 | 5 | 1 | Keep as-is (local) |
| 1252 | Info | `estimated_pref_error` | av2/encoder/mcomp.c:2802 | 29 | 1 | Keep as-is (local) |
| 1253 | Info | `check_better_fast` | av2/encoder/mcomp.c:2898 | 29 | 1 | Keep as-is (local) |
| 1254 | Info | `check_better` | av2/encoder/mcomp.c:2930 | 24 | 1 | Keep as-is (local) |
| 1255 | Info | `get_best_diag_step` | av2/encoder/mcomp.c:2955 | 9 | 1 | Keep as-is (local) |
| 1256 | Info | `first_level_check_fast` | av2/encoder/mcomp.c:2968 | 39 | 1 | Keep as-is (local) |
| 1257 | Info | `second_level_check_fast` | av2/encoder/mcomp.c:3010 | 61 | 1 | Keep as-is (local) |
| 1258 | Info | `two_level_checks_fast` | av2/encoder/mcomp.c:3075 | 15 | 1 | Keep as-is (local) |
| 1259 | Info | `first_level_check` | av2/encoder/mcomp.c:3091 | 35 | 1 | Keep as-is (local) |
| 1260 | Info | `second_level_check_v2` | av2/encoder/mcomp.c:3130 | 35 | 1 | Keep as-is (local) |
| 1261 | Info | `divide_and_round` | av2/encoder/mcomp.c:3217 | 3 | 1 | Keep as-is (local) |
| 1262 | Info | `is_cost_list_wellbehaved` | av2/encoder/mcomp.c:3221 | 4 | 1 | Keep as-is (local) |
| 1263 | Info | `get_cost_surf_min` | av2/encoder/mcomp.c:3234 | 7 | 1 | Keep as-is (local) |
| 1264 | Info | `check_repeated_mv_and_update` | av2/encoder/mcomp.c:3245 | 11 | 1 | Keep as-is (local) |
| 1265 | Info | `setup_center_error_facade` | av2/encoder/mcomp.c:3257 | 13 | 1 | Keep as-is (local) |
| 1266 | Info | `second_level_check_v2_intraBC` | av2/encoder/mcomp.c:4314 | 50 | 1 | Keep as-is (local) |
| 1267 | Info | `compute_motion_cost` | av2/encoder/mcomp.c:4615 | 28 | 1 | Keep as-is (local) |
| 1268 | Info | `av2_set_ms_compound_refs` | av2/encoder/mcomp.h:119 | 9 | 11 | Keep as-is (small utility) |
| 1269 | Info | `av2_set_mv_row_limits` | av2/encoder/mcomp.h:278 | 12 | 11 | Keep as-is (small utility) |
| 1270 | Info | `av2_set_mv_col_limits` | av2/encoder/mcomp.h:291 | 12 | 11 | Keep as-is (small utility) |
| 1271 | Info | `av2_set_mv_limits` | av2/encoder/mcomp.h:304 | 6 | 11 | Keep as-is (small utility) |
| 1272 | Info | `get_opfl_mv_upshift_bits` | av2/encoder/mcomp.h:325 | 5 | 11 | Keep as-is (small utility) |
| 1273 | Info | `av2_set_fractional_mv` | av2/encoder/mcomp.h:598 | 5 | 11 | Keep as-is (small utility) |
| 1274 | Info | `av2_lower_mv_limit` | av2/encoder/mcomp.h:627 | 4 | 11 | Keep as-is (small utility) |
| 1275 | Info | `av2_is_subpelmv_in_range` | av2/encoder/mcomp.h:676 | 5 | 11 | Keep as-is (small utility) |
| 1276 | Info | `is_sub_pel_bv_valid` | av2/encoder/mcomp.h:702 | 11 | 11 | Keep as-is (small utility) |
| 1277 | Info | `compute_sse_plane` | av2/encoder/model_rd.h:60 | 12 | 4 | Keep as-is (small utility) |
| 1278 | Info | `build_second_inter_pred` | av2/encoder/motion_search_facade.c:1175 | 36 | 1 | Keep as-is (local) |
| 1279 | Info | `do_masked_motion_search_indexed` | av2/encoder/motion_search_facade.c:1236 | 27 | 1 | Keep as-is (local) |
| 1280 | Info | `get_ref_mv_for_mv_stats` | av2/encoder/mv_prec.c:24 | 31 | 1 | Keep as-is (local) |
| 1281 | Info | `get_symbol_cost` | av2/encoder/mv_prec.c:56 | 7 | 1 | Keep as-is (local) |
| 1282 | Info | `get_quniform_costs` | av2/encoder/mv_prec.c:65 | 11 | 1 | Keep as-is (local) |
| 1283 | Info | `get_vq_col_mvd_rate` | av2/encoder/mv_prec.c:76 | 26 | 1 | Keep as-is (local) |
| 1284 | Info | `get_truncated_unary_rate` | av2/encoder/mv_prec.c:102 | 20 | 1 | Keep as-is (local) |
| 1285 | Info | `get_vq_mvd_rate` | av2/encoder/mv_prec.c:123 | 111 | 1 | Keep as-is (local) |
| 1286 | Info | `keep_vq_one_mv_stat` | av2/encoder/mv_prec.c:235 | 68 | 1 | Keep as-is (local) |
| 1287 | Info | `collect_mv_stats_b` | av2/encoder/mv_prec.c:304 | 83 | 1 | Keep as-is (local) |
| 1288 | Info | `collect_mv_stats_sb` | av2/encoder/mv_prec.c:389 | 121 | 1 | Keep as-is (local) |
| 1289 | Info | `collect_mv_stats_tile` | av2/encoder/mv_prec.c:511 | 18 | 1 | Keep as-is (local) |
| 1290 | Info | `get_smart_mv_prec` | av2/encoder/mv_prec.c:553 | 39 | 1 | Keep as-is (local) |
| 1291 | Info | `av2_frame_allows_smart_mv` | av2/encoder/mv_prec.h:23 | 8 | 3 | Keep as-is (small utility) |
| 1292 | Info | `av2_set_high_precision_mv` | av2/encoder/mv_prec.h:32 | 6 | 3 | Keep as-is (small utility) |
| 1293 | Info | `extend_palette_color_map` | av2/encoder/palette.c:134 | 20 | 1 | Keep as-is (local) |
| 1294 | Info | `optimize_palette_colors` | av2/encoder/palette.c:158 | 19 | 1 | Keep as-is (local) |
| 1295 | Info | `palette_rd_y` | av2/encoder/palette.c:185 | 77 | 1 | Keep as-is (local) |
| 1296 | Info | `is_iter_over` | av2/encoder/palette.c:263 | 4 | 1 | Keep as-is (local) |
| 1297 | Info | `perform_top_color_palette_search` | av2/encoder/palette.c:272 | 35 | 1 | Keep as-is (local) |
| 1298 | Info | `perform_k_means_palette_search` | av2/encoder/palette.c:312 | 39 | 1 | Keep as-is (local) |
| 1299 | Info | `set_stage2_params` | av2/encoder/palette.c:353 | 15 | 1 | Keep as-is (local) |
| 1300 | Info | `av2_calc_indices` | av2/encoder/palette.h:61 | 12 | 7 | Keep as-is (small utility) |
| 1301 | Info | `av2_k_means` | av2/encoder/palette.h:94 | 13 | 7 | Keep as-is (small utility) |
| 1302 | Info | `av2_ml_part_split_features_square` | av2/encoder/partition_ml.c:111 | 102 | 1 | Keep as-is (local) |
| 1303 | Info | `av2_ml_part_split_features_none` | av2/encoder/partition_ml.c:214 | 76 | 1 | Keep as-is (local) |
| 1304 | Info | `av2_ml_part_split_features` | av2/encoder/partition_ml.c:291 | 59 | 1 | Keep as-is (local) |
| 1305 | Info | `update_best_level_banks` | av2/encoder/partition_search.c:2856 | 7 | 1 | Keep as-is (local) |
| 1306 | Info | `restore_level_banks` | av2/encoder/partition_search.c:2864 | 7 | 1 | Keep as-is (local) |
| 1307 | Info | `get_forced_partition_type` | av2/encoder/partition_search.c:2872 | 30 | 1 | Keep as-is (local) |
| 1308 | Info | `init_allowed_partitions` | av2/encoder/partition_search.c:2903 | 80 | 1 | Keep as-is (local) |
| 1309 | Info | `is_part_pruned_by_forced_partition` | av2/encoder/partition_search.c:3213 | 5 | 1 | Keep as-is (local) |
| 1310 | Info | `is_rect_part_allowed` | av2/encoder/partition_search.c:3222 | 15 | 1 | Keep as-is (local) |
| 1311 | Info | `prune_rect_with_none_rd` | av2/encoder/partition_search.c:3238 | 25 | 1 | Keep as-is (local) |
| 1312 | Info | `set_part_none_allowed_flag` | av2/encoder/partition_search.c:3462 | 16 | 1 | Keep as-is (local) |
| 1313 | Info | `node_uses_horz` | av2/encoder/partition_search.c:3984 | 7 | 1 | Keep as-is (local) |
| 1314 | Info | `node_uses_vert` | av2/encoder/partition_search.c:3993 | 7 | 1 | Keep as-is (local) |
| 1315 | Info | `trace_partition_boundary` | av2/encoder/partition_search.c:4069 | 155 | 1 | Keep as-is (local) |
| 1316 | Info | `prune_part_3_with_partition_boundary` | av2/encoder/partition_search.c:4230 | 77 | 1 | Keep as-is (local) |
| 1317 | Info | `prune_part_4_with_partition_boundary` | av2/encoder/partition_search.c:4313 | 95 | 1 | Keep as-is (local) |
| 1318 | Info | `prune_ext_partitions_3way` | av2/encoder/partition_search.c:4410 | 87 | 1 | Keep as-is (local) |
| 1319 | Info | `early_termination_inter_sdp` | av2/encoder/partition_search.c:4499 | 88 | 1 | Keep as-is (local) |
| 1320 | Info | `search_intra_region_partitioning` | av2/encoder/partition_search.c:4589 | 105 | 1 | Keep as-is (local) |
| 1321 | Info | `prune_ext_partitions_4way` | av2/encoder/partition_search.c:4696 | 233 | 1 | Keep as-is (local) |
| 1322 | Info | `search_partition_horz_3` | av2/encoder/partition_search.c:5386 | 116 | 1 | Keep as-is (local) |
| 1323 | Info | `search_partition_vert_3` | av2/encoder/partition_search.c:5504 | 116 | 1 | Keep as-is (local) |
| 1324 | Info | `get_partition_depth` | av2/encoder/partition_search.c:5621 | 83 | 1 | Keep as-is (local) |
| 1325 | Info | `try_none_after_rect` | av2/encoder/partition_search.c:5705 | 66 | 1 | Keep as-is (local) |
| 1326 | Info | `prune_none_with_rect_results` | av2/encoder/partition_search.c:5774 | 32 | 1 | Keep as-is (local) |
| 1327 | Info | `convert_bsize_to_idx` | av2/encoder/partition_strategy.c:47 | 10 | 1 | Keep as-is (local) |
| 1328 | Info | `simple_motion_search_prune_part_features` | av2/encoder/partition_strategy.c:393 | 119 | 1 | Keep as-is (local) |
| 1329 | Info | `get_sms_count_from_length` | av2/encoder/partition_strategy.c:1008 | 12 | 1 | Keep as-is (local) |
| 1330 | Info | `get_sms_arr_1d_idx` | av2/encoder/partition_strategy.c:1023 | 15 | 1 | Keep as-is (local) |
| 1331 | Info | `get_sms_arr` | av2/encoder/partition_strategy.c:1045 | 99 | 1 | Keep as-is (local) |
| 1332 | Info | `add_start_mv_to_block` | av2/encoder/partition_strategy.c:1295 | 12 | 1 | Keep as-is (local) |
| 1333 | Info | `add_start_mv_to_partition` | av2/encoder/partition_strategy.c:1308 | 68 | 1 | Keep as-is (local) |
| 1334 | Info | `clip_rate` | av2/encoder/partition_strategy.c:1428 | 6 | 1 | Keep as-is (local) |
| 1335 | Info | `av2_add_mode_search_context_to_cache` | av2/encoder/partition_strategy.h:139 | 13 | 5 | Keep as-is (small utility) |
| 1336 | Info | `av2_set_best_mode_cache` | av2/encoder/partition_strategy.h:152 | 12 | 5 | Keep as-is (small utility) |
| 1337 | Info | `av2_init_sms_data_bufs` | av2/encoder/partition_strategy.h:230 | 3 | 5 | Keep as-is (small utility) |
| 1338 | Info | `is_full_sb` | av2/encoder/partition_strategy.h:241 | 8 | 5 | Keep as-is (small utility) |
| 1339 | Info | `is_almost_static` | av2/encoder/pass2_strategy.c:910 | 13 | 1 | Keep as-is (local) |
| 1340 | Info | `detect_gf_cut` | av2/encoder/pass2_strategy.c:925 | 40 | 1 | Keep as-is (local) |
| 1341 | Info | `set_baseline_gf_interval` | av2/encoder/pass2_strategy.c:1532 | 43 | 1 | Keep as-is (local) |
| 1342 | Info | `reuse_ccso_class_info` | av2/encoder/pickccso.c:65 | 3 | 1 | Keep as-is (local) |
| 1343 | Info | `count_lut_bits` | av2/encoder/pickccso.c:1027 | 24 | 1 | Keep as-is (local) |
| 1344 | Info | `get_cdef_filter_strengths` | av2/encoder/pickcdef.c:49 | 23 | 1 | Keep as-is (local) |
| 1345 | Info | `init_src_params` | av2/encoder/pickcdef.c:240 | 9 | 1 | Keep as-is (local) |
| 1346 | Info | `get_cdef_context` | av2/encoder/pickcdef.c:361 | 41 | 1 | Keep as-is (local) |
| 1347 | Info | `reset_frame_mi_cdef_strength` | av2/encoder/pickcdef.c:403 | 13 | 1 | Keep as-is (local) |
| 1348 | Info | `reset_all_banks` | av2/encoder/pickrst.c:195 | 5 | 1 | Keep as-is (local) |
| 1349 | Info | `rsc_on_tile` | av2/encoder/pickrst.c:255 | 19 | 1 | Keep as-is (local) |
| 1350 | Info | `reset_rsc` | av2/encoder/pickrst.c:275 | 6 | 1 | Keep as-is (local) |
| 1351 | Info | `init_rsc` | av2/encoder/pickrst.c:282 | 37 | 1 | Keep as-is (local) |
| 1352 | Info | `search_pc_wiener_visitor` | av2/encoder/pickrst.c:378 | 84 | 1 | Keep as-is (local) |
| 1353 | Info | `search_norestore_visitor` | av2/encoder/pickrst.c:578 | 19 | 1 | Keep as-is (local) |
| 1354 | Info | `copy_unit_info` | av2/encoder/pickrst.c:2769 | 34 | 1 | Keep as-is (local) |
| 1355 | Info | `bru_set_sru_skip` | av2/encoder/pickrst.c:2804 | 31 | 1 | Keep as-is (local) |
| 1356 | Info | `initialize_stat_weights` | av2/encoder/pickrst.c:3472 | 6 | 1 | Keep as-is (local) |
| 1357 | Info | `lcg_next` | av2/encoder/random.h:25 | 4 | 7 | Keep as-is (small utility) |
| 1358 | Info | `lcg_rand16` | av2/encoder/random.h:31 | 3 | 7 | Keep as-is (small utility) |
| 1359 | Info | `lcg_randint` | av2/encoder/random.h:40 | 4 | 7 | Keep as-is (small utility) |
| 1360 | Info | `lcg_randrange` | av2/encoder/random.h:46 | 5 | 7 | Keep as-is (small utility) |
| 1361 | Info | `set_rc_buffer_sizes` | av2/encoder/rc_utils.h:23 | 13 | 3 | Keep as-is (small utility) |
| 1362 | Info | `av2_get_gfu_boost_projection_factor` | av2/encoder/rc_utils.h:135 | 9 | 3 | Keep as-is (small utility) |
| 1363 | Info | `get_gfu_boost_from_r0_lap` | av2/encoder/rc_utils.h:145 | 8 | 3 | Keep as-is (small utility) |
| 1364 | Info | `av2_get_kf_boost_projection_factor` | av2/encoder/rc_utils.h:154 | 7 | 3 | Keep as-is (small utility) |
| 1365 | Info | `av2_calculate_rd_cost` | av2/encoder/rd.h:176 | 7 | 10 | Keep as-is (small utility) |
| 1366 | Info | `av2_rd_cost_update` | av2/encoder/rd.h:184 | 8 | 10 | Keep as-is (small utility) |
| 1367 | Info | `av2_rd_stats_subtraction` | av2/encoder/rd.h:193 | 14 | 10 | Keep as-is (small utility) |
| 1368 | Info | `reset_thresh_freq_fact` | av2/encoder/rd.h:248 | 7 | 10 | Keep as-is (small utility) |
| 1369 | Info | `av2_set_error_per_bit` | av2/encoder/rd.h:261 | 3 | 10 | Keep as-is (small utility) |
| 1370 | Info | `inter_mode_data_push` | av2/encoder/rdopt.c:266 | 18 | 1 | Keep as-is (local) |
| 1371 | Info | `inter_modes_info_push` | av2/encoder/rdopt.c:285 | 12 | 1 | Keep as-is (local) |
| 1372 | Info | `inter_modes_info_sort` | av2/encoder/rdopt.c:315 | 12 | 1 | Keep as-is (local) |
| 1373 | Info | `get_single_mode` | av2/encoder/rdopt.c:573 | 5 | 1 | Keep as-is (local) |
| 1374 | Info | `estimate_ref_frame_costs` | av2/encoder/rdopt.c:579 | 140 | 1 | Keep as-is (local) |
| 1375 | Info | `store_coding_context` | av2/encoder/rdopt.c:754 | 22 | 1 | Keep as-is (local) |
| 1376 | Info | `setup_buffer_ref_mvs_inter` | av2/encoder/rdopt.c:777 | 64 | 1 | Keep as-is (local) |
| 1377 | Info | `clamp_mv2` | av2/encoder/rdopt.c:846 | 8 | 1 | Keep as-is (local) |
| 1378 | Info | `clamp_and_check_mv` | av2/encoder/rdopt.c:913 | 10 | 1 | Keep as-is (local) |
| 1379 | Info | `clamp_mv_in_range` | av2/encoder/rdopt.c:927 | 18 | 1 | Keep as-is (local) |
| 1380 | Info | `save_comp_mv_search_stat` | av2/encoder/rdopt.c:946 | 98 | 1 | Keep as-is (local) |
| 1381 | Info | `reuse_comp_mv_for_opfl` | av2/encoder/rdopt.c:1045 | 166 | 1 | Keep as-is (local) |
| 1382 | Info | `select_modes_to_search` | av2/encoder/rdopt.c:1629 | 24 | 1 | Keep as-is (local) |
| 1383 | Info | `get_warp_ref_idx_cost` | av2/encoder/rdopt.c:1655 | 19 | 1 | Keep as-is (local) |
| 1384 | Info | `is_this_mvds_valid_for_derivesign` | av2/encoder/rdopt.c:1788 | 32 | 1 | Keep as-is (local) |
| 1385 | Info | `check_repeat_ref_mv` | av2/encoder/rdopt.c:3248 | 41 | 1 | Keep as-is (local) |
| 1386 | Info | `get_this_mv` | av2/encoder/rdopt.c:3290 | 53 | 1 | Keep as-is (local) |
| 1387 | Info | `build_cur_mv` | av2/encoder/rdopt.c:3345 | 51 | 1 | Keep as-is (local) |
| 1388 | Info | `get_skip_drl_cost` | av2/encoder/rdopt.c:3397 | 22 | 1 | Keep as-is (local) |
| 1389 | Info | `is_single_newmv_valid` | av2/encoder/rdopt.c:3462 | 16 | 1 | Keep as-is (local) |
| 1390 | Info | `get_jmvd_scale_mode_cost` | av2/encoder/rdopt.c:3541 | 11 | 1 | Keep as-is (local) |
| 1391 | Info | `mask_set_bit` | av2/encoder/rdopt.c:3648 | 1 | 1 | Keep as-is (local) |
| 1392 | Info | `mask_check_bit` | av2/encoder/rdopt.c:3650 | 3 | 1 | Keep as-is (local) |
| 1393 | Info | `ref_match_found_in_nb_blocks` | av2/encoder/rdopt.c:3881 | 16 | 1 | Keep as-is (local) |
| 1394 | Info | `find_ref_match_in_above_nbs` | av2/encoder/rdopt.c:3898 | 21 | 1 | Keep as-is (local) |
| 1395 | Info | `find_ref_match_in_left_nbs` | av2/encoder/rdopt.c:3920 | 21 | 1 | Keep as-is (local) |
| 1396 | Info | `get_block_level_tpl_stats` | av2/encoder/rdopt.c:3958 | 52 | 1 | Keep as-is (local) |
| 1397 | Info | `prune_modes_based_on_tpl_stats` | av2/encoder/rdopt.c:4011 | 52 | 1 | Keep as-is (local) |
| 1398 | Info | `is_bv_valid` | av2/encoder/rdopt.c:5370 | 16 | 1 | Keep as-is (local) |
| 1399 | Info | `rd_pick_skip_mode` | av2/encoder/rdopt.c:6266 | 274 | 1 | Keep as-is (local) |
| 1400 | Info | `get_winner_mode_stats` | av2/encoder/rdopt.c:6542 | 24 | 1 | Keep as-is (local) |
| 1401 | Info | `refine_winner_mode_tx` | av2/encoder/rdopt.c:6572 | 133 | 1 | Keep as-is (local) |
| 1402 | Info | `disable_reference` | av2/encoder/rdopt.c:6721 | 8 | 1 | Keep as-is (local) |
| 1403 | Info | `disable_inter_references_except_top` | av2/encoder/rdopt.c:6731 | 5 | 1 | Keep as-is (local) |
| 1404 | Info | `default_skip_mask` | av2/encoder/rdopt.c:6750 | 34 | 1 | Keep as-is (local) |
| 1405 | Info | `init_mode_skip_mask` | av2/encoder/rdopt.c:6785 | 62 | 1 | Keep as-is (local) |
| 1406 | Info | `prune_ref_frame` | av2/encoder/rdopt.c:6848 | 29 | 1 | Keep as-is (local) |
| 1407 | Info | `is_ref_frame_used_by_compound_ref` | av2/encoder/rdopt.c:6878 | 13 | 1 | Keep as-is (local) |
| 1408 | Info | `is_ref_frame_used_in_cache` | av2/encoder/rdopt.c:6892 | 14 | 1 | Keep as-is (local) |
| 1409 | Info | `set_params_rd_pick_inter_mode` | av2/encoder/rdopt.c:6909 | 93 | 1 | Keep as-is (local) |
| 1410 | Info | `init_intra_mode_search_state` | av2/encoder/rdopt.c:7003 | 12 | 1 | Keep as-is (local) |
| 1411 | Info | `init_inter_mode_search_state` | av2/encoder/rdopt.c:7016 | 82 | 1 | Keep as-is (local) |
| 1412 | Info | `is_mode_intra` | av2/encoder/rdopt.c:7159 | 3 | 1 | Keep as-is (local) |
| 1413 | Info | `skip_inter_mode_with_cached_mode` | av2/encoder/rdopt.c:7166 | 84 | 1 | Keep as-is (local) |
| 1414 | Info | `init_mbmi` | av2/encoder/rdopt.c:7331 | 51 | 1 | Keep as-is (local) |
| 1415 | Info | `init_submi` | av2/encoder/rdopt.c:7383 | 6 | 1 | Keep as-is (local) |
| 1416 | Info | `collect_single_states` | av2/encoder/rdopt.c:7390 | 50 | 1 | Keep as-is (local) |
| 1417 | Info | `analyze_single_states` | av2/encoder/rdopt.c:7441 | 88 | 1 | Keep as-is (local) |
| 1418 | Info | `match_ref_frame` | av2/encoder/rdopt.c:7633 | 12 | 1 | Keep as-is (local) |
| 1419 | Info | `compound_skip_using_neighbor_refs` | av2/encoder/rdopt.c:7647 | 25 | 1 | Keep as-is (local) |
| 1420 | Info | `update_best_single_mode` | av2/encoder/rdopt.c:7674 | 10 | 1 | Keep as-is (local) |
| 1421 | Info | `skip_compound_using_best_single_mode_ref` | av2/encoder/rdopt.c:7686 | 29 | 1 | Keep as-is (local) |
| 1422 | Info | `update_search_state` | av2/encoder/rdopt.c:7728 | 46 | 1 | Keep as-is (local) |
| 1423 | Info | `find_top_ref` | av2/encoder/rdopt.c:7777 | 15 | 1 | Keep as-is (local) |
| 1424 | Info | `in_single_ref_cutoff` | av2/encoder/rdopt.c:7794 | 7 | 1 | Keep as-is (local) |
| 1425 | Info | `evaluate_motion_mode_for_winner_candidates` | av2/encoder/rdopt.c:7802 | 76 | 1 | Keep as-is (local) |
| 1426 | Info | `is_tip_mode` | av2/encoder/rdopt.c:8195 | 3 | 1 | Keep as-is (local) |
| 1427 | Info | `is_compound_mode_disallowed` | av2/encoder/rdopt.c:8199 | 9 | 1 | Keep as-is (local) |
| 1428 | Info | `init_top_tx_part_rd_for_inter_modes` | av2/encoder/rdopt.c:8210 | 10 | 1 | Keep as-is (local) |
| 1429 | Info | `av2_encoder_get_relative_dist` | av2/encoder/rdopt.h:159 | 4 | 16 | Keep as-is (small utility) |
| 1430 | Info | `av2_get_sb_mi_size` | av2/encoder/rdopt.h:165 | 9 | 16 | Keep as-is (small utility) |
| 1431 | Info | `update_mv_precision` | av2/encoder/rdopt.h:366 | 10 | 16 | Keep as-is (small utility) |
| 1432 | Info | `prune_curr_mv_precision_eval` | av2/encoder/rdopt.h:378 | 11 | 16 | Keep as-is (small utility) |
| 1433 | Info | `restore_dst_buf` | av2/encoder/rdopt_utils.h:29 | 7 | 6 | Keep as-is (small utility) |
| 1434 | Info | `get_rd_thresh_from_best_rd` | av2/encoder/rdopt_utils.h:39 | 11 | 6 | Keep as-is (small utility) |
| 1435 | Info | `inter_mode_data_block_idx` | av2/encoder/rdopt_utils.h:51 | 7 | 6 | Keep as-is (small utility) |
| 1436 | Info | `bsize_to_num_blk` | av2/encoder/rdopt_utils.h:137 | 4 | 6 | Keep as-is (small utility) |
| 1437 | Info | `set_tx_type_prune` | av2/encoder/rdopt_utils.h:244 | 13 | 6 | Keep as-is (small utility) |
| 1438 | Info | `init_sbuv_mode` | av2/encoder/rdopt_utils.h:415 | 4 | 6 | Keep as-is (small utility) |
| 1439 | Info | `av2_resize_scaled` | av2/encoder/scale.h:25 | 3 | 4 | Keep as-is (small utility) |
| 1440 | Info | `av2_frame_scaled` | av2/encoder/scale.h:29 | 3 | 4 | Keep as-is (small utility) |
| 1441 | Info | `init_hl_sf` | av2/encoder/speed_features.c:589 | 9 | 1 | Keep as-is (local) |
| 1442 | Info | `init_tpl_sf` | av2/encoder/speed_features.c:599 | 11 | 1 | Keep as-is (local) |
| 1443 | Info | `init_gm_sf` | av2/encoder/speed_features.c:611 | 7 | 1 | Keep as-is (local) |
| 1444 | Info | `init_part_sf` | av2/encoder/speed_features.c:619 | 46 | 1 | Keep as-is (local) |
| 1445 | Info | `init_mv_sf` | av2/encoder/speed_features.c:666 | 19 | 1 | Keep as-is (local) |
| 1446 | Info | `init_flexmv_sf` | av2/encoder/speed_features.c:686 | 11 | 1 | Keep as-is (local) |
| 1447 | Info | `init_inter_sf` | av2/encoder/speed_features.c:698 | 46 | 1 | Keep as-is (local) |
| 1448 | Info | `init_interp_sf` | av2/encoder/speed_features.c:745 | 3 | 1 | Keep as-is (local) |
| 1449 | Info | `init_intra_sf` | av2/encoder/speed_features.c:749 | 14 | 1 | Keep as-is (local) |
| 1450 | Info | `init_tx_sf` | av2/encoder/speed_features.c:764 | 21 | 1 | Keep as-is (local) |
| 1451 | Info | `init_rd_sf` | av2/encoder/speed_features.c:786 | 30 | 1 | Keep as-is (local) |
| 1452 | Info | `init_winner_mode_sf` | av2/encoder/speed_features.c:817 | 11 | 1 | Keep as-is (local) |
| 1453 | Info | `init_lpf_sf` | av2/encoder/speed_features.c:829 | 7 | 1 | Keep as-is (local) |
| 1454 | Info | `set_erp_speed_features_framesize_dependent` | av2/encoder/speed_features.c:855 | 59 | 1 | Keep as-is (local) |
| 1455 | Info | `set_erp_speed_features` | av2/encoder/speed_features.c:940 | 80 | 1 | Keep as-is (local) |
| 1456 | Info | `set_erp_speed_features_qindex_dependent` | av2/encoder/speed_features.c:1178 | 34 | 1 | Keep as-is (local) |
| 1457 | Info | `compute_square_diff` | av2/encoder/temporal_filter.c:602 | 23 | 1 | Keep as-is (local) |
| 1458 | Info | `get_num_blocks` | av2/encoder/temporal_filter.c:844 | 3 | 1 | Keep as-is (local) |
| 1459 | Info | `get_q` | av2/encoder/temporal_filter.c:849 | 7 | 1 | Keep as-is (local) |
| 1460 | Info | `av2_get_tx_eob` | av2/encoder/tokenize.h:91 | 5 | 14 | Keep as-is (small utility) |
| 1461 | Info | `free_token_info` | av2/encoder/tokenize.h:137 | 7 | 14 | Keep as-is (small utility) |
| 1462 | Info | `get_quantize_error` | av2/encoder/tpl_model.c:37 | 25 | 1 | Keep as-is (local) |
| 1463 | Info | `set_tpl_stats_block_size` | av2/encoder/tpl_model.c:63 | 9 | 1 | Keep as-is (local) |
| 1464 | Info | `tpl_fwd_txfm` | av2/encoder/tpl_model.c:120 | 14 | 1 | Keep as-is (local) |
| 1465 | Info | `tpl_subtract_block` | av2/encoder/tpl_model.c:135 | 8 | 1 | Keep as-is (local) |
| 1466 | Info | `tpl_get_satd_cost` | av2/encoder/tpl_model.c:144 | 14 | 1 | Keep as-is (local) |
| 1467 | Info | `txfm_quant_rdcost` | av2/encoder/tpl_model.c:174 | 19 | 1 | Keep as-is (local) |
| 1468 | Info | `mode_estimation` | av2/encoder/tpl_model.c:299 | 303 | 1 | Keep as-is (local) |
| 1469 | Info | `tpl_model_update_b` | av2/encoder/tpl_model.c:675 | 72 | 1 | Keep as-is (local) |
| 1470 | Info | `tpl_model_update` | av2/encoder/tpl_model.c:748 | 16 | 1 | Keep as-is (local) |
| 1471 | Info | `tpl_model_store` | av2/encoder/tpl_model.c:765 | 40 | 1 | Keep as-is (local) |
| 1472 | Info | `tpl_reset_src_ref_frames` | av2/encoder/tpl_model.c:807 | 6 | 1 | Keep as-is (local) |
| 1473 | Info | `get_gop_length` | av2/encoder/tpl_model.c:814 | 4 | 1 | Keep as-is (local) |
| 1474 | Info | `init_mc_flow_dispenser` | av2/encoder/tpl_model.c:820 | 84 | 1 | Keep as-is (local) |
| 1475 | Info | `mc_flow_dispenser` | av2/encoder/tpl_model.c:945 | 19 | 1 | Keep as-is (local) |
| 1476 | Info | `init_gop_frames_for_tpl` | av2/encoder/tpl_model.c:983 | 235 | 1 | Keep as-is (local) |
| 1477 | Info | `convert_length_to_bsize` | av2/encoder/tpl_model.h:28 | 12 | 8 | Keep as-is (small utility) |
| 1478 | Info | `init_tcq_decision` | av2/encoder/trellis_quant.c:49 | 6 | 1 | Keep as-is (local) |
| 1479 | Info | `init_tcq_ctx` | av2/encoder/trellis_quant.c:61 | 16 | 1 | Keep as-is (local) |
| 1480 | Info | `set_levels_buf` | av2/encoder/trellis_quant.c:79 | 18 | 1 | Keep as-is (local) |
| 1481 | Info | `get_dqv` | av2/encoder/trellis_quant.c:98 | 8 | 1 | Keep as-is (local) |
| 1482 | Info | `get_coeff_dist` | av2/encoder/trellis_quant.c:107 | 6 | 1 | Keep as-is (local) |
| 1483 | Info | `get_coeff_cost_eob` | av2/encoder/trellis_quant.c:114 | 57 | 1 | Keep as-is (local) |
| 1484 | Info | `get_coeff_cost_def` | av2/encoder/trellis_quant.c:172 | 23 | 1 | Keep as-is (local) |
| 1485 | Info | `get_coeff_cost_general` | av2/encoder/trellis_quant.c:196 | 54 | 1 | Keep as-is (local) |
| 1486 | Info | `get_diag_ctx` | av2/encoder/trellis_quant.c:815 | 12 | 1 | Keep as-is (local) |
| 1487 | Info | `get_mid_ctx` | av2/encoder/trellis_quant.h:114 | 3 | 3 | Keep as-is (small utility) |
| 1488 | Info | `get_base_ctx` | av2/encoder/trellis_quant.h:118 | 3 | 3 | Keep as-is (small utility) |
| 1489 | Info | `get_mid_diag_ctx` | av2/encoder/trellis_quant.h:127 | 3 | 3 | Keep as-is (small utility) |
| 1490 | Info | `get_base_diag_ctx` | av2/encoder/trellis_quant.h:131 | 3 | 3 | Keep as-is (small utility) |
| 1491 | Info | `get_low_range` | av2/encoder/trellis_quant.h:136 | 14 | 3 | Keep as-is (small utility) |
| 1492 | Info | `get_high_range_uv` | av2/encoder/trellis_quant.h:152 | 5 | 3 | Keep as-is (small utility) |
| 1493 | Info | `get_high_range` | av2/encoder/trellis_quant.h:159 | 6 | 3 | Keep as-is (small utility) |
| 1494 | Info | `get_golomb_cost_tcq` | av2/encoder/trellis_quant.h:167 | 5 | 3 | Keep as-is (small utility) |
| 1495 | Info | `get_br_lf_cost_tcq_uv` | av2/encoder/trellis_quant.h:174 | 5 | 3 | Keep as-is (small utility) |
| 1496 | Info | `get_br_lf_cost_tcq` | av2/encoder/trellis_quant.h:181 | 6 | 3 | Keep as-is (small utility) |
| 1497 | Info | `get_br_cost_tcq` | av2/encoder/trellis_quant.h:189 | 5 | 3 | Keep as-is (small utility) |
| 1498 | Info | `highbd_unsharp_rect` | av2/encoder/tune_vmaf.c:196 | 18 | 1 | Keep as-is (local) |
| 1499 | Info | `unsharp` | av2/encoder/tune_vmaf.c:215 | 9 | 1 | Keep as-is (local) |
| 1500 | Info | `gaussian_blur` | av2/encoder/tune_vmaf.c:229 | 31 | 1 | Keep as-is (local) |
| 1501 | Info | `cal_approx_vmaf` | av2/encoder/tune_vmaf.c:261 | 23 | 1 | Keep as-is (local) |
| 1502 | Info | `highbd_image_sad_c` | av2/encoder/tune_vmaf.c:809 | 17 | 1 | Keep as-is (local) |
| 1503 | Info | `get_neighbor_frames` | av2/encoder/tune_vmaf.c:875 | 15 | 1 | Keep as-is (local) |
| 1504 | Info | `cal_approx_score` | av2/encoder/tune_vmaf.c:949 | 10 | 1 | Keep as-is (local) |
| 1505 | Info | `get_block_residue_hash` | av2/encoder/tx_search.c:118 | 9 | 1 | Keep as-is (local) |
| 1506 | Info | `find_mb_rd_info` | av2/encoder/tx_search.c:128 | 17 | 1 | Keep as-is (local) |
| 1507 | Info | `fetch_tx_rd_info` | av2/encoder/tx_search.c:146 | 13 | 1 | Keep as-is (local) |
| 1508 | Info | `pixel_diff_dist` | av2/encoder/tx_search.c:162 | 28 | 1 | Keep as-is (local) |
| 1509 | Info | `pixel_diff_stats` | av2/encoder/tx_search.c:193 | 30 | 1 | Keep as-is (local) |
| 1510 | Info | `set_skip_txfm` | av2/encoder/tx_search.c:302 | 51 | 1 | Keep as-is (local) |
| 1511 | Info | `save_tx_rd_info` | av2/encoder/tx_search.c:354 | 25 | 1 | Keep as-is (local) |
| 1512 | Info | `get_energy_distribution_fine` | av2/encoder/tx_search.c:386 | 103 | 1 | Keep as-is (local) |
| 1513 | Info | `get_2x2_normalized_sses_and_sads` | av2/encoder/tx_search.c:513 | 51 | 1 | Keep as-is (local) |
| 1514 | Info | `PrintTransformUnitStats` | av2/encoder/tx_search.c:576 | 95 | 1 | Keep as-is (local) |
| 1515 | Info | `PrintPredictionUnitStats` | av2/encoder/tx_search.c:746 | 123 | 1 | Keep as-is (local) |
| 1516 | Info | `inverse_transform_block_facade` | av2/encoder/tx_search.c:872 | 18 | 1 | Keep as-is (local) |
| 1517 | Info | `recon_intra` | av2/encoder/tx_search.c:891 | 101 | 1 | Keep as-is (local) |
| 1518 | Info | `dist_block_px_domain` | av2/encoder/tx_search.c:1032 | 37 | 1 | Keep as-is (local) |
| 1519 | Info | `joint_uv_dist_block_px_domain` | av2/encoder/tx_search.c:1072 | 79 | 1 | Keep as-is (local) |
| 1520 | Info | `sort_rd` | av2/encoder/tx_search.c:1157 | 24 | 1 | Keep as-is (local) |
| 1521 | Info | `dist_block_tx_domain` | av2/encoder/tx_search.c:1182 | 21 | 1 | Keep as-is (local) |
| 1522 | Info | `sort_probability` | av2/encoder/tx_search.c:1474 | 24 | 1 | Keep as-is (local) |
| 1523 | Info | `get_adaptive_thresholds` | av2/encoder/tx_search.c:1499 | 16 | 1 | Keep as-is (local) |
| 1524 | Info | `get_energy_distribution_finer` | av2/encoder/tx_search.c:1516 | 63 | 1 | Keep as-is (local) |
| 1525 | Info | `get_tx_mask` | av2/encoder/tx_search.c:1706 | 219 | 1 | Keep as-is (local) |
| 1526 | Info | `update_txb_coeff_cost` | av2/encoder/tx_search.c:1927 | 21 | 1 | Keep as-is (local) |
| 1527 | Info | `cost_coeffs` | av2/encoder/tx_search.c:1950 | 20 | 1 | Keep as-is (local) |
| 1528 | Info | `predict_dc_only_block` | av2/encoder/tx_search.c:2005 | 77 | 1 | Keep as-is (local) |
| 1529 | Info | `tx_type_rd` | av2/encoder/tx_search.c:2886 | 24 | 1 | Keep as-is (local) |
| 1530 | Info | `try_tx_block_no_split` | av2/encoder/tx_search.c:2911 | 58 | 1 | Keep as-is (local) |
| 1531 | Info | `push_inter_block_none_tx_part_rd` | av2/encoder/tx_search.c:2971 | 21 | 1 | Keep as-is (local) |
| 1532 | Info | `prune_tx_part_eval_using_none_rd` | av2/encoder/tx_search.c:2994 | 19 | 1 | Keep as-is (local) |
| 1533 | Info | `choose_largest_tx_size` | av2/encoder/tx_search.c:3237 | 59 | 1 | Keep as-is (local) |
| 1534 | Info | `choose_lossless_tx_size` | av2/encoder/tx_search.c:3297 | 71 | 1 | Keep as-is (local) |
| 1535 | Info | `block_rd_txfm` | av2/encoder/tx_search.c:3500 | 81 | 1 | Keep as-is (local) |
| 1536 | Info | `block_rd_txfm_joint_uv` | av2/encoder/tx_search.c:3657 | 92 | 1 | Keep as-is (local) |
| 1537 | Info | `model_based_tx_search_prune` | av2/encoder/tx_search.c:3880 | 21 | 1 | Keep as-is (local) |
| 1538 | Info | `skip_cctx_eval_based_on_eob` | av2/encoder/tx_search.h:147 | 8 | 5 | Keep as-is (small utility) |

---

## Critical and High Severity Functions (Detail)

These functions have the highest impact on binary size and should be prioritized for optimization.

### ensure_mv_buffer

- **Location:** `av2/common/av2_common_int.h:3267`
- **Lines:** 106
- **TU Count:** 50
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void ensure_mv_buffer(RefCntBuffer *buf, AV2_COMMON *cm)`

### set_mi_row_col

- **Location:** `av2/common/av2_common_int.h:3531`
- **Lines:** 122
- **TU Count:** 50
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void set_mi_row_col(const AV2_COMMON *const cm, MACROBLOCKD *xd, const TileInfo *const tile, int mi_row, int bh, int mi_col, int bw, int...`

### init_allowed_partitions_for_signaling

- **Location:** `av2/common/av2_common_int.h:4491`
- **Lines:** 143
- **TU Count:** 50
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void init_allowed_partitions_for_signaling( bool *partition_allowed, const AV2_COMMON *const cm, TREE_TYPE tree_type, REGION_TYPE pa...`

### cluster_active_regions

- **Location:** `av2/common/bru.h:400`
- **Lines:** 151
- **TU Count:** 15
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void cluster_active_regions( unsigned char *map, AV2PixelRect *regions, uint32_t *act_sb_in_region, ARD_Queue **ard_queue, int width, in...`

### bru_active_map_validation

- **Location:** `av2/common/bru.h:553`
- **Lines:** 134
- **TU Count:** 15
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE bool bru_active_map_validation(AV2_COMMON *cm)`

### av2_is_dv_in_local_range

- **Location:** `av2/common/mvref_common.h:634`
- **Lines:** 227
- **TU Count:** 14
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int av2_is_dv_in_local_range(const AV2_COMMON *cm, const MV dv, const MACROBLOCKD *xd, int mi_row, int mi_col, int bh, int bw, int mib_s...`

### av2_is_dv_valid

- **Location:** `av2/common/mvref_common.h:862`
- **Lines:** 133
- **TU Count:** 14
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int av2_is_dv_valid(const MV dv, const AV2_COMMON *cm, const MACROBLOCKD *xd, int mi_row, int mi_col, BLOCK_SIZE bsize, int mib_size_log...`

### dealloc_compressor_data

- **Location:** `av2/encoder/encoder_alloc.h:145`
- **Lines:** 159
- **TU Count:** 5
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void dealloc_compressor_data(AV2_COMP *cpi)`

### highbd_set_var_fns

- **Location:** `av2/encoder/encoder_utils.h:531`
- **Lines:** 291
- **TU Count:** 4
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void highbd_set_var_fns(AV2_COMP *const cpi)`

### recode_loop_update_q

- **Location:** `av2/encoder/rc_utils.h:227`
- **Lines:** 165
- **TU Count:** 3
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void recode_loop_update_q( AV2_COMP *const cpi, int *const loop, int *const q, int *const q_low, int *const q_high, const int top_in...`

### prune_ref_by_selective_ref_frame

- **Location:** `av2/encoder/rdopt.h:207`
- **Lines:** 104
- **TU Count:** 16
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int prune_ref_by_selective_ref_frame( const AV2_COMP *const cpi, const MACROBLOCK *const x, const MV_REFERENCE_FRAME *const ref_frame)`

### set_mode_eval_params

- **Location:** `av2/encoder/rdopt_utils.h:283`
- **Lines:** 109
- **TU Count:** 6
- **Severity:** Critical
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void set_mode_eval_params(const struct AV2_COMP *cpi, MACROBLOCK *x, MODE_EVAL_TYPE mode_eval_type)`

### partition_plane_context_helper

- **Location:** `av2/common/av2_common_int.h:3778`
- **Lines:** 70
- **TU Count:** 50
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int partition_plane_context_helper(int raw_context, BLOCK_SIZE bsize, PART_CTX_MODE ctx_mode)`

### is_partition_implied_at_boundary

- **Location:** `av2/common/av2_common_int.h:4292`
- **Lines:** 67
- **TU Count:** 50
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE bool is_partition_implied_at_boundary( const CommonModeInfoParams *const mi_params, int mi_row, int mi_col, BLOCK_SIZE bsize, PARTIT...`

### get_tx_size

- **Location:** `av2/common/av2_common_int.h:4782`
- **Lines:** 51
- **TU Count:** 50
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE TX_SIZE get_tx_size(int width, int height)`

### get_partition

- **Location:** `av2/common/av2_common_int.h:5064`
- **Lines:** 71
- **TU Count:** 50
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE PARTITION_TYPE get_partition(const AV2_COMMON *const cm, const int plane_type, int mi_row, int mi_col, BLOCK_SIZE bsize)`

### opfl_allowed_cur_refs_bsize

- **Location:** `av2/common/av2_common_int.h:5349`
- **Lines:** 52
- **TU Count:** 50
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int opfl_allowed_cur_refs_bsize(const AV2_COMMON *cm, const MACROBLOCKD *xd, const MB_MODE_INFO *mbmi)`

### motion_mode_allowed

- **Location:** `av2/common/av2_common_int.h:5585`
- **Lines:** 83
- **TU Count:** 50
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int motion_mode_allowed(const AV2_COMMON *cm, const MACROBLOCKD *xd, const CANDIDATE_MV *ref_mv_stack, const MB_MODE_INFO *mbmi)`

### is_sub_partition_chroma_ref

- **Location:** `av2/common/blockd.h:1256`
- **Lines:** 53
- **TU Count:** 30
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int is_sub_partition_chroma_ref(PARTITION_TYPE partition, int index, BLOCK_SIZE bsize, BLOCK_SIZE parent_bsize, int ss_x, int ss_y, int ...`

### set_chroma_ref_offset_size

- **Location:** `av2/common/blockd.h:1310`
- **Lines:** 89
- **TU Count:** 30
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void set_chroma_ref_offset_size( int mi_row, int mi_col, PARTITION_TYPE partition, BLOCK_SIZE bsize, BLOCK_SIZE parent_bsize, int ss_x, ...`

### av2_get_tx_type

- **Location:** `av2/common/blockd.h:3063`
- **Lines:** 98
- **TU Count:** 30
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE TX_TYPE av2_get_tx_type(const MACROBLOCKD *xd, PLANE_TYPE plane_type, int blk_row, int blk_col, TX_SIZE tx_size, int reduced_tx_set)`

### compute_directions

- **Location:** `av2/common/cdef_block_simd.h:62`
- **Lines:** 68
- **TU Count:** 5
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE v128 compute_directions(v128 lines[8], int32_t tmp_cost1[4])`

### is_mhccp_allowed

- **Location:** `av2/common/cfl.h:96`
- **Lines:** 53
- **TU Count:** 14
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE MHCCP_ALLOWED_TYPE is_mhccp_allowed(const AV2_COMMON *const cm, const MACROBLOCKD *xd)`

### sdp_chroma_part_from_luma

- **Location:** `av2/common/common_data.h:331`
- **Lines:** 60
- **TU Count:** 5
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE PARTITION_TYPE sdp_chroma_part_from_luma( BLOCK_SIZE bsize, PARTITION_TYPE luma_part, int ssx, int ssy)`

### get_warp_motion_vector

- **Location:** `av2/common/mvref_common.h:216`
- **Lines:** 51
- **TU Count:** 14
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE int_mv get_warp_motion_vector(const MACROBLOCKD *xd, const WarpedMotionParams *model, MvSubpelPrecision precision, BLOCK_SIZE bsize, int...`

### get_converter

- **Location:** `av2/common/pc_wiener_filters.h:1412`
- **Lines:** 75
- **TU Count:** 0
- **Severity:** High
- **Proposed Change:** Consider moving to .c file
- **Signature:** `static AVM_INLINE const uint8_t *get_converter(int filter_set_index, int num_classes, int target_classes)`

### init_ref_map_pair

- **Location:** `av2/common/pred_common.h:25`
- **Lines:** 59
- **TU Count:** 20
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void init_ref_map_pair(AV2_COMMON *cm, RefFrameMapPair *ref_frame_map_pairs, int is_key, int is_ras)`

### get_txb_ctx

- **Location:** `av2/common/txb_common.h:751`
- **Lines:** 97
- **TU Count:** 9
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void get_txb_ctx(const BLOCK_SIZE plane_bsize, const TX_SIZE tx_size, const int plane, const ENTROPY_CONTEXT *const a, const ENTROPY_CON...`

### av2_scale_warp_model

- **Location:** `av2/common/warped_motion.h:270`
- **Lines:** 72
- **TU Count:** 11
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void av2_scale_warp_model(const WarpedMotionParams *in_params, int in_distance, WarpedMotionParams *out_params, int out_distance)`

### check_mv_precision

- **Location:** `av2/encoder/encodemv.h:110`
- **Lines:** 57
- **TU Count:** 15
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static inline int check_mv_precision(const AV2_COMMON *cm, const MB_MODE_INFO *const mbmi, const MACROBLOCK *x)`

### av2_set_seq_tile_info

- **Location:** `av2/encoder/encoder_utils.h:969`
- **Lines:** 54
- **TU Count:** 4
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void av2_set_seq_tile_info(SequenceHeader *const seq_params, const AV2EncoderConfig *oxcf)`

### av2_set_tile_info

- **Location:** `av2/encoder/encoder_utils.h:1024`
- **Lines:** 68
- **TU Count:** 4
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void av2_set_tile_info(AV2_COMMON *const cm, const TileConfig *const tile_cfg)`

### intra_mode_info_cost_y

- **Location:** `av2/encoder/intra_mode_search_utils.h:205`
- **Lines:** 63
- **TU Count:** 2
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE int intra_mode_info_cost_y(const AV2_COMP *cpi, const MACROBLOCK *x, const MB_MODE_INFO *mbmi, BLOCK_SIZE bsize, int mode_cost)`

### model_rd_for_sb

- **Location:** `av2/encoder/model_rd.h:151`
- **Lines:** 50
- **TU Count:** 4
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void model_rd_for_sb( const AV2_COMP *const cpi, BLOCK_SIZE bsize, MACROBLOCK *x, MACROBLOCKD *xd, int plane_from, int plane_to, int...`

### model_rd_for_sb_with_curvfit

- **Location:** `av2/encoder/model_rd.h:202`
- **Lines:** 60
- **TU Count:** 4
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void model_rd_for_sb_with_curvfit( const AV2_COMP *const cpi, BLOCK_SIZE bsize, MACROBLOCK *x, MACROBLOCKD *xd, int plane_from, int ...`

### config_target_level

- **Location:** `av2/encoder/rc_utils.h:37`
- **Lines:** 53
- **TU Count:** 3
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static AVM_INLINE void config_target_level(AV2_COMP *const cpi, AV2_LEVEL target_level, int tier)`

### store_winner_mode_stats

- **Location:** `av2/encoder/rdopt_utils.h:421`
- **Lines:** 81
- **TU Count:** 6
- **Severity:** High
- **Proposed Change:** Move to .c file
- **Signature:** `static INLINE void store_winner_mode_stats( const AV2_COMMON *const cm, MACROBLOCK *x, const MB_MODE_INFO *mbmi, RD_STATS *rd_cost, RD_STATS *rd_cost_...`
