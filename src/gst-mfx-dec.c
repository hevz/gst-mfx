/*
 ============================================================================
 Name        : gst-mfx-dec.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#include "gst-mfx-dec.h"

#define GST_MFX_DEC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_DEC, GstMfxDecPrivate))
#define GST_CAT_DEFAULT (mfxdec_debug)

typedef struct _GstMfxDecPrivate GstMfxDecPrivate;

struct _GstMfxDecPrivate
{
    gchar c;
};

GST_DEBUG_CATEGORY_STATIC (mfxdec_debug);

GST_BOILERPLATE (GstMfxDec, gst_mfx_dec,
            GstMfxBase, GST_TYPE_MFX_BASE);

static void
gst_mfx_dec_dispose (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_dec_finalize (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GObject *
gst_mfx_dec_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_mfx_dec_constructed (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_mfx_dec_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_set_details_simple (element_class,
                "MFX Decoder",
                "Codec/Decoder/Video",
                "MFX Video Decoder",
                "Heiher <admin@heiher.info>");
}

static void
gst_mfx_dec_class_init (GstMfxDecClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);

    obj_class->constructor = gst_mfx_dec_constructor;
    obj_class->constructed = gst_mfx_dec_constructed;
    obj_class->dispose = gst_mfx_dec_dispose;
    obj_class->finalize = gst_mfx_dec_finalize;

    g_type_class_add_private (klass, sizeof (GstMfxDecPrivate));

    GST_DEBUG_CATEGORY_INIT (mfxdec_debug, "mfxdec", 0, "MFX Decoder");
}

static void
gst_mfx_dec_init (GstMfxDec *self,
            GstMfxDecClass *klass)
{
}

