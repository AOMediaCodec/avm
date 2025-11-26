#!/usr/bin/env python
## Copyright (c) 2021, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
__author__ = "maggie.sun@intel.com, ryanlei@meta.com"

import os
import re
import sys
import argparse
import subprocess

from Utils import CreateNewSubfolder
from Config import WorkPath, FrameNum, EnableParallelGopEncoding, GOP_SIZE

root_path = '/project/tenjin/fba/design/ryanlei/AV2-CTC/AV2-CTC-v11.0.0/'
test_path = os.path.join(root_path, 'test')
dec_path = os.path.join(test_path, 'decodedYUVs')
#ra_dec_log_path = os.path.join(test_path, 'RA_decLogs')
dec_log_path = os.path.join(test_path, 'decLogs')
ra_cmd_log_file = os.path.join(test_path, "AV2CTC_TestCmd_RA.log")
as_cmd_log_file = os.path.join(test_path, "AV2CTC_TestCmd_AS.log")
decoder = "%s/bin/aomdec-v11.0.0" % root_path
ra_dec_error_log = os.path.join(test_path, "ra_decode_error.log")
as_dec_error_log = os.path.join(test_path, "as_decode_error.log")
updated_ra_cmd_log = os.path.join(test_path, "AV2CTC_TestCmd_Update_RA.log")
updated_as_cmd_log = os.path.join(test_path, "AV2CTC_TestCmd_Update_AS.log")


def get_expected_frames(cfg):
    if cfg in ['LD', 'AI', 'STILL']:
        total_frame = FrameNum[cfg]
    elif cfg in ['RA', 'AS']:
        if EnableParallelGopEncoding:
            total_frame = GOP_SIZE
        else:
            total_frame = FrameNum[cfg]
    return total_frame

def check_decode_log(dec_log_file, cfg):
    if not os.path.isfile(dec_log_file):
        print("%s does not exists\n" % dec_log_file)
        return False
    else:
        dec_log = open(dec_log_file, 'r')
        for line in dec_log:
            m = re.search(r"(\d+) decoded frames", line)
            if m:
                frames = int(m.group(1))
                #print ("%s has %d frames" % (dec_log_file, frames))
                if (frames == get_expected_frames(cfg)):
                    return True
                else:
                    print("%s has %d frames\n" % (dec_log_file, frames))
                    return False

        dec_log.close()
    return False

def run_decode(cmd_log_file):
    CreateNewSubfolder(WorkPath, dec_log_path)
    cmd_log = open(cmd_log_file, 'r')
    for line in cmd_log:
        m = re.search(r"-o (.*).obu",line)
        if m:
            bitstream = m.group(1)
            output_file = bitstream + ".obu"
            video = bitstream.split("/")[-1]
            dec_log = os.path.join(dec_log_path, video + "_DecLog.txt")
            dec_output = os.path.join(dec_path, video + ".y4m")
            #print("bitstrams file is %s\n" % output_file)
            #print("decoding log file is %s\n" % dec_log)
            args = " --codec=av2 --summary -o %s %s" % (dec_output, output_file)
            dec_cmd = decoder + args + "> %s 2>&1"%dec_log
            #print("=== decoding %s \n" %video)
            #print(dec_cmd)
            #os.system(dec_cmd)
            cmd = "ENABLE_CONTAINER_CONFIG=1 grid run -autokill 0 -C modeling_c9 -r+ RAM/0" + " '" + dec_cmd + "'"
            #print(cmd)
            subprocess.call(cmd, shell=True)
    cmd_log.close()

def check_decoding(cmd_log_file, dec_error_log):
    cmd_log = open(cmd_log_file, 'r')
    error_list = []
    error_log = open(dec_error_log, 'wt')
    for line in cmd_log:
        m = re.search(r"-o (.*).obu(\s)",line)
        if m:
            #print(line)
            bitstream = m.group(1)
            output_file = bitstream + ".obu"
            video = bitstream.split("/")[-1]
            dec_log = os.path.join(dec_log_path, video + "_DecLog.txt")
            dec_output = os.path.join(dec_path, video + ".y4m")
            cfg = get_config(output_file)
            #print("checking %s for %s" % (dec_log, cfg))

            if check_decode_log(dec_log, cfg) == False:
                print("%s decoding error" % video)
                error_log.write(output_file + "\n")
                error_list.append(video)

            if os.path.exists(dec_output):
                del_cmd = "rm %s" % dec_output
                #print("=== deleting %s \n" %video)
                os.system(del_cmd)
                #input("press any key to continue")
    cmd_log.close()
    error_log.close()
    return error_list


def filter_cmd_log(cmd_log_file, updated_cmd_log_file, error_list):
    cmd_log = open(cmd_log_file, 'r')
    updated_cmd_log = open(updated_cmd_log_file, 'wt')
    start_copy = False

    for line in cmd_log:
        if start_copy:
            updated_cmd_log.write(line)

        m = re.search(r"============== (.*) Job Start =================", line)
        if m:
            video = m.group(1)
            if video in error_list:
                start_copy = True
                updated_cmd_log.write(line)
            else:
                video = video.replace("_AS_3840x2160_Preset", "_AS_Preset")
                if video in error_list:
                    start_copy = True
                    updated_cmd_log.write(line)

        m = re.search(r"============== (.*) Job End =================", line)
        if m:
            video = m.group(1)
            if video in error_list:
                start_copy = False
            else:
                video = video.replace("_AS_3840x2160_Preset", "_AS_Preset")
                if video in error_list:
                    start_copy = False

    cmd_log.close()
    updated_cmd_log.close()

def get_config(file_name):
    if ('_LD_' in file_name):
        return 'LD'
    elif ('_AS_' in file_name):
        return 'AS'
    elif ('_AI_' in file_name):
        return 'AI'
    elif ('_STILL_' in file_name):
        return 'STILL'
    elif ('_RA_' in file_name):
        return 'RA'
    else:
        print("unsupported configuration")
        return ''

def check_ld_decoding():
    cmd_log = open(cmd_log_file, 'r')
    error_list = []
    error_log = open(dec_error_log, 'wt')
    for line in cmd_log:
        m = re.search(r"=* (.*) Job Start", line)
        #m = re.search(r"-o (.*).obu",line)
        if m:
            bitstream = m.group(1)
            output_file = bitstream + ".obu"
            video = bitstream.split("/")[-1]
            cfg = get_config(video)
            if cfg in ['RA', 'AS']:
                dec_log = os.path.join(ra_dec_log_path, video + "_DecLog.txt")
            elif cfg in ['LD', 'AI', 'STILL']:
                dec_log = os.path.join(dec_log_path, video + "_DecLog.txt")

            dec_output = os.path.join(dec_path, video + ".y4m")
            if check_decode_log(dec_log, cfg) == False:
                #print(line)
                print("%s decoding error" % video)
                error_log.write(output_file + "\n")
                error_list.append(video)

            if os.path.exists(dec_output):
                del_cmd = "rm %s" % dec_output
                #print("=== deleting %s \n" %video)
                os.system(del_cmd)
                #input("press any key to continue")
    cmd_log.close()
    error_log.close()
    return error_list

######################################
# main
######################################
if __name__ == "__main__":

    """ steps for RA and AS
    #1. trigger decoding process, needed for RA
    run_decode(cmd_log_file)
    """

    """
    #2. check decoding errors
    error_list = check_decoding(ra_cmd_log_file, ra_dec_error_log)

    print("There are %d missing encodings" % len(error_list))
    filter_cmd_log(ra_cmd_log_file, updated_ra_cmd_log, error_list)
    """


    #steps for AS
    #1. check decoding errors
    error_list = check_decoding(as_cmd_log_file, as_dec_error_log)

    print("There are %d missing encodings" % len(error_list))
    filter_cmd_log(as_cmd_log_file, updated_as_cmd_log, error_list)


    """
    # steps for AI, LD, STILL
    #1. check decoding errors
    error_list = check_decoding(cmd_log_file, dec_error_log)

    print("There are %d missing encodings" % len(error_list))
    filter_cmd_log(cmd_log_file, updated_cmd_log, error_list)
    """