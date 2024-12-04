#!/bin/bash

#create directories
mkdir -p binaries

if [ ! -f binaries/aomdec ]; then
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
        echo "Makefile exists: building aomdec..."
    fi

    # Makefile should exist
    if [ -f binaries/build/Makefile ]; then
        make aomdec -C ./binaries/build
    else
        echo "Error: Makefile does not exist cannot compile aomdec"
        exit 1
    fi

    # copy aomdec under binaries
    if [ -f binaries/build/aomdec ]; then
        cp ./binaries/build/aomdec ./binaries/aomdec
    else
        echo "Error: aomdec does not exist under ./binaries/build/"
        exit 1
    fi

    #clear build if aomdec is under binaries
    if [ -f binaries/aomdec ]; then
        rm -rf ./binaries/build/
        echo "Compilation complete!"
    else
        echo "Error: aomdec does not exist under ./binaries/"
        exit 1
    fi
else
    echo "Compilation skipped, because ./binaries/aomdec exists (delete aomdec and rerun this script to recompile from parent directory)."
fi
