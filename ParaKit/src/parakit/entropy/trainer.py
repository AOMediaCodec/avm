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
import json
import os

import numpy as np
import pandas as pd

import parakit.entropy.model as model
from parakit.entropy.codec_cdf_functions import (
    cdf2pmf_av2,
    cdfinv2pmf_av2,
    cost_symbol_av2,
    pmf2cdfinv_av2,
    update_cdfinv_av2,
)
from parakit.entropy.data_collector import DataCollector

COST_REGULARIZATION = 4  # modifies maximum cost
MIN_SAMPLE_REQUIRED = 10  # minimum number of data points needed to run training
MAX_NUMBER_ROWS = 5000000


class Trainer:
    def __init__(
        self,
        filename_data,
        rate_search_list,
        cost_regularization=COST_REGULARIZATION,
        min_sample=MIN_SAMPLE_REQUIRED,
        max_rows=MAX_NUMBER_ROWS,
    ):
        # training parameters
        self._filename_data = filename_data  # data file
        self._cost_regularization = cost_regularization
        self._min_sample = min_sample
        self._max_rows = max_rows
        self._rate_search_list = rate_search_list

        # internal data and models
        self.dataframe = None
        self.initial_model = None
        self.resulting_model = None
        self._max_cost_regularization = None
        self._output_filename = None  # resulting json file

        self.initialize()

    def initialize(self):
        dc = DataCollector(self._filename_data)
        self._initial_model = dc.get_context_model()
        self.dataframe = dc.collect_dataframe(max_rows=self._max_rows)
        self._output_filename = self._get_output_filename()
        self._max_cost_regularization = cost_symbol_av2(self._cost_regularization)

    def prepare_dataframe(self, df, ctx_idx_list):
        dims = self._initial_model.num_dims
        df_filtered = df
        if dims > 0:
            mask = df["Dim0"] == ctx_idx_list[0]
            for i in range(1, dims):
                mask &= df[f"Dim{i}"] == ctx_idx_list[i]
            df_filtered = df.loc[mask]
        # add "CalcCost" column calculating cost
        new_cols = ["CalcCost"]
        df_filtered = df_filtered.reindex(
            columns=[*df_filtered.columns.tolist(), *new_cols], fill_value=0
        )
        df_filtered.reset_index(inplace=True)

        # return empty df if very few (< _min_sample) data points available
        if len(df_filtered) < self._min_sample:
            return pd.DataFrame()

        # convert NaN values to -1, if any
        df_filtered = df_filtered.fillna(-1)
        df_filtered = df_filtered[df_filtered.columns].astype(
            int
        )  # all columns are integer

        return df_filtered

    def map_rate_from_counter(self, rates_tuple, counter):
        if counter <= 15:
            return rates_tuple[0]
        elif counter <= 31:
            return rates_tuple[1]
        else:
            return rates_tuple[2]

    def update_pmf_per_rate(self, pmf_list, cost_list, val, counter):
        rate_offset_list = self._rate_search_list
        nsymb = self._initial_model.num_symb
        for r, rates_tuple in enumerate(rate_offset_list):
            roffset = self.map_rate_from_counter(rates_tuple, counter)
            # cost
            cost = cost_symbol_av2(pmf_list[r][val])
            if cost > self._max_cost_regularization:
                cost = self._max_cost_regularization
            cost_list[r] += cost
            # update
            cdf_inv = pmf2cdfinv_av2(pmf_list[r])
            cdf_inv_updated = update_cdfinv_av2(cdf_inv, val, counter, nsymb, roffset)
            pmf_list[r] = cdfinv2pmf_av2(cdf_inv_updated)
        return (pmf_list, cost_list)

    def estimate_cost_multiple(self, df_filtered, pmf_init, rateidx_init=0):
        nsymb = self._initial_model.num_symb
        rate_offset_list = self._rate_search_list
        num_rates = len(rate_offset_list)
        cost_list = np.zeros(num_rates, dtype=int)

        # initialization
        pmf_init_list = [pmf_init] * num_rates
        prev_pmf_buffer = [pmf_init_list]
        cdf_cols = []
        for i in range(nsymb):
            if f"cdf{i}" in df_filtered.keys():
                cdf_cols.append(f"cdf{i}")

        mask_index = (df_filtered["Counter"] == 0) | (df_filtered["isBeginFrame"] == 1)
        index_list = df_filtered[mask_index].index.to_list()
        index_list.append(len(df_filtered))
        index_pair_list = [
            (index_list[ind], index_list[ind + 1]) for ind in range(len(index_list) - 1)
        ]

        for index_pair in index_pair_list:
            start_idx, end_idx = index_pair
            df_sub = df_filtered.iloc[start_idx:end_idx].copy()

            num_data_samples = len(df_sub)
            success = False
            if len(cdf_cols) == nsymb:
                cdf_init = np.array(df_sub.iloc[0][cdf_cols], dtype=int)
                prev_pmf_buffer = [[cdf2pmf_av2(cdf_init)] * num_rates]
            for _, prev_pmf in enumerate(prev_pmf_buffer):
                rateidx_init = df_sub.iloc[0]["rate"]
                pmf_default = prev_pmf[rateidx_init]  # default one
                # initial values
                val_init = df_sub.iloc[0]["Value"]
                cost = df_sub.iloc[0]["Cost"]
                calculated_cost = cost_symbol_av2(pmf_default[val_init])
                if calculated_cost > self._max_cost_regularization:
                    calculated_cost = self._max_cost_regularization

                cost_list_persub = np.zeros(num_rates, dtype=int)

                if calculated_cost != cost:
                    continue

                pmf_list = [pmf_default] * num_rates

                # try best cases
                for i in range(num_data_samples):
                    val = df_sub.iloc[i]["Value"]
                    counter = df_sub.iloc[i]["Counter"]
                    cost = df_sub.iloc[i]["Cost"]
                    calculated_cost = cost_symbol_av2(pmf_list[rateidx_init][val])
                    if calculated_cost > self._max_cost_regularization:
                        calculated_cost = self._max_cost_regularization
                    df_sub.iloc[i]["CalcCost"] = calculated_cost

                    if cost != calculated_cost:
                        break

                    # update variables
                    pmf_list, cost_list_persub = self.update_pmf_per_rate(
                        pmf_list, cost_list_persub, val, counter
                    )

                    # success
                    if i == num_data_samples - 1:
                        success = True
                        # update cost
                        cost_list += cost_list_persub
                        # save latest pmf before updating
                        prev_pmf_buffer.insert(1, pmf_list)
                if success:
                    break

        return (cost_list, 0)

    def get_cost_per_value(self, df):
        num_symb = self._initial_model.num_symb
        cost_pervalue_list = [0] * num_symb
        if df.empty:
            return cost_pervalue_list
        for i in range(num_symb):
            cost = df[df["Value"] == i]["Cost"].sum()
            cost_pervalue_list[i] = int(cost)
        return cost_pervalue_list

    def get_value_count(self, df):
        num_symb = self._initial_model.num_symb
        value_count_list = [0] * num_symb
        if df.empty:
            return value_count_list
        count_series = df["Value"].value_counts()
        for i in count_series.index:
            count = count_series[i]
            value_count_list[i] = int(count)
        return value_count_list

    def run_rate_training_on_file(self):
        # parameters
        rate_parameters = self._rate_search_list
        size_list = self._initial_model.size_list
        num_dims = self._initial_model.num_dims
        num_symb = self._initial_model.num_symb
        ctx_group_name = self._initial_model.ctx_group_name
        prob_dict = self._initial_model.model_dict

        ctx_name_list = list(prob_dict.keys())
        result_dict = prob_dict.copy()
        for ctx_name in ctx_name_list:
            ctx_idx_interest = prob_dict[ctx_name]["index"]
            print(f"Context {ctx_name}:", end=" ")
            df_filtered = self.prepare_dataframe(self.dataframe, ctx_idx_interest)
            value_count_list = self.get_value_count(df_filtered)
            value_cost_list = self.get_cost_per_value(df_filtered)
            # check empty df
            if df_filtered.empty:
                print("Skip training - insufficient data.")
                result_dict[ctx_name] = {
                    "current_cost": np.zeros(len(rate_parameters), dtype=int),
                    "adapt_rate": rate_parameters,
                    "initial_cost": 0,
                    "codec_cost": 0,
                    "upper_cost": 0,
                    "num_samples": 0,
                    "initializer": prob_dict[ctx_name]["initializer"],
                    "init_rateidx": prob_dict[ctx_name]["init_rateidx"],
                    "value_count": value_count_list,
                    "value_cost": value_cost_list,
                }
                continue

            # actual cost
            codec_cost = df_filtered["Cost"].sum()

            # estimate cost
            default_cdf = np.array(prob_dict[ctx_name]["initializer"])
            default_pmf = cdf2pmf_av2(default_cdf)
            default_rateidx = int(prob_dict[ctx_name]["init_rateidx"])
            # default_rate = RATE_LIST[default_rateidx]

            curr_cost_list, upper_cost = self.estimate_cost_multiple(
                df_filtered, pmf_init=default_pmf, rateidx_init=default_rateidx
            )
            init_cost = curr_cost_list[default_rateidx]
            curr_cost_list = curr_cost_list - init_cost
            result_dict[ctx_name] = {
                "current_cost": curr_cost_list,
                "adapt_rate": rate_parameters,
                "initial_cost": init_cost,
                "codec_cost": codec_cost,
                "upper_cost": upper_cost,
                "num_samples": len(df_filtered),
                "initializer": prob_dict[ctx_name]["initializer"],
                "init_rateidx": prob_dict[ctx_name]["init_rateidx"],
                "value_count": value_count_list,
                "value_cost": value_cost_list,
            }
            print("Training...")

        self.resulting_model = model.EntropyContext(
            ctx_group_name, num_symb, num_dims, size_list, result_dict
        )
        self.write_results()

        return self.resulting_model

    def _get_output_filename(self):
        # get path and filename
        fullpath_data = self._filename_data
        filename = os.path.basename(fullpath_data)
        dirname = os.path.dirname(fullpath_data)
        if len(dirname) == 0:
            dirname = "."
        base_filename = filename.split(".")[0]
        fullpath_result_json = dirname + "/" + "Result_" + base_filename + ".json"
        return fullpath_result_json

    def get_searched_rate_parameter_list(self):
        return self._rate_search_list

    def write_results(self):
        # information
        ctx_group_name = self.resulting_model.ctx_group_name
        num_symb = self.resulting_model.num_symb
        num_dims = self.resulting_model.num_dims
        size_list = self.resulting_model.size_list
        result_dict = self.resulting_model.model_dict

        # collect keys
        key_list = ["information"]
        key_list.extend(list(result_dict.keys()))
        json_dict = dict.fromkeys(key_list)
        for key in key_list:
            if key == "information":
                json_dict[key] = {
                    "header": {
                        "ctx_group_name": ctx_group_name,
                        "num_symb": num_symb,
                        "num_dims": num_dims,
                        "size_list": size_list,
                    },
                    "training": {
                        "rate_search_list": self._rate_search_list,
                        "cost_regularization": self._cost_regularization,
                        "min_sample": self._min_sample,
                    },
                }
            else:
                cost_list = result_dict[key]["current_cost"].copy()
                min_idx = np.argmin(cost_list)
                json_dict[key] = {
                    "best_rate": self._rate_search_list[min_idx],
                    "best_rate_idx": int(min_idx),
                    "current_cost": cost_list.astype(int).tolist(),
                    "initial_cost": int(result_dict[key]["initial_cost"]),
                    "codec_cost": int(result_dict[key]["codec_cost"]),
                    "upper_cost": int(result_dict[key]["upper_cost"]),
                    "num_samples": int(result_dict[key]["num_samples"]),
                    "initializer": result_dict[key]["initializer"],
                    "init_rateidx": result_dict[key]["init_rateidx"],
                    "value_count": result_dict[key]["value_count"],
                    "value_cost": result_dict[key]["value_cost"],
                }
        # write to json
        with open(self._output_filename, "w") as jsonfile:
            json.dump(json_dict, jsonfile, indent=4)
