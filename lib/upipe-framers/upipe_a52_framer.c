/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module building frames from a ATSC A/52:2012 stream
 * This framer supports A/52:2012 and A/52:2012 Annex E streams.
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-framers/upipe_a52_framer.h>

#include "upipe_framers_common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/atsc/a52.h>

/** @internal @This is the private context of an a52f pipe. */
struct upipe_a52f {
    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** attributes in the sequence header */
    struct uref *flow_def_attr;

    /* sync parsing stuff */
    /** number of octets in a frame */
    size_t frame_size;
    /** number of octets in a frame with padding enabled */
    size_t frame_size_padding;
    /** number of samples in a frame */
    size_t samples;
    /** number of samples per second */
    size_t samplerate;
    /** number of channels */
    uint8_t channels;
    /** octet rate */
    uint64_t octetrate;
    /** residue of the duration in 27 MHz units */
    uint64_t duration_residue;
    /** true we have had a discontinuity recently */
    bool got_discontinuity;
    /** sync header */
    uint8_t sync_header[6]; // FIXME

    /* octet stream stuff */
    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct uchain urefs;

    /* octet stream parser stuff */
    /** current size of next frame (in next_uref) */
    ssize_t next_frame_size;
    /** pseudo-packet containing date information for the next picture */
    struct uref au_uref_s;
    /** delay due to the ADTS BS */
    int64_t bs_delay;
    /** true if we have thrown the sync_acquired event (that means we found a
     * sequence header) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static void upipe_a52f_promote_uref(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_a52f, upipe, UPIPE_A52F_SIGNATURE)
UPIPE_HELPER_VOID(upipe_a52f)
UPIPE_HELPER_SYNC(upipe_a52f, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_a52f, next_uref, next_uref_size, urefs,
                         upipe_a52f_promote_uref)

UPIPE_HELPER_OUTPUT(upipe_a52f, output, flow_def, flow_def_sent)
UPIPE_HELPER_FLOW_DEF(upipe_a52f, flow_def_input, flow_def_attr)

/** @internal @This flushes all dates.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_a52f_flush_dates(struct upipe *upipe)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    uref_clock_set_date_sys(&upipe_a52f->au_uref_s, UINT64_MAX,
                            UREF_DATE_NONE);
    uref_clock_set_date_prog(&upipe_a52f->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_set_date_orig(&upipe_a52f->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_delete_dts_pts_delay(&upipe_a52f->au_uref_s);
}

/** @internal @This allocates an a52f pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_a52f_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_a52f_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    upipe_a52f_init_sync(upipe);
    upipe_a52f_init_uref_stream(upipe);
    upipe_a52f_init_output(upipe);
    upipe_a52f_init_flow_def(upipe);
    upipe_a52f->got_discontinuity = false;
    upipe_a52f->next_frame_size = -1;
    upipe_a52f_flush_dates(upipe);
    upipe_a52f->sync_header[0] = 0x0;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This scans for a sync word.
 *
 * @param upipe description structure of the pipe
 * @param dropped_p filled with the number of octets to drop before the sync
 * @return true if a sync word was found
 */
static bool upipe_a52f_scan(struct upipe *upipe, size_t *dropped_p)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    while (uref_block_scan(upipe_a52f->next_uref, dropped_p, 0xb)) {
        uint8_t word;
        if (!uref_block_extract(upipe_a52f->next_uref, *dropped_p + 1, 1,
                                &word))
            return false;

        if (word == 0x77)
            return true;
        (*dropped_p)++;
    }
    return false;
}

/** @internal @This checks if a sync word begins just after the end of the
 * next frame.
 *
 * @param upipe description structure of the pipe
 * @param ready_p filled with true if a sync word was found
 * @param false if no sync word was found and resync is needed
 */
static bool upipe_a52f_check_frame(struct upipe *upipe, bool *ready_p)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    size_t size;
    *ready_p = false;
    if (!uref_block_size(upipe_a52f->next_uref, &size))
        return false;
    if (size < upipe_a52f->next_frame_size)
        return true;

    uint8_t words[2];
    if (!uref_block_extract(upipe_a52f->next_uref,
                            upipe_a52f->next_frame_size, 2, words)) {
        /* not enough data */
        if (upipe_a52f->acquired) {/* avoid delaying packets unnecessarily */
            *ready_p = true;
        }
        return true;
    }
    if (words[0] != 0xb || words[1] != 0x77) {
        return false;
    }
    *ready_p = true;
    return true;
}

/** @internal @This parses A/52 Annex E header.
 *
 * @param upipe description structure of the pipe
 * @return false in case the header is inconsistent
 */
static bool upipe_a52f_parse_a52e(struct upipe *upipe) {
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    uint8_t header[6];
    if (unlikely(!uref_block_extract(upipe_a52f->next_uref, 0, 6, header)))
        return true;

    /* frame size */
    upipe_a52f->next_frame_size = a52e_get_frame_size(a52e_get_frmsiz(header));

    if (likely(upipe_a52f->sync_header[0])) { /* FIXME */
        return true;
    }

    struct uref *flow_def = upipe_a52f_alloc_flow_def_attr(upipe);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    uref_flow_set_def(flow_def, "block.eac3.sound.");

    flow_def = upipe_a52f_store_flow_def_attr(upipe, flow_def);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    upipe_a52f_store_flow_def(upipe, flow_def);

    upipe_a52f->sync_header[0] = header[0]; /* FIXME */

    return true;
}

/** @internal @This parses A/52 header.
 *
 * @param upipe description structure of the pipe
 * @return false in case the header is inconsistent
 */
static bool upipe_a52f_parse_a52(struct upipe *upipe) {
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    uint8_t header[6];
    if (unlikely(!uref_block_extract(upipe_a52f->next_uref, 0, 6, header)))
        return true;

    /* frame size */
    upipe_a52f->next_frame_size = a52_get_frame_size(a52_get_fscod(header),
                                                a52_get_frmsizecod(header));

    if (likely(upipe_a52f->sync_header[0])) { /* FIXME */
        return true;
    }
    
    struct uref *flow_def = upipe_a52f_alloc_flow_def_attr(upipe);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    uref_flow_set_def(flow_def, "block.ac3.sound.");
    uref_block_flow_set_octetrate(flow_def,
          a52_bitrate_tab[a52_get_frmsizecod(header)] / 8);

    flow_def = upipe_a52f_store_flow_def_attr(upipe, flow_def);
    if (unlikely(!flow_def)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    upipe_a52f_store_flow_def(upipe, flow_def);

    upipe_a52f->sync_header[0] = header[0]; /* FIXME */

    return true;
}

/** @internal @This parses a new header.
 *
 * @param upipe description structure of the pipe
 * @return false in case the header is inconsistent
 */
static bool upipe_a52f_parse_header(struct upipe *upipe)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    uint8_t header[6];
    if (unlikely(!uref_block_extract(upipe_a52f->next_uref, 0, 6, header)))
        return true;

    switch (a52_get_bsid(header)) {
        case A52_BSID:
            return upipe_a52f_parse_a52(upipe);
        case A52_BSID_ANNEX_E:
            return upipe_a52f_parse_a52e(upipe);
        default:
            return false;
    }

    return false; /* never reached */
}

/** @internal @This handles and outputs a frame.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_a52f_output_frame(struct upipe *upipe, struct upump *upump)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);

    struct uref au_uref_s = upipe_a52f->au_uref_s;
    /* From now on, PTS declaration only impacts the next frame. */
    upipe_a52f_flush_dates(upipe);

    struct uref *uref = upipe_a52f_extract_uref_stream(upipe,
            upipe_a52f->next_frame_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

#if 0
    lldiv_t div = lldiv((uint64_t)upipe_a52f->samples * UCLOCK_FREQ +
                        upipe_a52f->duration_residue,
                        upipe_a52f->samplerate);
    uint64_t duration = div.quot;
    upipe_a52f->duration_residue = div.rem;
#else
    uint64_t duration = 0;
#endif

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (uref_clock_get_dts_##dv(&au_uref_s, &date)) {                       \
        uref_clock_set_dts_##dv(uref, date);                                \
        uref_clock_set_dts_##dv(&upipe_a52f->au_uref_s, date + duration);   \
    } else if (uref_clock_get_dts_##dv(uref, &date))                        \
        uref_clock_set_date_##dv(uref, UINT64_MAX, UREF_DATE_NONE);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    uref_clock_set_dts_pts_delay(uref, 0);

    upipe_a52f_output(upipe, uref, upump);
}

/** @internal @This is called back by @ref upipe_a52f_append_uref_stream
 * whenever a new uref is promoted in next_uref.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_a52f_promote_uref(struct upipe *upipe)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (uref_clock_get_dts_##dv(upipe_a52f->next_uref, &date))              \
        uref_clock_set_dts_##dv(&upipe_a52f->au_uref_s, date);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    upipe_a52f->duration_residue = 0;
}

/** @internal @This tries to output frames from the queue of input buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_a52f_work(struct upipe *upipe, struct upump *upump)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    while (upipe_a52f->next_uref != NULL) {
        if (unlikely(!upipe_a52f->acquired)) {
            size_t dropped = 0;
            bool ret = upipe_a52f_scan(upipe, &dropped);
            upipe_a52f_consume_uref_stream(upipe, dropped);
            if (!ret)
                return;
        }
        if (upipe_a52f->next_frame_size == -1 &&
            !upipe_a52f_parse_header(upipe)) {
            upipe_warn(upipe, "invalid header");
            upipe_a52f_consume_uref_stream(upipe, 1);
            upipe_a52f_sync_lost(upipe);
            continue;
        }
        if (upipe_a52f->next_frame_size == -1)
            return; /* not enough data */


        bool ready;
        if (unlikely(!upipe_a52f_check_frame(upipe, &ready))) {
            upipe_warn(upipe, "invalid frame");
            upipe_a52f_consume_uref_stream(upipe, 1);
            upipe_a52f->next_frame_size = -1;
            upipe_a52f_sync_lost(upipe);
            continue;
        }
        if (!ready)
            return; /* not enough data */

        upipe_a52f_sync_acquired(upipe);
        upipe_a52f_output_frame(upipe, upump);
        upipe_a52f->next_frame_size = -1;
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_a52f_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct upipe_a52f *upipe_a52f = upipe_a52f_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        upipe_a52f_output(upipe, uref, upump);
        return;
    }

    if (unlikely(uref_flow_get_discontinuity(uref))) {
        /* Drop the current frame and resync. */
        upipe_a52f_clean_uref_stream(upipe);
        upipe_a52f_init_uref_stream(upipe);
        upipe_a52f->got_discontinuity = true;
        upipe_a52f->next_frame_size = -1;
        upipe_a52f_sync_lost(upipe);
    }

    upipe_a52f_append_uref_stream(upipe, uref);
    upipe_a52f_work(upipe, upump);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static bool upipe_a52f_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return false;
    if (unlikely(!uref_flow_match_def(flow_def, "block.")))
        return false;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    flow_def = upipe_a52f_store_flow_def_input(upipe, flow_def_dup);
    if (flow_def != NULL)
        upipe_a52f_store_flow_def(upipe, flow_def);
    return true;
}

/** @internal @This processes control commands on a a52f pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_a52f_control(struct upipe *upipe,
                                enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_a52f_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_a52f_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_a52f_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_a52f_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_a52f_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_a52f_clean_uref_stream(upipe);
    upipe_a52f_clean_output(upipe);
    upipe_a52f_clean_flow_def(upipe);
    upipe_a52f_clean_sync(upipe);

    upipe_a52f_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_a52f_mgr = {
    .signature = UPIPE_A52F_SIGNATURE,

    .upipe_alloc = upipe_a52f_alloc,
    .upipe_input = upipe_a52f_input,
    .upipe_control = upipe_a52f_control,
    .upipe_free = upipe_a52f_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all a52f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_a52f_mgr_alloc(void)
{
    return &upipe_a52f_mgr;
}
