/*
 ============================================================================
 Name        : gst-mfx-base.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#ifndef __GST_MFX_BASE_H__
#define __GST_MFX_BASE_H__

#include <string.h>
#include <mfxvideo.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_MFX_BASE (gst_mfx_base_get_type ())
#define GST_MFX_BASE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MFX_BASE, GstMfxBase))
#define GST_IS_MFX_BASE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MFX_BASE))
#define GST_MFX_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MFX_BASE, GstMfxBaseClass))
#define GST_IS_MFX_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MFX_BASE))
#define GST_MFX_BASE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MFX_BASE, GstMfxBaseClass))

typedef struct _GstMfxBase GstMfxBase;
typedef struct _GstMfxBaseClass GstMfxBaseClass;

struct _GstMfxBase
{
    GstElement parent_instance;

    /* Private */
    mfxSession mfx_session;
};

struct _GstMfxBaseClass
{
    GstElementClass parent_class;
};

GType gst_mfx_base_get_type (void);

G_END_DECLS

#endif /* __GST_MFX_BASE_H__ */

