/*
 * femon_protocol.h — the femonitor <-> FEMU monitoring protocol
 *
 * The single definition included by both the device (femu/hw/femu) and the
 * host tool (femonitor). The structures here are the wire format of the
 * admin command payloads, so add or change fields only in this file and
 * rebuild both sides. Sizes are pinned by _Static_assert.
 *
 * Typedef names keep the ones the host tool has always used
 * (bbssd_*_t / znssd_*_t); new device-side code uses the same names.
 */
#ifndef FEMON_PROTOCOL_H
#define FEMON_PROTOCOL_H

#include <stdint.h>

/* ---------------------------------------------------------- admin opcodes */
#define NVME_ADM_CMD_FEMON_RESET  0xD4 /* clear all registered cumulative counters
                                        * and histograms (gauges and state kept) */
#define NVME_ADM_CMD_DEVTYPE     0xD0  /* device type and geometry */
#define NVME_ADM_CMD_FEMON_SCHEMA 0xD1 /* registry schema (self-describing) */
#define NVME_ADM_CMD_FEMON_FETCH  0xD2 /* registry values (all or one) */
#define NVME_ADM_CMD_FEMON_TRACE  0xD3 /* tracepoint histograms on/off
                                        * cdw11 = 1 (on) / 0 (off),
                                        * cdw12 = FEMON_SEL_ALL or a metric id */

/* ------------------------------------------------------------ device type */
typedef uint64_t femon_device_type_t;
enum {
    FEMON_DEVICE_BBSSD = 0,
    FEMON_DEVICE_ZNSSD = 1,
};

/* ------------------------------------------------- 0xD0: device info */
typedef struct {
    femon_device_type_t type;
    union {
        struct {
            int secsz;        int secs_per_pg;  int pgs_per_blk;
            int blks_per_pl;  int pls_per_lun;  int luns_per_ch;
            int nchs;         int secs_per_blk; int secs_per_pl;
            int secs_per_lun; int secs_per_ch;  int tt_secs;
            int pgs_per_pl;   int pgs_per_lun;  int pgs_per_ch;
            int tt_pgs;       int blks_per_lun; int blks_per_ch;
            int tt_blks;      int secs_per_line; int pgs_per_line;
            int blks_per_line; int tt_lines;    int pls_per_ch;
            int tt_pls;       int tt_luns;
        } bbssd;
        struct {
            uint64_t num_ch;
            uint64_t num_lun;
            uint64_t nr_zones;
            uint64_t num_planes;     /* total number of planes */
            uint64_t planes_per_die;
            uint64_t dies_per_chip;
        } znssd;
    };
} femon_device_info_t;

/* ------------------------------------------------- femonitor view model: BBSSD
 * Not a wire format. The client rebuilds these from the 0xD2 values of the
 * bbssd.* metrics and hands them to the views. */
typedef struct {
    int curline_id;
    int ch;
    int lun;
    int pg;
    int blk;
    int pl;
} bbssd_write_pointer_info_t;

typedef struct {
    int id;
    int ipc;
    int vpc;
    uint64_t pos;   /* position in the victim priority queue */
} bbssd_line_info_t;

typedef struct {
    bbssd_write_pointer_info_t wp_info;
    uint64_t gc_count;
    uint64_t gc_pgs;
    int tt_lines;
    int free_line_cnt;
    int victim_line_cnt;
    int full_line_cnt;
} bbssd_monitor_header_t;

/* ------------------------------------------------- femonitor view model: ZNS
 * Not a wire format. The client rebuilds these from the 0xD2 values of the
 * zns.* metrics and hands them to the views. */
typedef struct {
    uint8_t  zt;    /* zone type */
    uint8_t  zs;    /* zone state */
    uint8_t  za;    /* zone attributes */
    uint64_t zcap;
    uint64_t zslba;
    uint64_t wp;
} znssd_zone_descr_info_t;

typedef struct {
    znssd_zone_descr_info_t d;
    uint64_t w_ptr;
    uint64_t cnt_reset;  /* cumulative resets of this zone */
} znssd_zone_info_t;

typedef struct {
    uint64_t total_xfer_count;   /* NAND page transfers */
    uint64_t conflict_count;
    uint64_t total_wait_time_ns;
} znssd_channel_stat_t;

typedef struct {
    uint64_t total_io_count;
    uint64_t conflict_count;
    uint64_t total_wait_time_ns;
} znssd_plane_stat_t;

typedef struct {
    uint64_t num_zones;
    uint64_t zr_count;      /* cumulative zone resets, all zones */
    uint64_t num_channels;
    uint64_t num_planes;
    /* write cache statistics, counted per write command.
     * hit  : a cache is bound to the request's zone and absorbs it
     * miss : no cache bound to the zone (including taking a free one)
     * evict: misses that took another zone's cache and paid its flush
     * flush: all flushes to NAND (evictions + full caches) */
    uint64_t wc_hit;
    uint64_t wc_miss;
    uint64_t wc_evict;
    uint64_t wc_flush;
} znssd_monitor_header_t;

/* ------------------------------------------- 0xD1/0xD2: metric registry
 *
 * Registration-based metric protocol with a self-describing payload: the
 * host first reads the schema with 0xD1 (which metrics exist and how many
 * values each has), and uses it to size and decode the 0xD2 payload. A
 * metric newly registered in the device (femon_registry.h) appears in the
 * schema without any change to the host tool.
 *
 * 0xD1 SCHEMA: cdw10 = NUMD (buffer dwords - 1).
 *   payload = femon_schema_hdr_t + femon_schema_rec_t[count]
 * 0xD2 FETCH:  cdw10 = NUMD, cdw11 = selector
 *   (FEMON_SEL_ALL = all, otherwise that one id).
 *   payload = femon_fetch_hdr_t + { femon_fetch_rec_t + uint64_t[nvals] }*
 *   version must match the schema generation; if not, the host re-reads. */

#define FEMON_SEL_ALL   0xFFFFFFFFu

enum {
    FEMON_MT_U64  = 0,   /* cumulative counter or gauge, u64 (scalar or array) */
    FEMON_MT_HIST = 1,   /* log2 histogram buckets (tracepoints) */
};

/* FEMON_MT_HIST payload: dims[0] = FEMON_HIST_NVALS u64 values.
 * The first FEMON_HIST_BUCKETS are log2 buckets in µs (bucket i =
 * [2^i, 2^(i+1)) µs, i = 0 is [0, 2) µs, the last has no upper bound); the
 * last 4 are n, sum, min, max in ns (buckets show the order of magnitude,
 * the statistics keep full precision; the renderer converts). Buckets alone
 * would put the mean within a factor of 2, so the exact statistics travel
 * with them. Invariant: sum of buckets == n. */
#define FEMON_HIST_BUCKETS 16
#define FEMON_HIST_NVALS   (FEMON_HIST_BUCKETS + 4)

#define FEMON_MAX_DIMS  3
#define FEMON_NAME_LEN  48   /* including NUL */

typedef struct {
    uint32_t version;    /* schema generation (bumped on every registration) */
    uint32_t count;      /* number of records that follow */
} femon_schema_hdr_t;

typedef struct {
    uint16_t id;
    uint8_t  type;                     /* FEMON_MT_* */
    uint8_t  ndims;                    /* 0 = scalar */
    uint32_t dims[FEMON_MAX_DIMS];     /* the first ndims are valid */
    char     name[FEMON_NAME_LEN];     /* NUL-terminated, <subsys>.<obj>.<metric> */
} femon_schema_rec_t;

typedef struct {
    uint32_t version;
    uint32_t count;
} femon_fetch_hdr_t;

typedef struct {
    uint16_t id;
    uint16_t rsvd;
    uint32_t nvals;      /* uint64_t values that follow (= product of dims) */
} femon_fetch_rec_t;

/* ------------------------------------------------- pinned wire format */
_Static_assert(sizeof(bbssd_write_pointer_info_t) == 24, "bb wp_info layout");
_Static_assert(sizeof(bbssd_line_info_t) == 24, "bb line_info layout");
_Static_assert(sizeof(bbssd_monitor_header_t) == 56, "bb monitor header layout");
_Static_assert(sizeof(znssd_zone_descr_info_t) == 32, "zns zone descr layout");
_Static_assert(sizeof(znssd_zone_info_t) == 48, "zns zone info layout");
_Static_assert(sizeof(znssd_channel_stat_t) == 24, "zns channel stat layout");
_Static_assert(sizeof(znssd_plane_stat_t) == 24, "zns plane stat layout");
_Static_assert(sizeof(znssd_monitor_header_t) == 64, "zns monitor header layout");
_Static_assert(sizeof(femon_device_info_t) == 112, "device info layout");
_Static_assert(sizeof(femon_schema_hdr_t) == 8, "femon schema hdr layout");
_Static_assert(sizeof(femon_schema_rec_t) == 64, "femon schema rec layout");
_Static_assert(sizeof(femon_fetch_hdr_t) == 8, "femon fetch hdr layout");
_Static_assert(sizeof(femon_fetch_rec_t) == 8, "femon fetch rec layout");

#endif /* FEMON_PROTOCOL_H */
