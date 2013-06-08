/*
 ============================================================================
 Name        : gst-mfx-helpers.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#include "gst-mfx-helpers.h"

GstStateChangeDir
gst_state_change_get_dir (GstStateChange transition)
{
    GstState curr, next;

    curr = (transition & 0x38) >> 3;
    next = transition & 0x7;

    return (curr < next) ?
        GST_STATE_CHANGE_DIR_UPWARDS :
        GST_STATE_CHANGE_DIR_DOWNWARDS;
}

