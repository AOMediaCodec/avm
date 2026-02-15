#!/usr/bin/env python
"""
Launch.py - Parse the TestCmd.log file and submit encoding jobs to the grid.

This script reads the *_TestCmd.log file, parses job markers, and submits
the pre-generated shell scripts to the compute cluster.

Usage:
    python Launch.py [log_file_name]

Examples:
    python Launch.py                        # Auto-find *_TestCmd.log in WorkPath
    python Launch.py AV2CTC_TestCmd.log     # Use specified file in WorkPath
    python Launch.py /path/to/TestCmd.log   # Use full path

If no argument is provided, the script will look for *_TestCmd.log in the
WorkPath defined in config.yaml. If only a filename is provided (without path),
it assumes the file is located under WorkPath.
"""

import os
import re
import sys
import glob
from Config import WorkPath

# Test configurations that have subfolders under cmdLogs
TEST_CONFIGURATIONS = ["AI", "LD", "RA", "AS", "STILL"]

def submit_job(job_file_path):
    """Submit a single job to the compute cluster"""
    # need to be implemented based on the interface of the cluster
    print("submitting job %s" % job_file_path)


def find_testcmd_log(work_path):
    """Find the *_TestCmd.log file in the given work path"""
    pattern = os.path.join(work_path, "*_TestCmd.log")
    log_files = glob.glob(pattern)
    if log_files:
        return log_files[0]
    return None


def extract_test_cfg_from_job_name(job_name):
    """
    Extract test configuration from job name.
    Job names typically contain the configuration in the middle: _AI_, _LD_, _RA_, _AS_, _STILL_
    """
    for cfg in TEST_CONFIGURATIONS:
        if "_" + cfg + "_" in job_name:
            return cfg
    # For ConvexHull jobs, they are AS configuration
    if "ConvexHull" in job_name or "convexhull" in job_name.lower():
        return "AS"
    return None


def parse_jobs_from_log(log_file_path, cmdlogs_path):
    """
    Parse the TestCmd.log file and extract job information.

    Parses job markers in the format:
        ============== {JobName} Job Start =================
        ...
        ============== {JobName} Job End ===================

    Returns a list of tuples: (test_cfg, job_name, job_script_path)
    """
    jobs = []

    with open(log_file_path, 'r') as f:
        for line in f:
            line = line.strip()

            # Look for job end markers: "============== {JobName} Job End ==================="
            # We use Job End to ensure we capture complete jobs
            match = re.search(r"=+\s+(.+?)\s+Job End\s+=+", line)
            if match:
                job_name = match.group(1)
                test_cfg = extract_test_cfg_from_job_name(job_name)

                if test_cfg:
                    # Verify the job script exists
                    job_script_path = os.path.join(cmdlogs_path, test_cfg, job_name + ".sh")
                    if os.path.exists(job_script_path):
                        jobs.append((test_cfg, job_name, job_script_path))
                    else:
                        print("Warning: Job script not found: %s" % job_script_path)
                else:
                    print("Warning: Could not determine test configuration for job: %s" % job_name)

    return jobs


def launch_jobs_from_log(log_file_path, cmdlogs_path):
    """
    Read the TestCmd.log file and launch jobs to the grid.

    Args:
        log_file_path: Path to the *_TestCmd.log file
        cmdlogs_path: Path to the cmdLogs directory

    Returns:
        Number of jobs launched
    """
    jobs = parse_jobs_from_log(log_file_path, cmdlogs_path)

    job_count = 0
    for test_cfg, job_name, job_script_path in jobs:
        print("Launching job: %s/%s" % (test_cfg, job_name))
        submit_job(job_script_path)
        job_count += 1

    return job_count


######################################
# main
######################################
if __name__ == "__main__":
    # Determine the log file path
    if len(sys.argv) >= 2:
        # Log file path/name provided as argument
        log_file_arg = sys.argv[1]

        # Check if it's just a filename (no directory separator)
        if os.path.dirname(log_file_arg) == "":
            # Just a filename, assume it's under WorkPath
            log_file = os.path.join(WorkPath, log_file_arg)
            work_path = WorkPath
        else:
            # Full or relative path provided
            log_file = log_file_arg
            work_path = os.path.dirname(os.path.abspath(log_file))

        if not os.path.exists(log_file):
            print("Error: Log file not found: %s" % log_file)
            sys.exit(1)
    else:
        # Use WorkPath from config.yaml and find the log file
        work_path = WorkPath
        log_file = find_testcmd_log(work_path)
        if not log_file:
            print("Error: No *_TestCmd.log file found in WorkPath: %s" % work_path)
            print("Usage: python Launch.py [log_file_name]")
            sys.exit(1)

    # Determine cmdLogs path
    cmdlogs_path = os.path.join(work_path, "cmdLogs")

    if not os.path.exists(cmdlogs_path):
        print("Error: cmdLogs directory not found: %s" % cmdlogs_path)
        sys.exit(1)

    os.chdir(work_path)

    print("=== WorkPath: %s ===" % work_path)
    print("=== Reading job information from: %s ===" % log_file)
    print("=== Shell scripts directory: %s ===" % cmdlogs_path)

    total_jobs = launch_jobs_from_log(log_file, cmdlogs_path)
    print("\n=== Total jobs launched: %d ===" % total_jobs)
