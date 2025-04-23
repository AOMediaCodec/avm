#!/bin/bash

echo "--- Installing ParaKit ---"
if [ ! -d venv ]; then
        pipdir=`which pip3`
        if [ $? -ne 0 ]; then
            echo "Installing pip3 with the latest version of python3"
            brew install python3
        fi
    python3 -m venv venv
    source venv/bin/activate
    # install required packages
    python3 -m pip install --require-hashes -r requirements.txt
    # install package locally
    python3 -m pip install --no-deps -e .
else
    echo "venv exists: activating"
    source venv/bin/activate
fi
mkdir -p binaries
mkdir -p bitstreams
mkdir -p results/
mkdir -p results/data
mkdir -p unit_test/data

export PYTHONPATH=$(pwd)
echo "Setup Complete!"
