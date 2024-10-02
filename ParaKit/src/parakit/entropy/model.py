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
import parakit.config.user as user
from parakit.entropy.codec_default_cdf import get_aom_cdf_string

ADD_COMMENTS = False


class EntropyContext:
    def __init__(
        self,
        ctx_group_name,
        num_symb,
        num_dims,
        size_list,
        model_dict,
        user_cfg_file=(),
    ):
        self.ctx_group_name = ctx_group_name
        self.num_symb = num_symb
        self.num_dims = num_dims
        self.size_list = size_list
        self.model_dict = model_dict
        self.user_cfg = user_cfg_file

    def get_model_dictionary(self, index_list):
        key = self._get_key(index_list)
        return self.model_dict[key]

    def get_model_string(self, index_list):
        return self.get_cdf_string(index_list) + ", " + self.get_rate_string(index_list)

    def get_rate_string(self, index_list):
        model = self.get_model_dictionary(index_list)
        rate = model["init_rate"]
        rate_str = str(rate).rjust(3)
        return rate_str

    def get_cdf_string(self, index_list):
        model = self.get_model_dictionary(index_list)
        cdf_list = model["initializer"]
        num_symb = len(cdf_list)
        cdf = cdf_list[: num_symb - 1]
        cdf_str = get_aom_cdf_string(cdf)
        return cdf_str

    def get_frequency_string(self, index_list):
        model = self.get_model_dictionary(index_list)
        num_samples = model["num_samples"]
        sample_str = str(num_samples).rjust(8)
        return sample_str

    def get_complete_model_string(self, is_avm_style=True):
        model_str = []
        if is_avm_style:
            model_str = self._get_avm_variable_name() + " = "
        else:
            model_str = self._get_arrayname() + " = "
        num_dim = self.num_dims
        if num_dim == 0:
            model_str = self._model_string_dim0(model_str)
        elif num_dim == 1:
            model_str = self._model_string_dim1(model_str)
        elif num_dim == 2:
            model_str = self._model_string_dim2(model_str)
        elif num_dim == 3:
            model_str = self._model_string_dim3(model_str)
        elif num_dim == 4:
            model_str = self._model_string_dim4(model_str)
        else:
            model_str = "Number of dimensions needs to be between 0 and 4."

        # last characters
        model_str += "\n"

        return model_str

    def _get_num_ctx_groups(self):
        num_context_groups = 1
        for size in self.size_list:
            num_context_groups *= size
        return num_context_groups

    def _get_key(self, index_list):
        key = self.ctx_group_name
        for index in index_list:
            key = key + "[" + str(index) + "]"
        return key

    def _get_arrayname(self):
        array_name = self.ctx_group_name
        for size in self.size_list:
            array_name = array_name + "[" + str(size) + "]"
        array_name += "[CDF_SIZE(" + str(self.num_symb) + ")]"
        return array_name

    def _get_avm_variable_name(self):
        array_name = "static const aom_cdf_prob " + user.read_config_context(
            self.user_cfg, self.ctx_group_name
        )
        for size in self.size_list:
            array_name = array_name + "[" + str(size) + "]"
        array_name += "[CDF_SIZE(" + str(self.num_symb) + ")]"
        return array_name

    def _model_string_dim0(self, model_str, is_commented=ADD_COMMENTS):
        end_char = ";"
        index_list = []
        comment_str = (
            "   // "
            + self._get_key(index_list)
            + " "
            + self.get_frequency_string(index_list)
            if is_commented
            else ""
        )
        model_str = (
            model_str
            + "{ "
            + self.get_model_string(index_list)
            + " }"
            + end_char
            + comment_str
        )
        return model_str

    def _model_string_dim1_special_nobracket(
        self, model_str, is_commented=ADD_COMMENTS
    ):
        for i0 in range(self.size_list[0]):
            index_list = [i0]
            comment_str = (
                "   // "
                + self._get_key(index_list)
                + " "
                + self.get_frequency_string(index_list)
                if is_commented
                else ""
            )
            model_str = (
                model_str
                + "{ "
                + self.get_model_string(index_list)
                + " },"
                + comment_str
                + "\n"
            )
        return model_str

    def _model_string_dim1(self, model_str, is_commented=ADD_COMMENTS):
        end_char = ";"
        model_str += "{\n"
        for i0 in range(self.size_list[0]):
            index_list = [i0]
            comment_str = (
                "   // "
                + self._get_key(index_list)
                + " "
                + self.get_frequency_string(index_list)
                if is_commented
                else ""
            )
            model_str = (
                model_str
                + "  { "
                + self.get_model_string(index_list)
                + " },"
                + comment_str
                + "\n"
            )
        model_str += "}" + end_char
        return model_str

    def _model_string_dim2(self, model_str, is_commented=ADD_COMMENTS):
        end_char = ";"
        model_str += "{\n"
        for i0 in range(self.size_list[0]):
            model_str += "  {\n"
            for i1 in range(self.size_list[1]):
                index_list = [i0, i1]
                comment_str = (
                    "   // "
                    + self._get_key(index_list)
                    + " "
                    + self.get_frequency_string(index_list)
                    if is_commented
                    else ""
                )
                model_str = (
                    model_str
                    + "    { "
                    + self.get_model_string(index_list)
                    + " },"
                    + comment_str
                    + "\n"
                )
            model_str += "  },\n"
        model_str += "}" + end_char  # + '\n'
        return model_str

    def _model_string_dim3(self, model_str, is_commented=ADD_COMMENTS):
        end_char = ";"
        model_str += "{\n"
        for i0 in range(self.size_list[0]):
            model_str += "  {\n"
            for i1 in range(self.size_list[1]):
                model_str += "    {\n"
                for i2 in range(self.size_list[2]):
                    index_list = [i0, i1, i2]
                    comment_str = (
                        "   // "
                        + self._get_key(index_list)
                        + " "
                        + self.get_frequency_string(index_list)
                        if is_commented
                        else ""
                    )
                    model_str = (
                        model_str
                        + "      { "
                        + self.get_model_string(index_list)
                        + " },"
                        + comment_str
                        + "\n"
                    )
                model_str += "    },\n"
            model_str += "  },\n"
        model_str += "}" + end_char  # + '\n'
        return model_str

    def _model_string_dim4(self, model_str, is_commented=ADD_COMMENTS):
        end_char = ";"
        model_str += "{\n"
        for i0 in range(self.size_list[0]):
            model_str += "  {\n"
            for i1 in range(self.size_list[1]):
                model_str += "    {\n"
                for i2 in range(self.size_list[2]):
                    model_str += "      {\n"
                    for i3 in range(self.size_list[3]):
                        index_list = [i0, i1, i2, i3]
                        comment_str = (
                            "   // "
                            + self._get_key(index_list)
                            + " "
                            + self.get_frequency_string(index_list)
                            if is_commented
                            else ""
                        )
                        model_str = (
                            model_str
                            + "        { "
                            + self.get_model_string(index_list)
                            + " },"
                            + comment_str
                            + "\n"
                        )
                    model_str += "      },\n"
                model_str += "    },\n"
            model_str += "  },\n"
        model_str += "}" + end_char  # + '\n'
        return model_str
