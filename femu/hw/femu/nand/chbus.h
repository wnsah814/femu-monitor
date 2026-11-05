#ifndef __FEMU_CHBUS_H
#define __FEMU_CHBUS_H

/*
 * Per-channel NAND bus timeline shared by the bbssd and ZNS timing models.
 *
 * Two kinds of phase use one bus. Phases that happen when the op reaches
 * them (a program's data-in) queue FIFO behind `avail`. A read's data-out
 * happens only after the array has finished (tR later), so it is booked as a
 * window [array done, +xfer) and does not advance `avail`: the bus stays
 * free for other dies until the window opens. Both kinds avoid every booked
 * window, and a zero-length phase touches nothing.
 */

#include <stdint.h>
#include <stdbool.h>

#define FEMU_CHBUS_RES_MAX 32

struct femu_chbus_res {
    uint64_t start;
    uint64_t end;
};

struct femu_chbus {
    uint64_t avail;                                  /* busy until (immediate phases) */
    struct femu_chbus_res r[FEMU_CHBUS_RES_MAX];     /* booked data-out windows, by start */
    int n;
};

static inline uint64_t chbus_max(uint64_t a, uint64_t b) { return a > b ? a : b; }

static inline void chbus_prune(struct femu_chbus *b, uint64_t now)
{
    int i, j = 0;
    for (i = 0; i < b->n; i++) {
        if (b->r[i].end > now) {
            b->r[j++] = b->r[i];
        }
    }
    b->n = j;
}

/* earliest s >= start such that [s, s + len) overlaps no booked window */
static inline uint64_t chbus_fit(const struct femu_chbus *b, uint64_t start, uint64_t len)
{
    int i;
    for (i = 0; i < b->n; i++) {
        if (b->r[i].end <= start) {
            continue;
        }
        if (b->r[i].start >= start + len) {
            break;
        }
        start = b->r[i].end;
    }
    return start;
}

static inline bool chbus_book(struct femu_chbus *b, uint64_t start, uint64_t end)
{
    int i;
    if (b->n == FEMU_CHBUS_RES_MAX) {
        return false;
    }
    for (i = b->n; i > 0 && b->r[i - 1].start > start; i--) {
        b->r[i] = b->r[i - 1];
    }
    b->r[i].start = start;
    b->r[i].end = end;
    b->n++;
    return true;
}

/* a phase that happens now (data-in): FIFO on the bus, around booked windows.
 * Returns the time the phase ends. `now` is the op's start time (for pruning). */
static inline uint64_t chbus_now(struct femu_chbus *b, uint64_t now,
                                 uint64_t earliest, uint64_t len)
{
    uint64_t start;
    if (!len) {
        return earliest;
    }
    chbus_prune(b, now);
    start = chbus_max(earliest, b->avail);
    start = chbus_fit(b, start, len);
    b->avail = start + len;
    return b->avail;
}

/* a phase that happens later (a read's data-out): booked, `avail` untouched.
 * Returns the time the transfer ends. */
static inline uint64_t chbus_later(struct femu_chbus *b, uint64_t now,
                                   uint64_t earliest, uint64_t len)
{
    uint64_t start;
    if (!len) {
        return earliest;
    }
    chbus_prune(b, now);
    start = chbus_max(earliest, b->avail);
    start = chbus_fit(b, start, len);
    if (chbus_book(b, start, start + len)) {
        return start + len;
    }
    b->avail = start + len;   /* no room to book: hold the bus from now */
    return b->avail;
}

#endif
