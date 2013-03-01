/*
 ============================================================================
 Name        : gst-mfx-dec.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#include "gst-mfx-dec.h"

#define GST_MFX_DEC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_DEC, GstMfxDecPrivate))

typedef struct _GstMfxDecPrivate GstMfxDecPrivate;

struct _GstMfxDecPrivate
{
    gchar *c;
};

static const GstElementDetails gst_mfx_dec_details =
GST_ELEMENT_DETAILS (
            "MFX Decoder",
            "Codec/Decoder/Video",
            "MFX Video Decoder",
            "Heiher <admin@heiher.info>");

GST_BOILERPLATE (GstMfxDec, gst_mfx_dec,
            GstElement, GST_TYPE_ELEMENT);

static void
gst_mfx_dec_dispose (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_dec_finalize (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GObject *
gst_mfx_dec_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_mfx_dec_constructed (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_mfx_dec_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    gst_element_class_set_details (element_class, &gst_mfx_dec_details);
}

static void
gst_mfx_dec_class_init (GstMfxDecClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    obj_class->constructor = gst_mfx_dec_constructor;
    obj_class->constructed = gst_mfx_dec_constructed;
    obj_class->dispose = gst_mfx_dec_dispose;
    obj_class->finalize = gst_mfx_dec_finalize;

    g_type_class_add_private (klass, sizeof (GstMfxDecPrivate));
}

static void
gst_mfx_dec_init (GstMfxDec *self,
            GstMfxDecClass *klass)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
}

