/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short simple probe logging all received events, as a fall-back
 */

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_log.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

/** first event to log */
#define UPROBE_FIRST_EVENT UPROBE_READY
/** last event to log */
#define UPROBE_LAST_EVENT UPROBE_CLOCK_TS

/** @This is a super-set of the uprobe structure with additional local members.
 */
struct uprobe_log {
    /** level at which to log the messages */
    enum uprobe_log_level level;
    /** events to log */
    bool events[UPROBE_LAST_EVENT + 1 - UPROBE_FIRST_EVENT];
    /** whether to log unknown events */
    bool unknown_events;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_log, uprobe)

/** @internal @This converts an error code into a description string.
 *
 * @param errcode error code
 * @return a description string
 */
static const char *uprobe_log_errcode(enum uprobe_error_code errcode)
{
    switch (errcode) {
        case UPROBE_ERR_ALLOC: return "allocation error";
        case UPROBE_ERR_UPUMP: return "upump error";
        case UPROBE_ERR_INVALID: return "invalid argument";
        case UPROBE_ERR_EXTERNAL: return "external error";
        default: return "unknown error";
    }
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return always false
 */
static bool uprobe_log_throw(struct uprobe *uprobe, struct upipe *upipe,
                             enum uprobe_event event, va_list args)
{
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    if (upipe == NULL)
        return false;

    va_list args_copy;
    va_copy(args_copy, args);

    if (event >= UPROBE_FIRST_EVENT && event <= UPROBE_LAST_EVENT) {
        if (!log->events[event - UPROBE_FIRST_EVENT])
            return false;
    } else if (!log->unknown_events)
        return false;

    switch (event) {
        case UPROBE_READY:
            upipe_log(upipe, log->level, "probe caught ready event");
            break;
        case UPROBE_DEAD:
            upipe_log(upipe, log->level, "probe caught dead event");
            break;
        case UPROBE_LOG:
            break;
        case UPROBE_FATAL: {
            enum uprobe_error_code errcode =
                va_arg(args, enum uprobe_error_code);
            upipe_log_va(upipe, log->level, "probe caught fatal error: %s (%x)",
                         uprobe_log_errcode(errcode), errcode);
            break;
        }
        case UPROBE_ERROR: {
            enum uprobe_error_code errcode =
                va_arg(args, enum uprobe_error_code);
            upipe_log_va(upipe, log->level, "probe caught error: %s (%x)",
                         uprobe_log_errcode(errcode), errcode);
            break;
        }
        case UPROBE_SOURCE_END:
            upipe_log_va(upipe, log->level, "probe caught source end");
            break;
        case UPROBE_SINK_END:
            upipe_log_va(upipe, log->level, "probe caught sink end");
            break;
        case UPROBE_NEED_UREF_MGR:
            upipe_log(upipe, log->level,
                      "probe caught need uref manager");
            break;
        case UPROBE_NEED_UPUMP_MGR:
            upipe_log(upipe, log->level,
                      "probe caught need upump manager");
            break;
        case UPROBE_NEED_UCLOCK:
            upipe_log(upipe, log->level, "probe caught need uclock");
            break;
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            if (flow_def != NULL)
                uref_flow_get_def(flow_def, &def);
            upipe_log_va(upipe, log->level,
                         "probe caught new flow def \"%s\"", def);
            break;
        }
        case UPROBE_NEED_UBUF_MGR: {
            struct uref *flow_def = va_arg(args_copy, struct uref *);
            const char *def = "[invalid]";
            if (flow_def != NULL)
                uref_flow_get_def(flow_def, &def);
            upipe_log_va(upipe, log->level,
                         "probe caught need ubuf manager for flow def \"%s\"",
                         def);
            break;
        }
        case UPROBE_NEW_RAP:
            upipe_log(upipe, log->level,
                      "probe caught new random access point");
            break;
        case UPROBE_SPLIT_UPDATE:
            upipe_log(upipe, log->level, "probe caught split update");
            break;
        case UPROBE_SYNC_ACQUIRED:
            upipe_log(upipe, log->level, "probe caught sync acquired");
            break;
        case UPROBE_SYNC_LOST:
            upipe_log(upipe, log->level, "probe caught sync lost");
            break;
        case UPROBE_CLOCK_REF: {
            struct uref *uref = va_arg(args_copy, struct uref *);
            uint64_t pcr = va_arg(args_copy, uint64_t);
            int discontinuity = va_arg(args_copy, int);
            if (discontinuity == 1)
                upipe_log_va(upipe, log->level,
                         "probe caught new clock ref %"PRIu64" (discontinuity)",
                         pcr);
            else
                upipe_log_va(upipe, log->level,
                             "probe caught new clock ref %"PRIu64, pcr);
            break;
        }
        case UPROBE_CLOCK_TS: {
            struct uref *uref = va_arg(args_copy, struct uref *);
            uint64_t date = UINT64_MAX;
            enum uref_date_type type;
            uref_clock_get_date_orig(uref, &date, &type);
            if (unlikely(type == UREF_DATE_NONE))
                upipe_log(upipe, log->level,
                          "probe caught an invalid timestamp event");
            else
                upipe_log_va(upipe, log->level, "probe caught new date %"PRIu64,
                             date);
            break;
        }
        default:
            upipe_log_va(upipe, log->level,
                     "probe caught an unknown, uncaught event (0x%x)", event);
            break;
    }
    va_end(args_copy);
    return false;
}

/** @This allocates a new uprobe log structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_log_alloc(struct uprobe *next,
                                enum uprobe_log_level level)
{
    struct uprobe_log *log = malloc(sizeof(struct uprobe_log));
    if (unlikely(log == NULL))
        return NULL;
    struct uprobe *uprobe = uprobe_log_to_uprobe(log);
    log->level = level;
    int i;
    for (i = 0; i < UPROBE_LAST_EVENT - UPROBE_FIRST_EVENT; i++)
        log->events[i] = true;
    /* by default disable clock events and unknown events */
    log->events[UPROBE_CLOCK_REF - UPROBE_FIRST_EVENT] = false;
    log->events[UPROBE_CLOCK_TS - UPROBE_FIRST_EVENT] = false;
    log->unknown_events = false;
    uprobe_init(uprobe, uprobe_log_throw, next);
    return uprobe;
}

/** @This frees a uprobe log structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_log_free(struct uprobe *uprobe)
{
    struct uprobe *next = uprobe->next;
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    free(log);
    return next;
}

/** @This masks an event from being logged.
 *
 * @param uprobe probe structure
 * @param event event to mask
 */
void uprobe_log_mask_event(struct uprobe *uprobe, enum uprobe_event event)
{
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    assert(event >= UPROBE_FIRST_EVENT);
    assert(event <= UPROBE_LAST_EVENT);
    log->events[event - UPROBE_FIRST_EVENT] = false;
}

/** @This unmasks an event from being logged.
 *
 * @param uprobe probe structure
 * @param event event to unmask
 */
void uprobe_log_unmask_event(struct uprobe *uprobe, enum uprobe_event event)
{
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    assert(event >= UPROBE_FIRST_EVENT);
    assert(event <= UPROBE_LAST_EVENT);
    log->events[event - UPROBE_FIRST_EVENT] = true;
}

/** @This masks unknown events from being logged.
 *
 * @param uprobe probe structure
 */
void uprobe_log_mask_unknown_events(struct uprobe *uprobe)
{
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    log->unknown_events = false;
}

/** @This unmasks unknown events from being logged.
 *
 * @param uprobe probe structure
 */
void uprobe_log_unmask_unknown_events(struct uprobe *uprobe)
{
    struct uprobe_log *log = uprobe_log_from_uprobe(uprobe);
    log->unknown_events = true;
}
