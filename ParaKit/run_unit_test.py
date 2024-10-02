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
import os

from termcolor import cprint

import parakit.tasks.collect_results as collect
import parakit.tasks.decoding as decode
import parakit.tasks.generate_tables as generate_table
import parakit.tasks.training as train

PATH_BITSTREAM = "./unit_test/bitstreams"
PATH_CTXDATA = "./unit_test/data"
PATH_TABLE = "./unit_test"
CFG_FILE = "./unit_test/parameters_unit_test.yaml"
TABLE_FILE = "Context-Table_Combined_Result_Unit-Test.h"


def main():
    cprint(
        "-------------------- UNIT TEST ----------------------", "black", attrs=["bold"]
    )
    # Step 1: run decoder to collect data
    decode.run(
        path_bitstream=PATH_BITSTREAM,
        path_ctx_data=PATH_CTXDATA,
        user_config_file=CFG_FILE,
    )

    # Step 2: run training and create result report
    train.run(path_ctxdata=PATH_CTXDATA, user_config_file=CFG_FILE)

    # Step 3: collect results
    collect.run(path_ctxdata=PATH_CTXDATA, user_config_file=CFG_FILE)

    # Step 4: generate context tables
    generate_table.run(
        path_ctxdata=PATH_CTXDATA, path_table=PATH_TABLE, user_config_file=CFG_FILE
    )

    # Check if TABLE_FILE exists after unit test
    table_file = f"{PATH_TABLE}/{TABLE_FILE}"
    check_file = os.path.exists(table_file)
    if check_file:
        cprint("Unit test successful!", "green", attrs=["bold"])
    else:
        cprint(
            f"Unit test failed: {TABLE_FILE} cannot be found.", "red", attrs=["bold"]
        )
    cprint(
        "-----------------------------------------------------", "black", attrs=["bold"]
    )


if __name__ == "__main__":
    main()
