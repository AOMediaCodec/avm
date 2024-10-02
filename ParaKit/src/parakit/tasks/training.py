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
import multiprocessing
import os

from termcolor import cprint

import parakit.config.user as user
from parakit.config.training import RATE_LIST
from parakit.entropy.file_collector import FileCollector
from parakit.entropy.trainer import Trainer


def train_task(train_info):
    file_fullpath = train_info[0]
    trainer = Trainer(file_fullpath, RATE_LIST)
    trainer.run_rate_training_on_file()
    print(f"Trained: {train_info[2]}", flush=True)


def run(path_ctxdata="./results/data", user_config_file="parameters.yaml"):
    test_output_tag, _ = user.read_config_data(user_config_file)
    fc = FileCollector(path_ctxdata, "csv", subtext=f"_{test_output_tag}.")
    data_files = fc.get_files()
    num_data_files = len(data_files)
    cprint(
        f"Training based on {num_data_files} csv files under {path_ctxdata}:",
        attrs=["bold"],
    )
    # prepare decoding task information
    train_info = []
    for idx, file_csv in enumerate(data_files):
        file_fullpath = f"{path_ctxdata}/{file_csv}"
        # print(f'[{idx}]: {file_csv}')
        train_info.append((file_fullpath, idx, file_csv))
    # run training in paralel using all available cores
    num_cpu = os.cpu_count()
    with multiprocessing.Pool(num_cpu) as pool:
        pool.map(train_task, train_info)
    cprint("Training complete!\n", "green", attrs=["bold"])


if __name__ == "__main__":
    run()
