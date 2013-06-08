/*
 ============================================================================
 Name        : gst-mfx-helpers.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#ifndef __GST_MFX_HELPERS_H__
#define __GST_MFX_HELPERS_H__

#include <gst/gst.h>

#define GST_STATE_CHANGE_DIR(transition) (gst_state_change_get_dir (transition))

typedef enum _GstStateChangeDir GstStateChangeDir;

enum _GstStateChangeDir
{
    GST_STATE_CHANGE_DIR_UPWARDS = 1,
    GST_STATE_CHANGE_DIR_DOWNWARDS = 0,
};

GstStateChangeDir gst_state_change_get_dir (GstStateChange transition);

#endif /* __GST_MFX_HELPERS_H__ */

