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
from termcolor import cprint

import parakit.config.user as user
from parakit.config.training import (
    CHANGE_INITIALIZERS,
    MIN_NUM_DATA_SAMPLES_NEEDED,
    RATE_LIST,
)
from parakit.entropy.file_collector import FileCollector
from parakit.entropy.result_collector import ResultCollector


def run(path_ctxdata="./results/data", user_config_file="parameters.yaml"):
    path_result = path_ctxdata
    test_output_tag, desired_ctx_list = user.read_config_data(user_config_file)
    if CHANGE_INITIALIZERS:
        test_output_tag += "_newinit"
    final_dict = dict.fromkeys(desired_ctx_list)
    cprint(
        f"Combining results for {len(desired_ctx_list)} sets of probability models:",
        attrs=["bold"],
    )
    for desired_ctx in desired_ctx_list:
        fc = FileCollector(
            path_result, "json", desired_ctx, subtext=f"_{test_output_tag}."
        )
        json_files = fc.get_files()
        num_json_files = len(json_files)
        print(f"Working on {desired_ctx} to combine {num_json_files} json files")
        combined_results = {}
        for file_idx in range(num_json_files):
            filename = json_files[file_idx]
            print(f"Combining: {filename}")
            filepath = path_result + "/" + filename
            rc = ResultCollector(filepath)
            result_dict = rc.parse_json_file()
            combined_results = rc.combine_data(combined_results, result_dict)

        if CHANGE_INITIALIZERS:  # change initializer
            combined_results = rc.update_probability_initialzer_av1_style(
                combined_results
            )

        if len(combined_results) == 0:
            final_dict[desired_ctx] = {}
        else:
            key_list = list(combined_results.keys())
            temp_dict = dict.fromkeys(key_list)
            for key in key_list:
                if key == "information":
                    temp_dict[key] = combined_results["information"]
                    continue
                cost_list = combined_results[key]["current_cost"]
                actual_cost = combined_results[key]["initial_cost"]
                if actual_cost == 0:
                    overall_percent_reduction = np.zeros(len(cost_list))
                else:
                    overall_percent_reduction = 100 * (cost_list / actual_cost)
                overall_percent_reduction = np.nan_to_num(overall_percent_reduction)
                num_samples = combined_results[key]["num_samples"]
                combined_percent_reduction = combined_results[key]["percent_cost_total"]
                MAX_RATE_SEARCH = len(RATE_LIST)
                min_idx = np.argmin(cost_list[0:MAX_RATE_SEARCH])
                min_idx_perc = np.argmin(combined_percent_reduction[0:MAX_RATE_SEARCH])
                if num_samples < MIN_NUM_DATA_SAMPLES_NEEDED:
                    min_idx = 0
                    min_idx_perc = 0
                init_rateidx = int(combined_results[key]["init_rateidx"])
                temp_dict[key] = {
                    "best_rate": RATE_LIST[min_idx],
                    "best_rate_idx": int(min_idx),
                    "current_cost": cost_list.astype(int).tolist(),
                    "initial_cost": int(actual_cost),
                    "best_rate_perc": RATE_LIST[min_idx_perc],
                    "best_rate_idx_perc": int(min_idx_perc),
                    "percent_cost": combined_percent_reduction.astype(float).tolist(),
                    "num_samples": int(num_samples),
                    "overall_percent_reduction": overall_percent_reduction.astype(
                        float
                    ).tolist(),
                    "initializer": combined_results[key]["initializer"],
                    "init_rate": RATE_LIST[init_rateidx],
                    "init_rateidx": int(init_rateidx),
                }
            final_dict[desired_ctx] = temp_dict

    with open(f"{path_result}/Combined_Result_{test_output_tag}.json", "w") as outfile:
        json.dump(final_dict, outfile, indent=4)
        cprint("Done combining results!\n", "green", attrs=["bold"])


if __name__ == "__main__":
    run()
