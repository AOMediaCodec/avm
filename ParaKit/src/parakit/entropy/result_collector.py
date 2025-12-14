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

import numpy as np

from parakit.entropy.codec_cdf_functions import count2cdf_av2
from parakit.entropy.codec_default_cdf import CDF_INIT_TOP, av2_default_cdf_parameters


class ResultCollector:
    def __init__(self, json_filename):
        self.json_filename = json_filename
        self._checkfile()

    def _checkfile(self):
        if not self.json_filename.endswith(".json"):
            raise ValueError("File should have .json extension")

    def _checkdata(self, data, key, isIgnored, default_rateidx=0):
        data_cost = data[key]["current_cost"]
        cost_default = int(data_cost[default_rateidx])
        if cost_default != 0:
            print(
                f"Warning: Default cost is {cost_default} for {key} in file {self.json_filename}",
                end=" -- ",
            )
            if isIgnored:
                print("Ignored!")
                return False
            else:
                print("Not ignored!")
        return True

    def parse_json_file(self, isIgnored=True):
        filepath = self.json_filename
        data = {}
        with open(filepath) as json_file:
            data = json.load(json_file)
            keys = list(data.keys())
            keys.remove("information")
            for key in keys:
                init_rateidx = data[key]["init_rateidx"]
                if self._checkdata(data, key, isIgnored, default_rateidx=init_rateidx):
                    data[key]["current_cost"] = np.array(data[key]["current_cost"])
                    data[key]["value_count"] = np.array(data[key]["value_count"])
                    data[key]["value_cost"] = np.array(data[key]["value_cost"])
                else:
                    data[key]["current_cost"] = np.zeros(
                        len(data[key]["current_cost"]), dtype=int
                    )
                    data[key]["value_count"] = np.zeros(
                        len(data[key]["value_count"]), dtype=int
                    )
                    data[key]["value_cost"] = np.zeros(
                        len(data[key]["value_cost"]), dtype=int
                    )
        return data

    def calculate_percent_reduction(self, data_in_key):
        actual_cost = data_in_key["initial_cost"]
        if actual_cost == 0:
            percent_reduction = np.zeros(len(data_in_key["current_cost"]))
        else:
            percent_reduction = 100 * (data_in_key["current_cost"] / actual_cost)
        percent_reduction = np.nan_to_num(percent_reduction)
        return percent_reduction

    def combine_data(self, combined_data, data):
        keys = list(data.keys())
        keys.remove("information")  # skip information key
        if len(combined_data) == 0:
            combined_data = data.copy()
            # one can add new fields below, if needed
            for key in keys:
                percent_reduction = self.calculate_percent_reduction(data[key])
                combined_data[key][
                    "percent_cost_total"
                ] = percent_reduction  # add percent reduction field
                combined_data[key][
                    "percent_cost_min"
                ] = percent_reduction  # add percent reduction field
                combined_data[key][
                    "percent_cost_max"
                ] = percent_reduction  # add percent reduction field
        else:
            for key in keys:
                combined_data[key]["current_cost"] += data[key]["current_cost"]
                combined_data[key]["num_samples"] += data[key]["num_samples"]
                combined_data[key]["initial_cost"] += data[key]["initial_cost"]
                combined_data[key]["codec_cost"] += data[key]["codec_cost"]
                combined_data[key]["upper_cost"] += data[key]["upper_cost"]
                combined_data[key]["value_count"] += data[key]["value_count"]
                combined_data[key]["value_cost"] += data[key]["value_cost"]
                # update percent reduction fields
                percent_reduction = self.calculate_percent_reduction(data[key])
                combined_data[key]["percent_cost_total"] += percent_reduction
                combined_data[key]["percent_cost_min"] = np.minimum(
                    percent_reduction, combined_data[key]["percent_cost_min"]
                )
                combined_data[key]["percent_cost_max"] = np.maximum(
                    percent_reduction, combined_data[key]["percent_cost_max"]
                )
        return combined_data

    def update_probability_initialzer(self, data):
        keys = list(data.keys())
        keys.remove("information")  # skip information key
        for key in keys:
            value_count = data[key]["value_count"]
            total_count = value_count.sum()
            if total_count > 0:
                pmf = value_count / total_count
                cdf = np.cumsum(pmf)
                scaled_cdf = np.round(CDF_INIT_TOP * cdf).astype(int)
                if scaled_cdf[-1] > CDF_INIT_TOP:
                    scaled_cdf[-1] = CDF_INIT_TOP
                data[key]["initializer"] = scaled_cdf.tolist()
        return data

    def update_probability_initialzer_av2_style(self, data):
        keys = list(data.keys())
        keys.remove("information")  # skip information key
        for key in keys:
            value_count = data[key]["value_count"]
            scaled_cdf = count2cdf_av2(value_count)
            data[key]["initializer"] = scaled_cdf.tolist()
        return data

    def update_default_probability_initializer(self, data):
        keys = list(data.keys())
        keys.remove("information")  # skip information key
        for key in keys:
            num_symb = data[key]["header"]["num_symb"]
            cdf_list = av2_default_cdf_parameters(num_symb).tolist()
            cdf_list.append(CDF_INIT_TOP)
            data[key]["initializer"] = cdf_list
        return data
