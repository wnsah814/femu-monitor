#include "../nvme.h"
#include "../femon_protocol.h"
#include "../femon_registry.h"
#include "./ftl.h"

static void bb_init_ctrl_str(FemuCtrl *n)
{
    static int fsid_vbb = 0;
    const char *vbbssd_mn = "FEMU BlackBox-SSD Controller";
    const char *vbbssd_sn = "vSSD";

    nvme_set_ctrl_name(n, vbbssd_mn, vbbssd_sn, &fsid_vbb);
}

/* ---- femon registry (0xD1/0xD2): metric registration (int fields via callbacks) */
static void bb_fill_line_cnt(void *opaque, uint64_t *out)
{
    struct ssd *ssd = ((FemuCtrl *)opaque)->ssd;

    out[0] = ssd->lm.free_line_cnt;
    out[1] = ssd->lm.victim_line_cnt;
    out[2] = ssd->lm.full_line_cnt;
}

/* per-line state (gauges) and the write pointer: used by the TUI line view */
static void bb_fill_line_at(void *opaque, uint64_t *out, int which)
{
    struct ssd *ssd = ((FemuCtrl *)opaque)->ssd;
    for (int i = 0; i < ssd->lm.tt_lines; i++) {
        switch (which) {
        case 0: out[i] = (uint64_t)(int64_t)ssd->lm.lines[i].ipc; break;
        case 1: out[i] = (uint64_t)(int64_t)ssd->lm.lines[i].vpc; break;
        case 2: out[i] = ssd->lm.lines[i].pos; break;
        case 3: out[i] = (uint64_t)ssd->lm.lines[i].nsec; break;
        default: out[i] = (ssd->lm.lines[i].minlpn == UINT64_MAX) ? 0 : ssd->lm.lines[i].minlpn; break;
        }
    }
}
static void bb_fill_line_ipc(void *o, uint64_t *out) { bb_fill_line_at(o, out, 0); }
static void bb_fill_line_vpc(void *o, uint64_t *out) { bb_fill_line_at(o, out, 1); }
static void bb_fill_line_pos(void *o, uint64_t *out) { bb_fill_line_at(o, out, 2); }
static void bb_fill_line_nsec(void *o, uint64_t *out) { bb_fill_line_at(o, out, 3); }
static void bb_fill_line_minlpn(void *o, uint64_t *out) { bb_fill_line_at(o, out, 4); }
static void bb_fill_wp(void *opaque, uint64_t *out)
{
    struct ssd *ssd = ((FemuCtrl *)opaque)->ssd;
    out[0] = (uint64_t)(int64_t)(ssd->wp.curline ? ssd->wp.curline->id : -1);
    out[1] = ssd->wp.ch;  out[2] = ssd->wp.lun; out[3] = ssd->wp.pg;
    out[4] = ssd->wp.blk; out[5] = ssd->wp.pl;
}

static void bb_register_metrics(FemuCtrl *n)
{
    struct ssd *ssd = n->ssd;
    femon_registry_t *reg = femon_registry_new();
    uint32_t d3[1] = { 3 };  /* free / victim / full */

    femon_register_u64(reg, "bbssd.gc.count", &ssd->mon_gc_count);
    femon_register_u64(reg, "bbssd.gc.pgs",   &ssd->mon_gc_pgs);
    femon_register_u64(reg, "bbssd.dsm.count", &ssd->mon_dsm_count);
    femon_register_u64(reg, "bbssd.dsm.pgs",  &ssd->mon_dsm_pgs);
    femon_register_u64(reg, "bbssd.host_rd.pgs",        &ssd->mon_host_rd_pgs);
    femon_register_u64(reg, "bbssd.host_rd.wait_ns",    &ssd->mon_host_rd_wait_ns);
    femon_register_u64(reg, "bbssd.host_rd.gcwait_pgs", &ssd->mon_host_rd_gcwait_pgs);
    femon_register_u64(reg, "bbssd.host_rd.gcwait_ns",  &ssd->mon_host_rd_gcwait_ns);
    femon_register_hist(reg, "bbssd.host_rd.wait_us",   &ssd->host_rd_wait_hist);
    femon_register_u64(reg, "bbssd.host_rd.small_pgs",        &ssd->mon_host_rd_small_pgs);
    femon_register_u64(reg, "bbssd.host_rd.small_wait_ns",    &ssd->mon_host_rd_small_wait_ns);
    femon_register_u64(reg, "bbssd.host_rd.small_gcwait_pgs", &ssd->mon_host_rd_small_gcwait_pgs);
    femon_register_hist(reg, "bbssd.host_rd.small_wait_us",   &ssd->host_rd_small_wait_hist);
    femon_register_u64(reg, "bbssd.gc.wall_ns",    &ssd->mon_gc_wall_ns);
    femon_register_u64(reg, "bbssd.gc.emu_max_ns", &ssd->mon_gc_emu_max_ns);
    femon_register_u64(reg, "bbssd.gc.emu_sum_ns", &ssd->mon_gc_emu_sum_ns);
    femon_register_hist(reg, "bbssd.gc.wall_us",   &ssd->gc_wall_hist);
    femon_register_hist(reg, "bbssd.gc.emu_max_us", &ssd->gc_emu_hist);
    femon_register_arr(reg, "bbssd.line.cnt_free_victim_full", FEMON_MT_U64,
                       1, d3, bb_fill_line_cnt, n);
    {
        uint32_t dl[1] = { (uint32_t)ssd->lm.tt_lines };
        uint32_t d6[1] = { 6 };
        femon_register_arr(reg, "bbssd.line.ipc", FEMON_MT_U64, 1, dl, bb_fill_line_ipc, n);
        femon_register_arr(reg, "bbssd.line.vpc", FEMON_MT_U64, 1, dl, bb_fill_line_vpc, n);
        femon_register_arr(reg, "bbssd.line.pos", FEMON_MT_U64, 1, dl, bb_fill_line_pos, n);
        femon_register_arr(reg, "bbssd.line.nsec", FEMON_MT_U64, 1, dl, bb_fill_line_nsec, n);   /* distinct 128 MiB host sections written into the line (not cleared on invalidation; reset when the line is erased) */
        femon_register_arr(reg, "bbssd.line.minlpn", FEMON_MT_U64, 1, dl, bb_fill_line_minlpn, n); /* lowest lpn in the line (alignment offset = minlpn % 32768) */
        /* [curline, ch, lun, pg, blk, pl] */
        femon_register_arr(reg, "bbssd.wp.pos", FEMON_MT_U64, 1, d6, bb_fill_wp, n);
    }

    n->femon_reg = reg;
}

/* bb <=> black-box */
static void bb_init(FemuCtrl *n, Error **errp)
{
    struct ssd *ssd = n->ssd = g_malloc0(sizeof(struct ssd));

    bb_init_ctrl_str(n);

    ssd->dataplane_started_ptr = &n->dataplane_started;
    ssd->ssdname = (char *)n->devname;
    femu_debug("Starting FEMU in Blackbox-SSD mode ...\n");
    ssd_init(n);
    bb_register_metrics(n);
}

static void bb_flip(FemuCtrl *n, NvmeCmd *cmd)
{
    struct ssd *ssd = n->ssd;
    int64_t cdw10 = le64_to_cpu(cmd->cdw10);

    switch (cdw10) {
    case FEMU_ENABLE_GC_DELAY:
        ssd->sp.enable_gc_delay = true;
        femu_log("%s,FEMU GC Delay Emulation [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_GC_DELAY:
        ssd->sp.enable_gc_delay = false;
        femu_log("%s,FEMU GC Delay Emulation [Disabled]!\n", n->devname);
        break;
    case FEMU_ENABLE_DELAY_EMU:
        ssd->sp.pg_rd_lat = NAND_READ_LATENCY;
        ssd->sp.pg_wr_lat = NAND_PROG_LATENCY;
        ssd->sp.blk_er_lat = NAND_ERASE_LATENCY;
        ssd->sp.ch_xfer_lat = 0;
        femu_log("%s,FEMU Delay Emulation [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_DELAY_EMU:
        ssd->sp.pg_rd_lat = 0;
        ssd->sp.pg_wr_lat = 0;
        ssd->sp.blk_er_lat = 0;
        ssd->sp.ch_xfer_lat = 0;
        femu_log("%s,FEMU Delay Emulation [Disabled]!\n", n->devname);
        break;
    case FEMU_RESET_ACCT: {
        /* counters are sharded per poller (see FemuPollerCtr); sum then reset */
        int64_t tt = 0, late = 0;
        if (n->poller_ctr) {
            for (uint32_t p = 1; p <= n->nr_pollers; p++) {
                tt += n->poller_ctr[p].nr_tt_ios;
                late += n->poller_ctr[p].nr_tt_late_ios;
                n->poller_ctr[p].nr_tt_ios = 0;
                n->poller_ctr[p].nr_tt_late_ios = 0;
            }
        }
        femu_log("%s,Reset tt_late_ios/tt_ios,%ld/%ld\n", n->devname, late, tt);
        break;
    }
    case FEMU_ENABLE_LOG:
        n->print_log = true;
        femu_log("%s,Log print [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_LOG:
        n->print_log = false;
        femu_log("%s,Log print [Disabled]!\n", n->devname);
        break;
    default:
        printf("FEMU:%s,Not implemented flip cmd (%lu)\n", n->devname, cdw10);
    }
}

static uint16_t bb_nvme_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                           NvmeRequest *req)
{
    return nvme_rw(n, ns, cmd, req);
}

static uint16_t bb_io_cmd(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                          NvmeRequest *req)
{
    switch (cmd->opcode) {
    case NVME_CMD_READ:
    case NVME_CMD_WRITE:
        return bb_nvme_rw(n, ns, cmd, req);
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

/* NVME_ADM_CMD_DEVTYPE (0xD0): device type + full geometry */
static uint16_t bb_id_dev(FemuCtrl *n, NvmeCmd *cmd)
{
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;
    uint64_t prp1 = le64_to_cpu(cmd->dptr.prp1);
    uint64_t prp2 = le64_to_cpu(cmd->dptr.prp2);
    femon_device_info_t info = { .type = FEMON_DEVICE_BBSSD };

    info.bbssd.secsz         = spp->secsz;
    info.bbssd.secs_per_pg   = spp->secs_per_pg;
    info.bbssd.pgs_per_blk   = spp->pgs_per_blk;
    info.bbssd.blks_per_pl   = spp->blks_per_pl;
    info.bbssd.pls_per_lun   = spp->pls_per_lun;
    info.bbssd.luns_per_ch   = spp->luns_per_ch;
    info.bbssd.nchs          = spp->nchs;
    info.bbssd.secs_per_blk  = spp->secs_per_blk;
    info.bbssd.secs_per_pl   = spp->secs_per_pl;
    info.bbssd.secs_per_lun  = spp->secs_per_lun;
    info.bbssd.secs_per_ch   = spp->secs_per_ch;
    info.bbssd.tt_secs       = spp->tt_secs;
    info.bbssd.pgs_per_pl    = spp->pgs_per_pl;
    info.bbssd.pgs_per_lun   = spp->pgs_per_lun;
    info.bbssd.pgs_per_ch    = spp->pgs_per_ch;
    info.bbssd.tt_pgs        = spp->tt_pgs;
    info.bbssd.blks_per_lun  = spp->blks_per_lun;
    info.bbssd.blks_per_ch   = spp->blks_per_ch;
    info.bbssd.tt_blks       = spp->tt_blks;
    info.bbssd.secs_per_line = spp->secs_per_line;
    info.bbssd.pgs_per_line  = spp->pgs_per_line;
    info.bbssd.blks_per_line = spp->blks_per_line;
    info.bbssd.tt_lines      = spp->tt_lines;
    info.bbssd.pls_per_ch    = spp->pls_per_ch;
    info.bbssd.tt_pls        = spp->tt_pls;
    info.bbssd.tt_luns       = spp->tt_luns;

    return dma_read_prp(n, (uint8_t *)&info, sizeof(info), prp1, prp2);
}

static uint16_t bb_admin_cmd(FemuCtrl *n, NvmeCmd *cmd)
{
    switch (cmd->opcode) {
    case NVME_ADM_CMD_FEMU_FLIP:
        bb_flip(n, cmd);
        return NVME_SUCCESS;
    case NVME_ADM_CMD_DEVTYPE:
        return bb_id_dev(n, cmd);
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

int nvme_register_bbssd(FemuCtrl *n)
{
    n->ext_ops = (FemuExtCtrlOps) {
        .state            = NULL,
        .init             = bb_init,
        .exit             = NULL,
        .rw_check_req     = NULL,
        .admin_cmd        = bb_admin_cmd,
        .io_cmd           = bb_io_cmd,
        .get_log          = NULL,
    };

    return 0;
}

