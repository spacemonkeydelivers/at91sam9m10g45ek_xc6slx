#!/bin/bash

warn () {
    echo "$0:" "$@" >&2
}
#thanks to tripleee (https://stackoverflow.com/questions/7868818/in-bash-is-there-an-equivalent-of-die-error-msg)
die () {
    rc=$1
    shift
    warn "$@"
    exit $rc
}

SK_AT91_FPGA_LINUX_ROOT=$1

if [ -z "$SK_AT91_FPGA_LINUX_ROOT" ]; then
    echo "the path to linux sources is not specified"
    exit -1
fi

PATH_KERNEL_README=$(realpath "$SK_AT91_FPGA_LINUX_ROOT/README" -m)
PATH_DRIVER_DST=$(realpath "$SK_AT91_FPGA_LINUX_ROOT/drivers/misc" -m)
PATH_DTS_DST=$(realpath "$SK_AT91_FPGA_LINUX_ROOT/arch/arm/boot/dts" -m)
PATH_CFG_DST=$(realpath "$SK_AT91_FPGA_LINUX_ROOT/arch/arm/configs" -m)

echo "searching for kernel README @\"$PATH_KERNEL_README\""
if [ ! -f "$PATH_KERNEL_README" ]; then
    echo "kernel README file is not found"
    exit -1
fi
echo "found, performing sanity checking..."

if [ "`head -n 1 "$PATH_KERNEL_README"`" != "Linux kernel" ]; then
    echo "sanity checks failed :( - destination does not look like a kernel"
    exit -2
fi
echo "done, looks like destination contains the kernel..."

echo "checking that destination ($PATH_DRIVER_DST) exists"
if [ ! -d "$PATH_DRIVER_DST" ]; then
    echo "destination does not exists. something is fishy"
    exit -3
fi

THIS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PATH_DRIVER_ROOT=""

function make_link {
    SRC_DIR="$1"
    DST_DIR="$2"
    FILENAME="$3"

    if [ -f "$DST_DIR/$FILENAME" ]; then
        unlink "$DST_DIR/$FILENAME" 2>/dev/null
    fi 

    echo "$SRC_DIR/$FILENAME"
    echo "     <- $DST_DIR/$FILENAME"
    SRC_FILE=$(realpath "$SRC_DIR/$FILENAME")
    ln -s "$SRC_FILE" "$DST_DIR/$FILENAME" \
        || die 2 "could not create symlink to $FILENAME (<- $SRC_DIR/$FILENAME)"
}

DRIVER_C_SOURCE="fpga-sk-at91sam9m10g45-xc6slx.c"
DRIVER_H_SOURCE="fpga-sk-at91sam9m10g45-xc6slx.h"
LINUX_DTS_SOURCE="sk_at91sam9m10g45ek_xc6slx.dts"
LINUX_CONFIG_SOURCE="sk_at91_xc6slx_dt_defconfig"

make_link "$THIS_DIR/../linux/kernel/dev_fpga" "$PATH_DRIVER_DST"  "$DRIVER_C_SOURCE"
make_link "$THIS_DIR/../linux/kernel/dev_fpga" "$PATH_DRIVER_DST"  "$DRIVER_H_SOURCE"
make_link "$THIS_DIR/../linux/kernel/dts"      "$PATH_DTS_DST"     "$LINUX_DTS_SOURCE"
make_link "$THIS_DIR/../linux/kernel/config"   "$PATH_CFG_DST"     "$LINUX_CONFIG_SOURCE"

echo "Applying patches:"
cd $SK_AT91_FPGA_LINUX_ROOT
# maybe we need somehow to preserve the order of the patches
for p in $(ls "$THIS_DIR/../linux/kernel/patches")
do
    echo "    Applying $p"
    patch -p1 < $(realpath "$THIS_DIR/../linux/kernel/patches/$p" -m) -s
done
cd $THIS_DIR

echo "great success"
