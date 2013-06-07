/*
 ============================================================================
 Name        : gst-mfx-scl.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#ifndef __GST_MFX_SCL_H__
#define __GST_MFX_SCL_H__

#include <gst/gst.h>

#include "gst-mfx-base.h"

G_BEGIN_DECLS

#define GST_TYPE_MFX_SCL (gst_mfx_scl_get_type ())
#define GST_MFX_SCL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MFX_SCL, GstMfxScl))
#define GST_IS_MFX_SCL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MFX_SCL))
#define GST_MFX_SCL_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MFX_SCL, GstMfxSclClass))
#define GST_IS_MFX_SCL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MFX_SCL))
#define GST_MFX_SCL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MFX_SCL, GstMfxSclClass))

typedef struct _GstMfxScl GstMfxScl;
typedef struct _GstMfxSclClass GstMfxSclClass;

struct _GstMfxScl
{
    GstMfxBase parent_instance;
};

struct _GstMfxSclClass
{
    GstMfxBaseClass parent_class;
};

GType gst_mfx_scl_get_type (void);

G_END_DECLS

#endif /* __GST_MFX_SCL_H__ */

