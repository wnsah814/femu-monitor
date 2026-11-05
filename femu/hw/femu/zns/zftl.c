#include "zns.h"

//#define FEMU_DEBUG_ZFTL

static void *ftl_thread(void *arg);

static inline struct ppa get_maptbl_ent(struct zns_ssd *zns, uint64_t lpn)
{
    return zns->maptbl[lpn];
}

static inline void set_maptbl_ent(struct zns_ssd *zns, uint64_t lpn, struct ppa *ppa)
{
    ftl_assert(lpn < zns->l2p_sz);
    zns->maptbl[lpn] = *ppa;
}

void zftl_init(FemuCtrl *n)
{
    struct zns_ssd *ssd = n->zns;

    qemu_thread_create(&ssd->ftl_thread, "FEMU-FTL-Thread", ftl_thread, n,
                       QEMU_THREAD_JOINABLE);
}

static inline struct zns_ch *get_ch(struct zns_ssd *zns, struct ppa *ppa)
{
    return &(zns->ch[ppa->g.ch]);
}

static inline struct zns_fc *get_fc(struct zns_ssd *zns, struct ppa *ppa)
{
    struct zns_ch *ch = get_ch(zns, ppa);
    return &(ch->fc[ppa->g.fc]);
}

static inline struct zns_plane *get_plane(struct zns_ssd *zns, struct ppa *ppa)
{
    struct zns_fc *fc = get_fc(zns, ppa);
    return &(fc->plane[ppa->g.pl]);
}

static inline struct zns_blk *get_blk(struct zns_ssd *zns, struct ppa *ppa)
{
    struct zns_plane *pl = get_plane(zns, ppa);
    return &(pl->blk[ppa->g.blk]);
}

static inline void check_addr(int a, int max)
{
   assert(a >= 0 && a < max);
}

static void zns_advance_write_pointer(struct zns_ssd *zns)
{
    struct write_pointer *wpp = &zns->wp;

    check_addr(wpp->ch, zns->num_ch);
    wpp->ch++;
    if (wpp->ch == zns->num_ch) {
        wpp->ch = 0;
        check_addr(wpp->lun, zns->num_lun);
        wpp->lun++;
        /* in this case, we should go to next lun */
        if (wpp->lun == zns->num_lun) {
            wpp->lun = 0;
        }
    }
}

static uint64_t zns_advance_status(struct zns_ssd *zns, struct ppa *ppa,struct nand_cmd *ncmd)
{
    int c = ncmd->cmd;

    uint64_t nand_stime;
    uint64_t req_stime = (ncmd->stime == 0) ? \
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : ncmd->stime;

    //plane level parallism
    struct zns_plane *pl = get_plane(zns, ppa);

    uint64_t lat = 0;
    int nand_type = get_blk(zns,ppa)->nand_type;

    uint64_t read_delay = zns->timing.pg_rd_lat[nand_type];
    uint64_t write_delay = zns->timing.pg_wr_lat[nand_type];
    uint64_t erase_delay = zns->timing.blk_er_lat[nand_type];

    /* femonitor: per-plane op/conflict accounting. Unlike the ConfZNS++
     * implementation, erase ops are counted here too, so reset-induced
     * plane occupancy is attributable instead of surfacing only as
     * conflicts of subsequent reads/writes. */
    pl->mon_io_count++;
    if (pl->next_plane_avail_time > req_stime) {
        pl->mon_conflict_count++;
                pl->mon_wait_ns += (zns->timing.pe_suspend && c == NAND_READ &&
                            pl->pe_end > req_stime) ?
                           ((pl->rd_end > req_stime) ? pl->rd_end - req_stime
                                                     : zns->timing.tsusp_ns) :
                           pl->next_plane_avail_time - req_stime;
    }
    struct zns_ch *ch = &zns->ch[ppa->g.ch];
    uint64_t xfer = zns->timing.ch_xfer_lat;
    uint64_t t;
    ch->mon_xfer_count++;

    switch (c) {
    case NAND_READ:
        /* array read on the plane, then data-out booked on the channel for
         * the window in which it happens (nand/chbus.h) */
        nand_stime = (pl->next_plane_avail_time < req_stime) ? req_stime : \
                     pl->next_plane_avail_time;
                if (zns->timing.pe_suspend && nand_stime > req_stime &&
            pl->pe_end > req_stime) {
            /* NAND P/E suspend (upstream 6a5c498a8): only a program or erase
             * is suspended; a read joins an open suspension behind the reads
             * in it with no second overhead, and a plane busy with reads
             * only is queued behind (the branch below) */
            uint64_t shift;
            if (pl->rd_end > req_stime) {
                nand_stime = pl->rd_end;
                shift = read_delay;
            } else {
                nand_stime = req_stime + zns->timing.tsusp_ns;
                shift = read_delay + zns->timing.tsusp_ns;
            }
            pl->pe_end += shift;
            pl->next_plane_avail_time += shift;
            pl->rd_end = nand_stime + read_delay;
            t = chbus_later(&ch->bus, req_stime, nand_stime + read_delay, xfer);
            if (xfer && t > nand_stime + read_delay + xfer) {
                ch->mon_conflict_count++;
                ch->mon_wait_ns += t - (nand_stime + read_delay + xfer);
            }
            lat = t - req_stime;
            break;
        }
                pl->next_plane_avail_time = nand_stime + read_delay;
        if (pl->rd_end < pl->next_plane_avail_time) {
            pl->rd_end = pl->next_plane_avail_time;
        }
        t = chbus_later(&ch->bus, req_stime, pl->next_plane_avail_time, xfer);
        if (xfer && t > pl->next_plane_avail_time + xfer) {
            ch->mon_conflict_count++;
            ch->mon_wait_ns += t - (pl->next_plane_avail_time + xfer);
        }
        lat = t - req_stime;
	    break;

    case NAND_WRITE:
        /* data-in over the channel first (FIFO, backfilling gaps before booked
         * read transfers), then the program on the plane */
        t = chbus_now(&ch->bus, req_stime, req_stime, xfer);
        if (xfer && t > req_stime + xfer) {
            ch->mon_conflict_count++;
            ch->mon_wait_ns += t - (req_stime + xfer);
        }
	    nand_stime = (pl->next_plane_avail_time < t) ? t : \
		            pl->next_plane_avail_time;
	    	    pl->next_plane_avail_time = nand_stime + write_delay;
	    if (pl->pe_end < pl->next_plane_avail_time) {
	        pl->pe_end = pl->next_plane_avail_time;
	    }
	    lat = pl->next_plane_avail_time - req_stime;
	    break;

    case NAND_ERASE:
        nand_stime = (pl->next_plane_avail_time < req_stime) ? req_stime : \
                        pl->next_plane_avail_time;
                pl->next_plane_avail_time = nand_stime + erase_delay;
        if (pl->pe_end < pl->next_plane_avail_time) {
            pl->pe_end = pl->next_plane_avail_time;
        }
        lat = pl->next_plane_avail_time - req_stime;
        break;

    default:
        /* To silent warnings */
        ;
    }

    return lat;
}

static inline bool valid_ppa(struct zns_ssd *zns, struct ppa *ppa)
{
    int ch = ppa->g.ch;
    int lun = ppa->g.fc;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sub_pg = ppa->g.spg;

    if (ch >= 0 && ch < zns->num_ch && lun >= 0 && lun < zns->num_lun && pl >=
        0 && pl < zns->num_plane && blk >= 0 && blk < zns->num_blk && pg>=0 && pg < zns->num_page && sub_pg >= 0 && sub_pg < ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE)
        return true;

    return false;
}

static inline bool mapped_ppa(struct ppa *ppa)
{
    return !(ppa->ppa == UNMAPPED_PPA);
}

static struct ppa get_new_page(struct zns_ssd *zns, uint64_t blk)
{
    struct write_pointer *wpp = &zns->wp;
    struct ppa ppa;
    ppa.ppa = 0;
    ppa.g.ch = wpp->ch;
    ppa.g.fc = wpp->lun;
    ppa.g.blk = blk;
    ppa.g.V = 1; //not padding page
    if(!valid_ppa(zns,&ppa))
    {
        ftl_err("[Misao] invalid ppa: ch %u lun %u pl %u blk %u pg %u subpg  %u \n",ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg);
        ppa.ppa = UNMAPPED_PPA;
    }
    return ppa;
}

/*
 * Look up the write cache bound to a specific zone. Caches are keyed by the
 * request's own zone (one buffer per open zone, like real ZNS devices —
 * see ZNSCtrlParams.zns_num_wc), not by the last zone the device touched:
 * a single global key made any >3-stream workload evict on every request.
 */
static int zns_get_wcidx(struct zns_ssd* zns, uint64_t sblk)
{
    int i;
    for(i = 0;i < zns->cache.num_wc;i++)
    {
        if(zns->cache.write_cache[i].sblk==sblk)
        {
            return i;
        }
    }
    return -1;
}

static void zns_invalidate_zone_cache(struct zns_ssd *zns, uint32_t zone_idx)
{
    int i;

    for (i = 0; i < zns->cache.num_wc; i++) {
        if (zns->cache.write_cache[i].sblk == zone_idx) {
            #ifdef FEMU_DEBUG_ZFTL
            uint64_t discarded_entries = zns->cache.write_cache[i].used;
            #endif

            zns->cache.write_cache[i].used = 0;
            zns->cache.write_cache[i].sblk = INVALID_SBLK;

            ftl_debug("Invalidated write_cache[%d] for zone %u (%lu entries discarded)\n",
                     i, zone_idx, discarded_entries);
            return;
        }
    }
}

static void zns_invalidate_zone_mappings(struct zns_ssd *zns, uint32_t zone_idx,
                                         uint64_t zone_size_lbas, uint64_t lbasz)
{
    uint64_t zone_start_lba = zone_idx * zone_size_lbas;
    uint64_t zone_end_lba = (zone_idx + 1) * zone_size_lbas;
    uint64_t secs_per_pg = LOGICAL_PAGE_SIZE / lbasz;
    uint64_t start_lpn = zone_start_lba / secs_per_pg;
    uint64_t end_lpn = zone_end_lba / secs_per_pg;
    uint64_t lpn;
    uint64_t invalidated_count = 0;

    for (lpn = start_lpn; lpn < end_lpn && lpn < zns->l2p_sz; lpn++) {
        if (zns->maptbl[lpn].ppa != UNMAPPED_PPA) {
            zns->maptbl[lpn].ppa = UNMAPPED_PPA;
            invalidated_count++;
        }
    }

    ftl_debug("Invalidated %lu LPN mappings for zone %u (LPN %lu-%lu)\n",
             invalidated_count, zone_idx, start_lpn, end_lpn - 1);
}

static void zns_reset_block_state(struct zns_ssd *zns, uint32_t zone_idx)
{
    int ch, lun, pl;
    struct ppa ppa;
    struct zns_blk *blk;

    for (ch = 0; ch < zns->num_ch; ch++) {
        for (lun = 0; lun < zns->num_lun; lun++) {
            for (pl = 0; pl < zns->num_plane; pl++) {
                ppa.ppa = 0; /* clear rsv/unused bits before field writes */
                ppa.g.ch = ch;
                ppa.g.fc = lun;
                ppa.g.pl = pl;
                ppa.g.blk = zone_idx;
                ppa.g.pg = 0;
                ppa.g.spg = 0;

                blk = get_blk(zns, &ppa);
                blk->page_wp = 0;
            }
        }
    }

    ftl_debug("Reset block state for zone %u (all page_wp = 0)\n", zone_idx);
}

uint64_t zns_zone_reset(struct zns_ssd *zns, uint32_t zone_idx,
                        uint64_t zone_size_lbas, uint64_t lbasz, uint64_t stime)
{
    int ch, lun, pl;
    struct ppa ppa;
    struct nand_cmd erase_cmd;
    uint64_t sublat, maxlat = 0;
    uint64_t total_blocks_erased = 0;

    ftl_debug("=== Zone Reset Started for Zone %u ===\n", zone_idx);

    /* Step 1: Invalidate write cache (instant) */
    zns_invalidate_zone_cache(zns, zone_idx);

    /* Step 2: Invalidate L2P mappings (instant) */
    zns_invalidate_zone_mappings(zns, zone_idx, zone_size_lbas, lbasz);

    /* Step 3: Reset block state (instant) */
    zns_reset_block_state(zns, zone_idx);

    /* Step 4: Simulate physical erase: one erase per plane, all planes in parallel */
    erase_cmd.type = USER_IO;
    erase_cmd.cmd = NAND_ERASE;
    erase_cmd.stime = stime;

    for (ch = 0; ch < zns->num_ch; ch++) {
        for (lun = 0; lun < zns->num_lun; lun++) {
            for (pl = 0; pl < zns->num_plane; pl++) {
                ppa.ppa = 0;
                ppa.g.ch = ch;
                ppa.g.fc = lun;
                ppa.g.pl = pl;
                ppa.g.blk = zone_idx;
                ppa.g.pg = 0;
                ppa.g.spg = 0;

                sublat = zns_advance_status(zns, &ppa, &erase_cmd);
                maxlat = (sublat > maxlat) ? sublat : maxlat;
                total_blocks_erased++;
            }
        }
    }

    ftl_debug("Zone %u reset complete: erased %lu blocks "
              "(%d ch * %d lun * %d planes)\n",
              zone_idx, total_blocks_erased, (int)zns->num_ch,
              (int)zns->num_lun, (int)zns->num_plane);
    ftl_debug("Maximum erase latency: %lu ns (%.2f ms)\n", maxlat, maxlat / 1000000.0);
    ftl_debug("=== Zone Reset Finished ===\n\n");

    femon_hist_record(&zns->reset_lat_hist, maxlat);
    return maxlat;
}

static uint64_t zns_read(struct zns_ssd *zns, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    uint32_t nlb = req->nlb;
    uint64_t secs_per_pg = LOGICAL_PAGE_SIZE/zns->lbasz;
    uint64_t start_lpn = lba / secs_per_pg;
    uint64_t end_lpn = (lba + nlb - 1) / secs_per_pg;
    //int wcidx = zns_get_wcidx(zns);
    struct ppa ppa;
    uint64_t lpn;
    uint64_t sublat, maxlat = 0;

    /* normal IO read path */
    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        ppa = get_maptbl_ent(zns, lpn);
        zns->mon_rd_lpns++;
        if (!mapped_ppa(&ppa) || !valid_ppa(zns, &ppa)) {
            zns->mon_rd_skip_lpns++;
            if (mapped_ppa(&ppa)) {
                zns->mon_rd_skip_oor++;
            } else {
                /* why is it unmapped: still in a write cache, or never programmed */
                bool cached = false;
                for (int w = 0; w < zns->cache.num_wc && !cached; w++) {
                    struct zns_write_cache *wc = &zns->cache.write_cache[w];
                    for (uint64_t k = 0; k < wc->used; k++) {
                        if (wc->lpns[k] == lpn) { cached = true; break; }
                    }
                }
                if (cached) zns->mon_rd_skip_incache++; else zns->mon_rd_skip_nomap++;
            }
            continue;
        }

        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;

        sublat = zns_advance_status(zns, &ppa, &srd);
        ftl_debug("[R] lpn:\t%lu\t<--ch:\t%u\tlun:\t%u\tpl:\t%u\tblk:\t%u\tpg:\t%u\tsubpg:\t%u\tlat\t%lu\n",lpn,ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg,sublat);
        maxlat = (sublat > maxlat) ? sublat : maxlat;
    }

    return maxlat;
}

static uint64_t zns_wc_flush(struct zns_ssd* zns, int wcidx, int type,uint64_t stime)
{
    int i,j,p,subpage;
    struct ppa ppa;
    struct ppa oldppa;
    uint64_t lpn;
    int flash_type = zns->flash_type;
    uint64_t sublat = 0, maxlat = 0;

    if (zns->cache.write_cache[wcidx].used < zns->cache.write_cache[wcidx].cap) {
        zns->mon_wc_partial_flush++;
    }
    i = 0;
    while(i < zns->cache.write_cache[wcidx].used)
    {
        for(p = 0;p<zns->num_plane;p++){
            /*
             * Fixed mode: a partial cache (evicted before its stripe filled)
             * programs only the pages that hold data. Upstream walked the
             * whole stripe and burned 2 planes x 3 pages for a single LPN.
             */
            if (!zns->wc_upstream && i >= zns->cache.write_cache[wcidx].used) {
                break;
            }
            /* new write — program into the block backing this cache's zone
             * (upstream global-key mode: the zone the device touched last) */
            ppa = get_new_page(zns, zns->wc_upstream ? zns->active_zone
                                    : zns->cache.write_cache[wcidx].sblk);
            ppa.g.pl = p;
            for(j = 0; j < flash_type ;j++)
            {
                if (!zns->wc_upstream && i >= zns->cache.write_cache[wcidx].used) {
                    break;
                }
                ppa.g.pg = get_blk(zns,&ppa)->page_wp;
                get_blk(zns,&ppa)->page_wp++;
                if (i + ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE > zns->cache.write_cache[wcidx].used) {
                    zns->mon_wc_partial_pages++;
                }
                for(subpage = 0;subpage < ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE;subpage++)
                {
                    if(i+subpage >= zns->cache.write_cache[wcidx].used)
                    {
                        //No need to write an invalid page
                        break;
                    }
                    if (ppa.g.pg >= zns->num_page) {
                        zns->mon_wc_oor_lpns++;
                    }
                    lpn = zns->cache.write_cache[wcidx].lpns[i+subpage];
                    oldppa = get_maptbl_ent(zns, lpn);
                    if (mapped_ppa(&oldppa)) {
                        /* FIXME: Misao: update old page information*/
                    }
                    ppa.g.spg = subpage;
                    /* update maptbl */
                    set_maptbl_ent(zns, lpn, &ppa);
                    // ftl_debug("[F] lpn:\t%lu\t-->ch:\t%u\tlun:\t%u\tpl:\t%u\tblk:\t%u\tpg:\t%u\tsubpg:\t%u\tlat\t%lu\n",lpn,ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg,sublat);
                }
                i+=ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE;
            }
            //FIXME Misao: identify padding page
            if(ppa.g.V)
            {
                struct nand_cmd swr;
                swr.type = type;
                swr.cmd = NAND_WRITE;
                swr.stime = stime;
                /* get latency statistics */
                sublat = zns_advance_status(zns, &ppa, &swr);
                maxlat = (sublat > maxlat) ? sublat : maxlat;
                zns->mon_wc_flush_pages++;
            }
        }
        /* need to advance the write pointer here */
        zns_advance_write_pointer(zns);
    }
    zns->cache.write_cache[wcidx].used = 0;
    return maxlat;
}

static uint64_t zns_write(struct zns_ssd *zns, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    uint32_t nlb = req->nlb;
    uint64_t secs_per_pg = LOGICAL_PAGE_SIZE/zns->lbasz;
    uint64_t start_lpn = lba / secs_per_pg;
    uint64_t end_lpn = (lba + nlb - 1) / secs_per_pg;
    uint64_t lpn;
    uint64_t sublat = 0, maxlat = 0;
    int i;
    uint64_t now = (req->stime == 0) ?
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : req->stime;

    uint64_t req_zone = req->slba / zns->zone_size_lbas;
    if (zns->wc_upstream) {
        req_zone = zns->active_zone;   /* upstream #145 lookup key */
    }
    int wcidx = zns_get_wcidx(zns, req_zone);

    if (wcidx != -1) {
        zns->mon_wc_hit++;
    } else {
        zns->mon_wc_miss++;
    }

    if(wcidx==-1)
    {
        /*
         * No cache bound to this zone. Take a free one; only when every
         * cache is held by another zone (more concurrent write streams
         * than zns_num_wc — beyond the open-zone budget on real devices)
         * evict the fullest, and the evicting request absorbs the flush.
         */
        /*
         * Upstream scanned from index 1 with cache 0 as the initial
         * candidate, so an empty cache 0 was never taken (num_wc-1 caches
         * effectively served). Fixed mode scans every cache.
         */
        wcidx = 0;
        uint64_t t_used = zns->cache.write_cache[wcidx].used;
        for(i = zns->wc_upstream ? 1 : 0;i < zns->cache.num_wc;i++)
        {
            if(zns->cache.write_cache[i].used==0)
            {
                t_used = 0;
                wcidx = i; //free wc！
                break;
            }
            if(zns->cache.write_cache[i].used > t_used)
            {
                t_used = zns->cache.write_cache[i].used;
                wcidx = i;
            }
        }
        if(t_used) {
            zns->mon_wc_evict++;
            zns->mon_wc_flush++;
            sublat = zns_wc_flush(zns,wcidx,USER_IO,now);
            maxlat = (sublat > maxlat) ? sublat : maxlat;
            sublat = 0;
        }
        zns->cache.write_cache[wcidx].sblk = req_zone;
    }

    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        if(zns->cache.write_cache[wcidx].used==zns->cache.write_cache[wcidx].cap)
        {
            ftl_debug("[W] flush wc %d (%u/%u)\n",wcidx,(int)zns->cache.write_cache[wcidx].used,(int)zns->cache.write_cache[wcidx].cap);
            zns->mon_wc_flush++;
            sublat = zns_wc_flush(zns,wcidx,USER_IO,now);
            ftl_debug("[W] flush lat: %u\n", (int)sublat);
            maxlat = (sublat > maxlat) ? sublat : maxlat;
            sublat = 0;
        }
        zns->cache.write_cache[wcidx].lpns[zns->cache.write_cache[wcidx].used++]=lpn;
        sublat += SRAM_WRITE_LATENCY_NS; //Simplified timing emulation
        maxlat = (sublat > maxlat) ? sublat : maxlat;
        ftl_debug("[W] lpn:\t%lu\t-->wc cache:%u, used:%u\n",lpn,(int)wcidx,(int)zns->cache.write_cache[wcidx].used);
    }
    /*
     * No write cache (zns_wc_bypass): program this request's pages now and
     * charge the request, the way bbssd charges a write its program time.
     * The cache entry is used only to group LPNs into physical pages.
     */
    if (zns->wc_bypass && zns->cache.write_cache[wcidx].used) {
        zns->mon_wc_flush++;
        sublat = zns_wc_flush(zns, wcidx, USER_IO, now);
        maxlat = (sublat > maxlat) ? sublat : maxlat;
    }
    femon_hist_record(&zns->write_lat_hist, maxlat);
    return maxlat;
}

static void *ftl_thread(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct zns_ssd *zns = n->zns;
    NvmeRequest *req = NULL;
    uint64_t lat = 0;
    int rc;
    int i;

    while (!*(zns->dataplane_started_ptr)) {
        usleep(100000);
    }

    /* FIXME: not safe, to handle ->to_ftl and ->to_poller gracefully */
    zns->to_ftl = n->to_ftl;
    zns->to_poller = n->to_poller;

    while (1) {
        for (i = 1; i <= n->nr_pollers; i++) {
            if (!zns->to_ftl[i] || !femu_ring_count(zns->to_ftl[i]))
                continue;

            rc = femu_ring_dequeue(zns->to_ftl[i], (void *)&req, 1);
            if (rc != 1) {
                printf("FEMU: FTL to_ftl dequeue failed\n");
            }

            ftl_assert(req);
            if (req->status != NVME_SUCCESS) {
                /* Failed at the NVMe layer (e.g. zone-write check): complete
                 * immediately with the error status. Processing it through
                 * zns_write/zns_read with an invalid LBA wedges the FTL
                 * thread and starves the SQ req_list (guest-visible as a
                 * controller timeout, host-visible as a poller segfault). */
                lat = 0;
            } else
            switch (req->cmd.opcode) {
                // Fix bug: zone append not respecting configured delay
                case NVME_CMD_ZONE_APPEND:
                    /* Fall through */
                case NVME_CMD_WRITE:
                    lat = zns_write(zns, req);
                    break;
                case NVME_CMD_READ:
                    lat = zns_read(zns, req);
                    break;
                case NVME_CMD_DSM:
                    lat = 0;
                    break;
                default:
                    //ftl_err("FTL received unkown request type, ERROR\n");
                    ;
            }

            req->reqlat = lat;
            req->expire_time += lat;

            rc = femu_ring_enqueue(zns->to_poller[i], (void *)&req, 1);
            if (rc != 1) {
                ftl_err("FTL to_poller enqueue failed\n");
            }

        }
    }

    return NULL;
}
