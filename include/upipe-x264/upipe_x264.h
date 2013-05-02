/*****************************************************************************
 * upipe_x264.h: application interface for x264 module
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
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
 *****************************************************************************/

/** @file
 * @short Upipe x264 module
 */

#ifndef _UPIPE_MODULES_UPIPE_X264_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_X264_H_

#include <upipe/upipe.h>

#define UPIPE_X264_SIGNATURE UBASE_FOURCC('x','2','6','4')

/** @This extends upipe_command with specific commands for x264. */
enum upipe_x264_command {
    UPIPE_X264_SENTINEL = UPIPE_CONTROL_LOCAL,
    
    /** reconfigure encoder with updated parameters */
    UPIPE_X264_RECONFIG,

    /** set default params */
    UPIPE_X264_SET_DEFAULT,

    /** set default params for preset (const char *, const char *) */
    UPIPE_X264_SET_DEFAULT_PRESET,

    /** enforce profile (const char *) */
    UPIPE_X264_SET_PROFILE
};

/** @This reconfigures encoder with updated parameters.
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static inline bool upipe_x264_reconfigure(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_X264_RECONFIG, UPIPE_X264_SIGNATURE);
}

/** @This sets default parameters (and runs CPU detection).
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static inline bool upipe_x264_set_default(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_X264_SET_DEFAULT, UPIPE_X264_SIGNATURE);
}

/** @This sets default parameters for specified preset.
 *
 * @param upipe description structure of the pipe
 * @param preset x264 preset
 * @param tuning x264 tuning
 * @return false in case of error
 */
static inline bool upipe_x264_set_default_preset(struct upipe *upipe, const char *preset,
                                         const char *tuning)
{
    return upipe_control(upipe, UPIPE_X264_SET_DEFAULT_PRESET, UPIPE_X264_SIGNATURE,
                         preset, tuning);
}

/** @This enforces profile.
 *
 * @param upipe description structure of the pipe
 * @param profile x264 profile
 * @return false in case of error
 */
static inline bool upipe_x264_set_profile(struct upipe *upipe, const char *profile)
{
    return upipe_control(upipe, UPIPE_X264_SET_PROFILE, UPIPE_X264_SIGNATURE,
                         profile);
}

/** @This returns the management structure for x264 pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_x264_mgr_alloc(void);

#endif