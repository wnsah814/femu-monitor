#!/bin/bash
#
# run-zns.sh — boot a VM with one FEMU ZNS SSD (femu_mode=3)
#
# Required:
#   OSIMGF=<guest OS image, qcow2>
# Optional (environment variables, no rebuild needed):
#   EMTYF=<raw image>      extra virtio-scsi data disk
#   SSD_SIZE_MB            device size (default 4096)
#   ZNS_NUM_BLK            blocks per plane (default 32); zones = ZNS_NUM_BLK / 2
#                          with 2 planes per die, zone size = SSD_SIZE_MB / zones
#   ZNS_FLASH_TYPE         1 = SLC, 3 = TLC (default), 4 = QLC
#   ZNS_PG_RD_LAT / ZNS_PG_WR_LAT / ZNS_BLK_ER_LAT / ZNS_CH_XFER_LAT
#                          NAND timing in ns; 0 = the flash type's constants
#   ZNS_NUM_WC             number of write caches (default 14)
#   ZNS_WC_BYPASS=1        no write cache: every write is programmed immediately
#   ZNS_WC_UPSTREAM=1      the upstream (#145) write cache, for comparison
#   ZNS_PE_SUSPEND=1       NAND program/erase suspend for reads
#   ZNS_TSUSP_NS           suspend overhead per suspending read (ns)
#   ONCS=4|0               DSM (TRIM) supported / not supported (default 4)
#   SMP, MEM               guest vCPUs and memory (default 4, 4G)
#   SSH_PORT               host port forwarded to guest port 22 (default 8080)
#   FEMU_CPUS              pin the whole QEMU process with taskset (e.g. 0-31)
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
QEMU=${QEMU:-$DIR/build/build-femu/qemu-system-x86_64}
[[ -x $QEMU ]] || { echo "no $QEMU: build first (see README.md)"; exit 1; }
[[ -w /dev/kvm ]] || { echo "no write access to /dev/kvm"; exit 1; }
: "${OSIMGF:?set OSIMGF to the guest OS image}"
[[ -e $OSIMGF ]] || { echo "no OS image: $OSIMGF"; exit 1; }

SSD_SIZE_MB=${SSD_SIZE_MB:-4096}

FEMU_OPTIONS="-device femu,devsz_mb=${SSD_SIZE_MB},namespaces=1,femu_mode=3"
FEMU_OPTIONS+=",zns_num_ch=8,zns_num_lun=4,zns_num_plane=2,zns_num_blk=${ZNS_NUM_BLK:-32}"
FEMU_OPTIONS+=",zns_flash_type=${ZNS_FLASH_TYPE:-3}"
FEMU_OPTIONS+=",zns_pg_rd_lat=${ZNS_PG_RD_LAT:-0},zns_pg_wr_lat=${ZNS_PG_WR_LAT:-0}"
FEMU_OPTIONS+=",zns_blk_er_lat=${ZNS_BLK_ER_LAT:-0},zns_ch_xfer_lat=${ZNS_CH_XFER_LAT:-0}"
FEMU_OPTIONS+=",zns_num_wc=${ZNS_NUM_WC:-14},zns_wc_bypass=${ZNS_WC_BYPASS:-0}"
FEMU_OPTIONS+=",zns_wc_upstream=${ZNS_WC_UPSTREAM:-0}"
FEMU_OPTIONS+=",zns_pe_suspend=${ZNS_PE_SUSPEND:-0},zns_tsusp_ns=${ZNS_TSUSP_NS:-0}"
FEMU_OPTIONS+=",oncs=${ONCS:-4}"

# the emulated media is backed by host DRAM, allocated up front
REQ_MB=$(( SSD_SIZE_MB + $(echo "${MEM:-4G}" | sed 's/G//') * 1024 + 2048 ))
AVAIL_MB=$(( $(awk '/MemAvailable/ {print $2}' /proc/meminfo) / 1024 ))
if (( AVAIL_MB < REQ_MB )); then
    echo "run-zns: ${AVAIL_MB} MB available < ${REQ_MB} MB needed" >&2
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
    -name "FEMU-ZNS-VM" \
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
