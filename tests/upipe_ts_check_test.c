/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS check module
 */

#undef NDEBUG

#include <upipe/ulog.h>
#include <upipe/ulog_stdio.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_check.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define ULOG_LEVEL ULOG_DEBUG

static unsigned int nb_packets = 0;

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_ts_check */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe, struct ulog *ulog)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe, ulog);
    return upipe;
}

/** helper phony pipe to test upipe_ts_check */
static void ts_test_input(struct upipe *upipe, struct uref *uref,
                          struct upump *upump)
{
    assert(uref != NULL);
    const char *def;
    if (uref_flow_get_def(uref, &def)) {
        uref_free(uref);
        return;
    }

    {
        size_t size;
        assert(uref_block_size(uref, &size));
        assert(size == TS_SIZE);
    }

    const uint8_t *buffer;
    int size = 1;
    assert(uref_block_read(uref, 0, &size, &buffer));
    assert(size == 1);
    assert(ts_validate(buffer));
    uref_block_unmap(uref, 0, size);
    uref_free(uref);
    nb_packets--;
}

/** helper phony pipe to test upipe_ts_check */
static void ts_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_ts_check */
static struct upipe_mgr ts_test_mgr = {
    .upipe_alloc = ts_test_alloc,
    .upipe_input = ts_test_input,
    .upipe_control = NULL,
    .upipe_use = NULL,
    .upipe_release = NULL,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_print = uprobe_print_alloc(&uprobe, stdout, "test");
    assert(uprobe_print != NULL);

    struct upipe *upipe_sink = upipe_alloc(&ts_test_mgr, uprobe_print,
            ulog_stdio_alloc(stdout, ULOG_LEVEL, "sink"));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_ts_check_mgr = upipe_ts_check_mgr_alloc();
    assert(upipe_ts_check_mgr != NULL);
    struct upipe *upipe_ts_check = upipe_alloc(upipe_ts_check_mgr, uprobe_print,
            ulog_stdio_alloc(stdout, ULOG_LEVEL, "ts check"));
    assert(upipe_ts_check != NULL);
    assert(upipe_set_output(upipe_ts_check, upipe_sink));

    struct uref *uref;
    uint8_t *buffer;
    int size;
    int i;
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    upipe_input(upipe_ts_check, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 7 * TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 7 * TS_SIZE);
    for (i = 0; i < 7; i++)
        ts_pad(buffer + i * TS_SIZE);
    uref_block_unmap(uref, 0, size);
    nb_packets = 7;
    upipe_input(upipe_ts_check, uref, NULL);
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 7 * TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 7 * TS_SIZE);
    for (i = 0; i < 7; i++)
        ts_pad(buffer + i * TS_SIZE);
    buffer[3 * TS_SIZE] = 0xff;
    uref_block_unmap(uref, 0, size);
    nb_packets = 3;
    upipe_input(upipe_ts_check, uref, NULL);
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1 + 7 * TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1 + 7 * TS_SIZE);
    buffer[0] = 0xff;
    for (i = 0; i < 7; i++)
        ts_pad(buffer + 1 + i * TS_SIZE);
    uref_block_unmap(uref, 0, size);
    nb_packets = 0;
    upipe_input(upipe_ts_check, uref, NULL);
    assert(!nb_packets);

    upipe_release(upipe_ts_check);
    upipe_mgr_release(upipe_ts_check_mgr); // nop

    ts_test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_print_free(uprobe_print);

    return 0;
}