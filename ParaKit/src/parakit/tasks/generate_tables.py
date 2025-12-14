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

from termcolor import cprint

import parakit.config.user as user
import parakit.entropy.model as model
from parakit.entropy.codec_default_cdf import CDF_INIT_TOP, av2_default_cdf_parameters

DEFAULT_PROB_INITIALIZER = False
DEFAULT_RATE_PARAMETER = False
ZERO_RATE_PARAMETER = False


def run(
    path_ctxdata="./results/data",
    path_table="./results",
    user_config_file="parameters.yaml",
):
    results = {}
    test_output_tag, desired_ctx_list = user.read_config_data(user_config_file)
    combined_file = f"Combined_Result_{test_output_tag}"
    with open(f"{path_ctxdata}/{combined_file}.json") as json_file:
        results = json.load(json_file)
    table_filename = f"{path_table}/Context-Table_{combined_file}.h"
    cprint(f"Generating context tables in file: {table_filename}", attrs=["bold"])
    num_symbols = 0
    num_symbol_groups = 0
    table_string = "\n"
    for desired_ctx in desired_ctx_list:
        result_ctx_group = results[desired_ctx]
        print(f"Generating table for context: {desired_ctx}")
        result_info = result_ctx_group["information"]
        ctx_group_name = result_info["header"]["ctx_group_name"]
        num_symb = result_info["header"]["num_symb"]
        num_dims = result_info["header"]["num_dims"]
        size_list = result_info["header"]["size_list"]

        sym_grp = 1
        for d in range(num_dims):
            sym_grp = sym_grp * size_list[d]
        num_symbol_groups += sym_grp
        num_symbols = num_symbols + (sym_grp * num_symb)

        key_list = list(result_ctx_group.keys())
        key_list.remove("information")
        for key in key_list:
            if ZERO_RATE_PARAMETER:
                result_ctx_group[key]["init_rate"] = 0
            elif DEFAULT_RATE_PARAMETER:
                result_ctx_group[key]["init_rate"] = result_ctx_group[key][
                    "init_rateidx"
                ]
            else:
                result_ctx_group[key]["init_rate"] = result_ctx_group[key][
                    "best_rate_idx"
                ]

            if DEFAULT_PROB_INITIALIZER:
                cdf_list = av2_default_cdf_parameters(num_symb).tolist()
                cdf_list.append(CDF_INIT_TOP)
                result_ctx_group[key]["initializer"] = cdf_list

        resulting_model = model.EntropyContext(
            ctx_group_name,
            num_symb,
            num_dims,
            size_list,
            result_ctx_group,
            user_config_file,
        )
        table_string += resulting_model.get_complete_model_string(is_avm_style=True)
        table_string += "\n"

    with open(table_filename, "w") as table_file:
        table_file.write(table_string)
        cprint("Done generating context tables!\n", "green", attrs=["bold"])


if __name__ == "__main__":
    run()
