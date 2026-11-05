#!/bin/bash
#
# run-multi.sh — boot a VM with a conventional SSD and a ZNS SSD, for an
# F2FS multi-device volume. In the guest, the CNS device is /dev/nvme0n1 and
# the ZNS device is /dev/nvme1n1.
#
# Required:
#   OSIMGF=<guest OS image, qcow2>
# Optional (environment variables, no rebuild needed):
#   EMTYF=<raw image>      extra virtio-scsi data disk
#   CNS_SIZE_MB            exposed CNS capacity (default 1024); physical = 4/3 of it (25% OP)
#   CNS_SECS_PER_PG        8 = 4 KiB pages (default), 32 = 16 KiB pages
#   CNS_PGS_PER_BLK        pages per block (default 256)
#   CNS_PG_RD_LAT / CNS_PG_WR_LAT / CNS_BLK_ER_LAT / CNS_CH_XFER_LAT
#                          CNS NAND timing in ns (default 40000 / 200000 / 2000000 / 0)
#   CNS_PE_SUSPEND=1, CNS_TSUSP_NS, CNS_HOT_COLD_SEP=on    as in run-cns.sh
#   ZNS_SIZE_MB            ZNS device size (default 4096)
#   ZNS_NUM_LUN, ZNS_NUM_PLANE, ZNS_NUM_BLK
#                          dies per channel (default 4), planes per die (default 2),
#                          blocks per plane (default 128); zones = blocks per plane
#                          / planes per die
#   ZNS_FLASH_TYPE, ZNS_*_LAT, ZNS_NUM_WC, ZNS_WC_BYPASS, ZNS_PE_SUSPEND,
#   ZNS_TSUSP_NS           as in run-zns.sh
#   ONCS=4|0               DSM (TRIM) on both devices (default 4)
#   SMP, MEM, SSH_PORT (default 8081), FEMU_CPUS   as in run-zns.sh
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
QEMU=${QEMU:-$DIR/build/build-femu/qemu-system-x86_64}
[[ -x $QEMU ]] || { echo "no $QEMU: build first (see README.md)"; exit 1; }
[[ -w /dev/kvm ]] || { echo "no write access to /dev/kvm"; exit 1; }
: "${OSIMGF:?set OSIMGF to the guest OS image}"
[[ -e $OSIMGF ]] || { echo "no OS image: $OSIMGF"; exit 1; }

# ---- CNS: 8 ch x 8 dies x 1 plane; blocks per plane set from the physical size
CNS_SIZE_MB=${CNS_SIZE_MB:-1024}
CNS_PHYS_MB=$(( CNS_SIZE_MB * 4 / 3 ))
CNS_SECS_PER_PG=${CNS_SECS_PER_PG:-8}
CNS_PGS_PER_BLK=${CNS_PGS_PER_BLK:-256}
CNS_BLK_KIB=$(( CNS_PGS_PER_BLK * CNS_SECS_PER_PG / 2 ))
CNS_BLKS_PER_PL=$(( CNS_PHYS_MB * 1024 / CNS_BLK_KIB / 64 ))

CNS_OPT="-device femu,devsz_mb=${CNS_SIZE_MB},namespaces=1,femu_mode=1"
CNS_OPT+=",secsz=512,secs_per_pg=${CNS_SECS_PER_PG},pgs_per_blk=${CNS_PGS_PER_BLK}"
CNS_OPT+=",blks_per_pl=${CNS_BLKS_PER_PL},pls_per_lun=1,luns_per_ch=8,nchs=8"
CNS_OPT+=",pg_rd_lat=${CNS_PG_RD_LAT:-40000},pg_wr_lat=${CNS_PG_WR_LAT:-200000}"
CNS_OPT+=",blk_er_lat=${CNS_BLK_ER_LAT:-2000000},ch_xfer_lat=${CNS_CH_XFER_LAT:-0}"
CNS_OPT+=",gc_thres_pcent=75,gc_thres_pcent_high=95"
CNS_OPT+=",pe_suspend=${CNS_PE_SUSPEND:-0},tsusp_ns=${CNS_TSUSP_NS:-0}"
CNS_OPT+=",hot_cold_sep=${CNS_HOT_COLD_SEP:-off},oncs=${ONCS:-4}"

# ---- ZNS
ZNS_SIZE_MB=${ZNS_SIZE_MB:-4096}
ZNS_OPT="-device femu,devsz_mb=${ZNS_SIZE_MB},namespaces=1,femu_mode=3"
ZNS_OPT+=",zns_num_ch=8,zns_num_lun=${ZNS_NUM_LUN:-4},zns_num_plane=${ZNS_NUM_PLANE:-2}"
ZNS_OPT+=",zns_num_blk=${ZNS_NUM_BLK:-128},zns_flash_type=${ZNS_FLASH_TYPE:-3}"
ZNS_OPT+=",zns_pg_rd_lat=${ZNS_PG_RD_LAT:-0},zns_pg_wr_lat=${ZNS_PG_WR_LAT:-0}"
ZNS_OPT+=",zns_blk_er_lat=${ZNS_BLK_ER_LAT:-0},zns_ch_xfer_lat=${ZNS_CH_XFER_LAT:-0}"
ZNS_OPT+=",zns_num_wc=${ZNS_NUM_WC:-14},zns_wc_bypass=${ZNS_WC_BYPASS:-0}"
ZNS_OPT+=",zns_pe_suspend=${ZNS_PE_SUSPEND:-0},zns_tsusp_ns=${ZNS_TSUSP_NS:-0}"
ZNS_OPT+=",oncs=${ONCS:-4}"

REQ_MB=$(( CNS_PHYS_MB + ZNS_SIZE_MB + $(echo "${MEM:-4G}" | sed 's/G//') * 1024 + 2048 ))
AVAIL_MB=$(( $(awk '/MemAvailable/ {print $2}' /proc/meminfo) / 1024 ))
if (( AVAIL_MB < REQ_MB )); then
    echo "run-multi: ${AVAIL_MB} MB available < ${REQ_MB} MB needed" >&2
    exit 1
fi
if [[ $(ulimit -l) != unlimited ]]; then
    echo "note: memlock limit $(ulimit -l) KB; the backends run unpinned" \
         "(raise RLIMIT_MEMLOCK for timing measurements)" >&2
fi
echo "[run-multi] CNS: ${CNS_OPT#-device }" >&2
echo "[run-multi] ZNS: ${ZNS_OPT#-device }" >&2

DATA=()
[[ -n ${EMTYF:-} ]] && DATA=(-device scsi-hd,drive=hd1
    -drive "file=$EMTYF,if=none,aio=native,cache=none,format=raw,id=hd1")
PIN=(); [[ -n ${FEMU_CPUS:-} ]] && PIN=(taskset -c "$FEMU_CPUS")

exec ${PIN[@]+"${PIN[@]}"} "$QEMU" \
    -name "FEMU-MULTI-VM" \
    -enable-kvm -cpu host -smp "${SMP:-4}" -m "${MEM:-4G}" \
    -device virtio-scsi-pci,id=scsi0 \
    -device scsi-hd,drive=hd0 \
    -drive "file=$OSIMGF,if=none,aio=native,cache=none,format=qcow2,id=hd0" \
    ${DATA[@]+"${DATA[@]}"} \
    ${CNS_OPT} \
    ${ZNS_OPT} \
    -net user,hostfwd=tcp::"${SSH_PORT:-8081}"-:22 \
    -net nic,model=virtio \
    -nographic \
    -qmp unix:./qmp-sock,server,nowait
