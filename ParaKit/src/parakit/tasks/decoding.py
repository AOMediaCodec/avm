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
import sys

from termcolor import cprint

import parakit.config.user as user
from parakit.entropy.file_collector import FileCollector


def decode_task(decode_info):
    os.system(decode_info[0])
    print(f"Decoded: {decode_info[2]}", flush=True)


def run(
    path_bitstream="./bitstreams",
    path_ctx_data="./results/data",
    user_config_file="parameters.yaml",
):
    test_output_tag, _ = user.read_config_data(user_config_file)
    bitstream_extension = user.read_config_decode(user_config_file)
    fc = FileCollector(path_bitstream, bitstream_extension)
    bitstreams = fc.get_files()
    num_bitstreams = len(bitstreams)
    if num_bitstreams == 0:
        cprint(
            f"No bistream files with extension .{bitstream_extension} under {path_bitstream}",
            "red",
            attrs=["bold"],
            file=sys.stderr,
        )
        print(
            f"Usage: (i) add files under {path_bitstream} path and (ii) choose the correct extension in parameters.yaml (BITSTREAM_EXTENSION field)."
        )
        sys.exit()
    cprint(
        f"Decoding {num_bitstreams} bitstreams to collect data under {path_ctx_data}:",
        attrs=["bold"],
    )
    # prepare decoding task information
    decode_info = []
    for idx, bitstream in enumerate(bitstreams):
        suffix = os.path.splitext(bitstream)[0] + "_" + test_output_tag
        decode_info.append(
            (
                f"binaries/aomdec {path_bitstream}/{bitstream} --path-ctxdata={path_ctx_data} --suffix-ctxdata={suffix} -o /dev/null",
                idx,
                bitstream,
            )
        )
    # run using all available cores
    num_cpu = os.cpu_count()
    with multiprocessing.Pool(num_cpu) as pool:
        pool.map(decode_task, decode_info)
    cprint("Decoding complete!\n", "green", attrs=["bold"])


if __name__ == "__main__":
    run()
