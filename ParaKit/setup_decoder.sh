#!/bin/bash

#create directories
mkdir -p binaries

if [ ! -f binaries/avmdec ]; then
    #check if AVM directory is available based on LICENSE file
    is_parent_avm=$(cat ../LICENSE | grep "Alliance for Open Media")
    if [ -n "$is_parent_avm" ]; then
        echo "AVM software is available in parent directory."
    else
        echo "Error: AVM is not available in parent directory."
        exit 1
    fi
    # create build directory
    mkdir -p binaries/build

    # check Makefile
    if [ ! -f binaries/build/Makefile ]; then
        cmake -S ../ -B ./binaries/build -DCONFIG_PARAKIT_COLLECT_DATA=1 -DCONFIG_ML_PART_SPLIT=0 -DCONFIG_MULTITHREAD=0
    else
        echo "Makefile exists: building avmdec..."
    fi

    # Makefile should exist
    if [ -f binaries/build/Makefile ]; then
        make avmdec -C ./binaries/build
    else
        echo "Error: Makefile does not exist cannot compile avmdec"
        exit 1
    fi

    # copy avmdec under binaries
    if [ -f binaries/build/avmdec ]; then
        cp ./binaries/build/avmdec ./binaries/avmdec
    else
        echo "Error: avmdec does not exist under ./binaries/build/"
        exit 1
    fi

    #clear build if avmdec is under binaries
    if [ -f binaries/avmdec ]; then
        rm -rf ./binaries/build/
        echo "Compilation complete!"
    else
        echo "Error: avmdec does not exist under ./binaries/"
        exit 1
    fi
else
    echo "Compilation skipped, because ./binaries/avmdec exists (delete avmdec and rerun this script to recompile from parent directory)."
fi
