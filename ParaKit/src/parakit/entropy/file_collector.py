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


class FileCollector:
    def __init__(
        self,
        data_path,
        file_extension,
        ctx_group="",
        coding_config="",
        desired_qp=(),
        subtext="",
        starttext="",
    ):
        # non-public
        self._data_path = data_path
        self._file_extension = "." + file_extension  # e.g., 'csv' and 'json'
        self._ctx_group = ctx_group
        self._coding_config = coding_config
        self._desired_qp = desired_qp
        self._subtext = subtext
        self._starttext = starttext
        # public
        self.all_files = self._get_all_files()
        self.files = self._filter_files()

    # public
    def get_files(self):
        return self.files

    # non-public
    def _get_all_files(self):
        files_all = os.listdir(self._data_path)
        files = [f for f in files_all if f.endswith(self._file_extension)]
        return sorted(files)

    def _filter_files(self):
        filtered_files = self.all_files
        # ctx_group
        ctx_group = self._ctx_group
        if ctx_group != "":
            filtered_files = [
                f for f in filtered_files if "Stat_" + ctx_group + "_Bin_" in f
            ]
        # coding config (AI, RA, LD, etc..)
        cfg = self._coding_config
        if cfg != "":
            filtered_files = [f for f in filtered_files if "_" + cfg + "_" in f]
        # filter based on desired QP list
        desired_qp = self._desired_qp
        if len(desired_qp) > 0:
            all_filt_files = filtered_files.copy()
            filtered_files = []
            for qp in desired_qp:
                new_files = [f for f in all_filt_files if "_QP_" + str(qp) + "_" in f]
                for f in new_files:
                    filtered_files.append(f)
        # any subtext between underscores in '_{subtext}_' format
        subtext = self._subtext
        if subtext != "":
            filtered_files = [f for f in filtered_files if subtext in f]

        starttext = self._starttext
        if starttext != "":
            filtered_files = [f for f in filtered_files if f.startswith(starttext)]

        return filtered_files
