/*
 ============================================================================
 Name        : gst-mfx-dec.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#ifndef __GST_MFX_DEC_H__
#define __GST_MFX_DEC_H__

#include <gst/gst.h>

#include "gst-mfx-base.h"

G_BEGIN_DECLS

#define GST_TYPE_MFX_DEC (gst_mfx_dec_get_type ())
#define GST_MFX_DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MFX_DEC, GstMfxDec))
#define GST_IS_MFX_DEC(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MFX_DEC))
#define GST_MFX_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MFX_DEC, GstMfxDecClass))
#define GST_IS_MFX_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MFX_DEC))
#define GST_MFX_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MFX_DEC, GstMfxDecClass))

typedef struct _GstMfxDec GstMfxDec;
typedef struct _GstMfxDecClass GstMfxDecClass;

struct _GstMfxDec
{
    GstMfxBase parent_instance;
};

struct _GstMfxDecClass
{
    GstMfxBaseClass parent_class;
};

GType gst_mfx_dec_get_type (void);

G_END_DECLS

#endif /* __GST_MFX_DEC_H__ */

