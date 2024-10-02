"""
Copyright (c) 2024, Alliance for Open Media. All rights reserved

This source code is subject to the terms of the BSD 3-Clause Clear License
and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
License was not distributed with this source code in the LICENSE file, you
can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
Alliance for Open Media Patent License 1.0 was not distributed with this
source code in the PATENTS file, you can obtain it at
aomedia.org/license/patent-license/.
"""
import pandas as pd

import parakit.entropy.model as model
from parakit.entropy.codec_default_cdf import CDF_INIT_TOP, MAX_CTX_DIM


class DataCollector:
    def __init__(self, csv_filename):
        self.csv_filename = csv_filename
        self._checkfile()

        self._context_model = None
        self._num_rows_header = None
        self._parse_header_information()

    def get_context_model(self):
        return self._context_model

    def collect_dataframe(self, max_rows=None):
        csv_filename = self.csv_filename
        file_path = "./" + csv_filename
        num_lineskips = self._num_rows_header + 1
        df = pd.read_csv(file_path, header=num_lineskips, nrows=max_rows)
        return df

    def _checkfile(self):
        if not self.csv_filename.endswith(".csv"):
            raise ValueError("File should have .csv extension")

    def _parse_header_information(self):
        csv_filename = self.csv_filename
        file_path = "./" + csv_filename
        with open(file_path) as f:
            header_line = f.readline()
            # create tokens
            tokens = header_line.split(",")
            num_tokens = len(tokens)
            # parse
            header_str, ctx_group_name = tokens[0].split(":")
            num_symb = int(tokens[1])
            num_dims = int(tokens[2])
            size_list = []
            if num_dims > 0:
                size_list = [int(tokens[3 + i]) for i in range(num_dims)]

            # check if first line if header includes 'Header'
            if header_str != "Header":
                raise Warning(
                    "First line of header does not match in file: " + csv_filename
                )
            # check size of tokens
            expected_num_tokens = num_dims + 3
            if num_tokens != expected_num_tokens:
                raise Warning(
                    f"Expected and actual number of tokens in top header ({num_tokens} and {expected_num_tokens}) does not match for file "
                    + csv_filename
                )

            if ctx_group_name not in csv_filename:
                raise Warning(
                    f"Context group {ctx_group_name} does not appear in {csv_filename}"
                )

            num_context_groups = 1
            for size in size_list:
                num_context_groups *= size

            default_prob_inits = {}
            for i in range(num_context_groups):
                init_line = f.readline()
                init_tokens = init_line.split(",")
                expected_num_tokens = (
                    1 + MAX_CTX_DIM + num_symb + 2
                )  # ctx_idx, {4 dims}, {num_symb}, counter, rate

                if len(init_tokens) != expected_num_tokens:
                    raise Warning(
                        f"Expected and actual number of tokens ({num_tokens} and {expected_num_tokens}) for context group at {i} does not match for file "
                        + csv_filename
                    )

                # check header line-by-line
                ctx_idx = int(init_tokens[0])
                if ctx_idx != i:
                    raise Warning(
                        f"Expected context group index {i} does not match with {ctx_idx} in file "
                        + csv_filename
                    )
                index_list = []
                if num_dims > 0:
                    index_list = [
                        int(token) for token in init_tokens[1 : (MAX_CTX_DIM + 1)]
                    ]
                    index_list = index_list[(MAX_CTX_DIM - num_dims) :]
                # create dictionary string
                dict_str = ctx_group_name
                for index in index_list:
                    dict_str = dict_str + "[" + str(index) + "]"

                index_init_counter = MAX_CTX_DIM + 1 + num_symb
                init_cdfs = [
                    int(token)
                    for token in init_tokens[MAX_CTX_DIM + 1 : index_init_counter]
                ]
                init_counter = int(init_tokens[index_init_counter])
                index_init_rateidx = index_init_counter + 1
                init_rateidx = int(init_tokens[index_init_rateidx])
                # checks & assertions
                if init_cdfs[-1] != CDF_INIT_TOP:
                    raise Warning(
                        f"At context group index {i} CDF entry does not match with maximum CDF value {CDF_INIT_TOP} in file "
                        + csv_filename
                    )
                if init_counter != 0:
                    raise Warning(
                        f"At context group index {i} initial counter does not {0} in file "
                        + csv_filename
                    )

                default_prob_inits[dict_str] = {
                    "initializer": init_cdfs,
                    "index": index_list,
                    "init_rateidx": init_rateidx,
                    "init_counter": init_counter,
                }

        self._context_model = model.EntropyContext(
            ctx_group_name, num_symb, num_dims, size_list, default_prob_inits
        )
        self._num_rows_header = len(default_prob_inits)
