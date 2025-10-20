#include "../nvme.h"
#include "./ftl.h"

static void bb_init_ctrl_str(FemuCtrl *n)
{
    static int fsid_vbb = 0;
    const char *vbbssd_mn = "FEMU BlackBox-SSD Controller";
    const char *vbbssd_sn = "vSSD";

    nvme_set_ctrl_name(n, vbbssd_mn, vbbssd_sn, &fsid_vbb);
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
    case FEMU_RESET_ACCT:
        n->nr_tt_ios = 0;
        n->nr_tt_late_ios = 0;
        femu_log("%s,Reset tt_late_ios/tt_ios,%lu/%lu\n", n->devname,
                n->nr_tt_late_ios, n->nr_tt_ios);
        break;
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

/* Calculate required buffer size for monitoring data */
static size_t bb_calc_monitor_buffer_size(struct ssd *ssd)
{
    return sizeof(bb_monitor_header) + 
           ssd->lm.tt_lines * sizeof(bb_line_info);
}

static uint16_t bb_id_dev(FemuCtrl *n, NvmeCmd *cmd)
{
    uint64_t prp1 = le64_to_cpu(cmd->dptr.prp1);
    uint64_t prp2 = le64_to_cpu(cmd->dptr.prp2);
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;
    nvme_device_info dev_data;

    memset(&dev_data, 0, sizeof(nvme_device_info));

    dev_data.type = 0;

    dev_data.bbssd.secsz = spp->secsz;
    dev_data.bbssd.secs_per_pg = spp->secs_per_pg;
    dev_data.bbssd.pgs_per_blk = spp->pgs_per_blk;
    dev_data.bbssd.blks_per_pl = spp->blks_per_pl;
    dev_data.bbssd.pls_per_lun = spp->pls_per_lun;
    dev_data.bbssd.luns_per_ch = spp->luns_per_ch;
    dev_data.bbssd.nchs = spp->nchs;
    dev_data.bbssd.secs_per_blk = spp->secs_per_blk;
    dev_data.bbssd.secs_per_pl = spp->secs_per_pl;
    dev_data.bbssd.secs_per_lun = spp->secs_per_lun;
    dev_data.bbssd.secs_per_ch = spp->secs_per_ch;
    dev_data.bbssd.tt_secs = spp->tt_secs;
    dev_data.bbssd.pgs_per_pl = spp->pgs_per_pl;
    dev_data.bbssd.pgs_per_lun = spp->pgs_per_lun;
    dev_data.bbssd.pgs_per_ch = spp->pgs_per_ch;
    dev_data.bbssd.tt_pgs = spp->tt_pgs;
    dev_data.bbssd.blks_per_lun = spp->blks_per_lun;
    dev_data.bbssd.blks_per_ch = spp->blks_per_ch;
    dev_data.bbssd.tt_blks = spp->tt_blks;
    dev_data.bbssd.secs_per_line = spp->secs_per_line;
    dev_data.bbssd.pgs_per_line = spp->pgs_per_line;
    dev_data.bbssd.blks_per_line = spp->blks_per_line;
    dev_data.bbssd.tt_lines = spp->tt_lines;
    dev_data.bbssd.pls_per_ch = spp->pls_per_ch;
    dev_data.bbssd.tt_pls = spp->tt_pls;
    dev_data.bbssd.tt_luns = spp->tt_luns;
    
    return dma_read_prp(n, (uint8_t *)&dev_data, sizeof(nvme_device_info), prp1, prp2);
}

/* Fill write pointer information */
static void bb_fill_wp_info(struct write_pointer *wpp, bb_write_pointer_info *wp_info)
{
    wp_info->curline_id = wpp->curline ? wpp->curline->id : -1;
    wp_info->ch = wpp->ch;
    wp_info->lun = wpp->lun;
    wp_info->pg = wpp->pg;
    wp_info->blk = wpp->blk;
    wp_info->pl = wpp->pl;
}

/* Fill header with SSD statistics */
static void bb_fill_monitor_header(struct ssd *ssd, bb_monitor_header *header)
{
    /* Write pointer information */
    bb_fill_wp_info(&ssd->wp, &header->wp_info);
    
    /* GC statistics */
    header->gc_count = ssd->sp.gc_count;
    header->gc_pgs = ssd->sp.gc_pgs;
    
    /* Line management counters */
    header->tt_lines = ssd->lm.tt_lines;
    header->free_line_cnt = ssd->lm.free_line_cnt;
    header->victim_line_cnt = ssd->lm.victim_line_cnt;
    header->full_line_cnt = ssd->lm.full_line_cnt;
}

/* Copy line information to bb_line_info structure */
static void bb_copy_line_info(struct line *line, bb_line_info *info)
{
    info->id = line->id;
    info->ipc = line->ipc;
    info->vpc = line->vpc;
    info->pos = line->pos;
}

/* Fill line information array */
static void bb_fill_line_info_array(struct line_mgmt *lm, bb_line_info *line_infos)
{
    for (int i = 0; i < lm->tt_lines; i++) {
        bb_copy_line_info(&lm->lines[i], &line_infos[i]);
    }
}

/* Fill buffer with monitoring data */
static void bb_fill_monitor_buffer(struct ssd *ssd, void *buf)
{
    /* Fill header section */
    bb_monitor_header *header = (bb_monitor_header *)buf;
    bb_fill_monitor_header(ssd, header);
    
    /* Fill line info array section */
    bb_line_info *line_infos = (bb_line_info *)((uint8_t *)buf + sizeof(bb_monitor_header));
    bb_fill_line_info_array(&ssd->lm, line_infos);
}

static uint16_t bb_mon(FemuCtrl *n, NvmeCmd *cmd)
{
    uint64_t prp1 = le64_to_cpu(cmd->dptr.prp1);
    uint64_t prp2 = le64_to_cpu(cmd->dptr.prp2);
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t requested_size = (dw10 + 1) << 2;
    
    struct ssd *ssd = n->ssd;
    
    /* Calculate required buffer size */
    size_t required_size = bb_calc_monitor_buffer_size(ssd);
    
    /* Validate requested size */
    if (requested_size < required_size) {
        femu_err("requested size (%u) < required size (%lu)\n", 
                requested_size, required_size);
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    
    /* Allocate buffer */
    void *buf = g_malloc0(requested_size);
    if (!buf) {
        femu_err("bb_mon: ERROR - Memory allocation failed\n");
        return NVME_INTERNAL_DEV_ERROR;
    }
    
    /* Fill buffer with monitoring data */
    bb_fill_monitor_buffer(ssd, buf);
    
    /* Transfer data via DMA */
    uint16_t status = dma_read_prp(n, (uint8_t *)buf, requested_size, prp1, prp2);
    
    /* Clean up */
    g_free(buf);
    
    return status;
}

static uint16_t bb_admin_cmd(FemuCtrl *n, NvmeCmd *cmd)
{
    switch (cmd->opcode) {
    case NVME_ADM_CMD_DEVTYPE:
        return bb_id_dev(n, cmd);
    case NVME_ADM_CMD_MON_BBSSD:
        return bb_mon(n, cmd);
    case NVME_ADM_CMD_FEMU_FLIP:
        bb_flip(n, cmd);
        return NVME_SUCCESS;
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

