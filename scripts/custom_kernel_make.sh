#!/bin/bash

if [ -z "$AT91SAM_TOOLCHAIN_PATH" ]; then
    echo "toochain is not specified! please set AT91SAM_TOOLCHAIN_PATH"
fi

action=$1

if [ -z "$action" ]; then
    echo "please, specify what you want to do. make or cfg?"
fi

if [ "$action" == "cfg" ]; then
    CROSS_COMPILE="$AT91SAM_TOOLCHAIN_PATH" \
        ARCH=arm make sk_at91_xc6slx_dt_defconfig
fi

if [ "$action" == "make" ]; then
    CROSS_COMPILE="$AT91SAM_TOOLCHAIN_PATH" \
         ARCH=arm LOADADDR=70008000 make uImage -j4 VERBOSE=1
fi
