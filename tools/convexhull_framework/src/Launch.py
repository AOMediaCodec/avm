#!/usr/bin/env python

import os
import re
import sys
import subprocess
from Config import WorkPath

######################################
# main
######################################
if __name__ == "__main__":
    cmd_log_file = sys.argv[1]
    curr_path = os.getcwd()
    Path_CmdLog = os.path.join(WorkPath, "cmdLogs")
    cmd_log_path = os.path.abspath(Path_CmdLog)
    os.chdir(WorkPath)

    cmd_log = open(cmd_log_file, "rt")

    width = 0; height = 0; job_name = None; job_file = None
    for line in cmd_log:
        if line == "\n":
            continue

        m = re.search(r"=+\s+(.*)\s+Job\s+Start\s+=+", line)
        if m:
            job_name = m.group(1)
            #print(job_name)
            job_file_name = os.path.join(Path_CmdLog, job_name + ".sh")
            job_file = open(job_file_name, 'wt')
            job_file.write("#!/bin/bash\n\n")
            m = re.search(r"_(\d+)x(\d+)", job_name)
            if m:
                width = int(m.group(1))
                height = int(m.group(2))
            continue

        m = re.search(r"::", line)
        if m:
            continue

        m = re.search(r"=+\s+(.*)\s+Job\s+End\s+=+",line)
        if m:
            job_file.close()
            os.chmod(job_file_name, 0o755)
            extra_memory = "0"
            max_wh = max(width, height)
            if max_wh >= 3840:
                extra_memory = "4500"

            width = 0; height = 0; job_name = None; job_file = None
            #cmd = "nc run -C nolic_batch -r+ RAM/" + extra_memory + " " + job_file_name
            #cmd = "nc run -C modeling -r+ RAM/" + extra_memory + " " + job_file_name
            #cmd = "nc run -autokill 0 -C modeling -r+ \"osversion>=8\" -r+ RAM/" + extra_memory + " " + job_file_name
            cmd = "ENABLE_CONTAINER_CONFIG=1 grid run -autokill 0 -C modeling_c9 -r+ RAM/" + extra_memory + " " + job_file_name
            print(cmd)
            subprocess.call(cmd, shell=True)
        else:
            job_file.write(line)
            job_file.write("wait\n")

    cmd_log.close()