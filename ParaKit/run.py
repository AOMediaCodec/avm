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
from datetime import datetime

from termcolor import cprint

import parakit.tasks.collect_results as collect
import parakit.tasks.decoding as decode
import parakit.tasks.generate_tables as generate_table
import parakit.tasks.training as train


def print_elapsed_time(start, end):
    total_time = end - start
    hours = total_time.seconds // 3600
    mins = total_time.seconds // 60 % 60
    sec = total_time.seconds % 60
    print(f"Elapsed time: {total_time.days}d {hours}h:{mins}m:{sec}s")


def main():
    cprint(
        "------------------ RUN TRAINING ---------------------", "black", attrs=["bold"]
    )
    start_time = datetime.now()

    # Step 1: run decoder to collect data
    decode.run()

    # Step 2: run training and create result report
    train.run()

    # Step 3: collect results
    collect.run()

    # Step 4: generate context tables
    generate_table.run()

    end_time = datetime.now()
    print_elapsed_time(start_time, end_time)
    cprint(
        "-----------------------------------------------------", "black", attrs=["bold"]
    )


if __name__ == "__main__":
    main()
