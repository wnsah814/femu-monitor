#!/bin/bash
#
# run-cns.sh — boot a VM with one FEMU conventional (block-interface) SSD
# (femu_mode=1, the BBSSD white-box FTL)
#
# Default geometry: 8 ch x 8 dies x 1 plane, 4 KiB pages, 256 pages per block,
# CNS_BLKS_PER_PL blocks per plane (physical = CNS_BLKS_PER_PL x 64 MiB),
# 25% over-provisioning.
#
# Required:
#   OSIMGF=<guest OS image, qcow2>
# Optional (environment variables, no rebuild needed):
#   EMTYF=<raw image>      extra virtio-scsi data disk
#   CNS_BLKS_PER_PL        blocks per plane (default 128 = 8 GiB physical, 6 GiB exposed)
#   CNS_PG_RD_LAT / CNS_PG_WR_LAT / CNS_BLK_ER_LAT / CNS_CH_XFER_LAT
#                          NAND timing in ns (default 40000 / 200000 / 2000000 / 0)
#   CNS_PE_SUSPEND=1       NAND program/erase suspend for reads
#   CNS_TSUSP_NS           suspend overhead per suspending read (ns)
#   CNS_HOT_COLD_SEP=on    separate host writes from GC copies
#   ONCS=4|0               DSM (TRIM) supported / not supported (default 4)
#   SMP, MEM, SSH_PORT, FEMU_CPUS   as in run-zns.sh
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
QEMU=${QEMU:-$DIR/build/build-femu/qemu-system-x86_64}
[[ -x $QEMU ]] || { echo "no $QEMU: build first (see README.md)"; exit 1; }
[[ -w /dev/kvm ]] || { echo "no write access to /dev/kvm"; exit 1; }
: "${OSIMGF:?set OSIMGF to the guest OS image}"
[[ -e $OSIMGF ]] || { echo "no OS image: $OSIMGF"; exit 1; }

BLKS_PER_PL=${CNS_BLKS_PER_PL:-128}
PHYS_MB=$(( BLKS_PER_PL * 64 ))
SSD_SIZE_MB=$(( PHYS_MB * 3 / 4 ))

FEMU_OPTIONS="-device femu,devsz_mb=${SSD_SIZE_MB},namespaces=1,femu_mode=1"
FEMU_OPTIONS+=",secsz=512,secs_per_pg=8,pgs_per_blk=256,blks_per_pl=${BLKS_PER_PL}"
FEMU_OPTIONS+=",pls_per_lun=1,luns_per_ch=8,nchs=8"
FEMU_OPTIONS+=",pg_rd_lat=${CNS_PG_RD_LAT:-40000},pg_wr_lat=${CNS_PG_WR_LAT:-200000}"
FEMU_OPTIONS+=",blk_er_lat=${CNS_BLK_ER_LAT:-2000000},ch_xfer_lat=${CNS_CH_XFER_LAT:-0}"
FEMU_OPTIONS+=",gc_thres_pcent=75,gc_thres_pcent_high=95"
FEMU_OPTIONS+=",pe_suspend=${CNS_PE_SUSPEND:-0},tsusp_ns=${CNS_TSUSP_NS:-0}"
FEMU_OPTIONS+=",hot_cold_sep=${CNS_HOT_COLD_SEP:-off}"
FEMU_OPTIONS+=",oncs=${ONCS:-4}"

REQ_MB=$(( PHYS_MB + $(echo "${MEM:-4G}" | sed 's/G//') * 1024 + 2048 ))
AVAIL_MB=$(( $(awk '/MemAvailable/ {print $2}' /proc/meminfo) / 1024 ))
if (( AVAIL_MB < REQ_MB )); then
    echo "run-cns: ${AVAIL_MB} MB available < ${REQ_MB} MB needed" >&2
    exit 1
fi
if [[ $(ulimit -l) != unlimited ]]; then
    echo "note: memlock limit $(ulimit -l) KB; the backend runs unpinned" \
         "(raise RLIMIT_MEMLOCK for timing measurements)" >&2
fi

DATA=()
[[ -n ${EMTYF:-} ]] && DATA=(-device scsi-hd,drive=hd1
    -drive "file=$EMTYF,if=none,aio=native,cache=none,format=raw,id=hd1")
PIN=(); [[ -n ${FEMU_CPUS:-} ]] && PIN=(taskset -c "$FEMU_CPUS")

exec ${PIN[@]+"${PIN[@]}"} "$QEMU" \
    -name "FEMU-CNS-VM" \
    -enable-kvm -cpu host -smp "${SMP:-4}" -m "${MEM:-4G}" \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    -drive "file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0" \
    ${DATA[@]+"${DATA[@]}"} \
    ${FEMU_OPTIONS} \
    -net user,hostfwd=tcp::"${SSH_PORT:-8080}"-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait
