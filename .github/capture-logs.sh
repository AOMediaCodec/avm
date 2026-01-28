#!/bin/bash

# handle pipe failures
set -o pipefail

# variables
logfile="$1"

# the rest of parameters is the command to execute
shift

# run command
"$@" 2>&1 | tee "${logfile}"
exit_code=$?

# append 'success' or 'failure' to logfile name
if [ ${exit_code} -eq 0 ]; then
	mv --verbose "${logfile}" "$(echo "${logfile}" | sed -E 's/\.log$/.success.log/')"
else
	mv --verbose "${logfile}" "$(echo "${logfile}" | sed -E 's/\.log$/.failure.log/')"
fi

# forward exit code to caller process
exit ${exit_code}
