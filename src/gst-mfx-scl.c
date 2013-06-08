/*
 ============================================================================
 Name        : gst-mfx-scl.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#include "gst-mfx-scl.h"

#define DEFAULT_TARGET_WIDTH    640
#define DEFAULT_TARGET_HEIGHT   480

#define GST_MFX_SCL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_SCL, GstMfxSclPrivate))
#define GST_CAT_DEFAULT (mfxscl_debug)

enum
{
    PROP_ZERO,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPERTIES
};

typedef struct _GstMfxSclPrivate GstMfxSclPrivate;

struct _GstMfxSclPrivate
{
    gint width;
    gint height;
};

static void gst_mfx_scl_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec);
static void gst_mfx_scl_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec);
static void gst_mfx_scl_update_params (GstMfxTrans *trans,
            mfxVideoParam *params);

GST_DEBUG_CATEGORY_STATIC (mfxscl_debug);

GST_BOILERPLATE (GstMfxScl, gst_mfx_scl,
            GstMfxTrans, GST_TYPE_MFX_TRANS);

static void
gst_mfx_scl_dispose (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_scl_finalize (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GObject *
gst_mfx_scl_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_mfx_scl_constructed (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_mfx_scl_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_set_details_simple (element_class,
                "MFX Scaler",
                "Filter/Converter/Video/Scaler",
                "MFX Video Scaler",
                "Heiher <admin@heiher.info>");
}

static void
gst_mfx_scl_class_init (GstMfxSclClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);
    GstMfxTransClass *mfx_trans_class = GST_MFX_TRANS_CLASS (klass);

    obj_class->constructor = gst_mfx_scl_constructor;
    obj_class->constructed = gst_mfx_scl_constructed;
    obj_class->set_property = gst_mfx_scl_set_property;
    obj_class->get_property = gst_mfx_scl_get_property;
    obj_class->dispose = gst_mfx_scl_dispose;
    obj_class->finalize = gst_mfx_scl_finalize;

    mfx_trans_class->update_params = gst_mfx_scl_update_params;

    g_object_class_install_property (obj_class, PROP_WIDTH,
                g_param_spec_int ("width", "Width",
                    "Target width", 1, G_MAXINT, DEFAULT_TARGET_WIDTH,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property (obj_class, PROP_HEIGHT,
                g_param_spec_int ("height", "Height",
                    "Target height", 1, G_MAXINT, DEFAULT_TARGET_HEIGHT,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    g_type_class_add_private (klass, sizeof (GstMfxSclPrivate));

    GST_DEBUG_CATEGORY_INIT (mfxscl_debug, "mfxscl", 0, "MFX Scaler");
}

static void
gst_mfx_scl_init (GstMfxScl *self,
            GstMfxSclClass *klass)
{
}

static void
gst_mfx_scl_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec)
{
    GstMfxScl *self = GST_MFX_SCL (obj);
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);

    switch (id) {
    case PROP_WIDTH:
        priv->width = g_value_get_int (value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
gst_mfx_scl_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec)
{
    GstMfxScl *self = GST_MFX_SCL (obj);
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);

    switch (id) {
    case PROP_WIDTH:
        g_value_set_int (value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int (value, priv->height);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
gst_mfx_scl_update_params (GstMfxTrans *trans, mfxVideoParam *params)
{
    GstMfxScl *self = GST_MFX_SCL (trans);
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);

    params->vpp.Out.Width = priv->width;
    params->vpp.Out.Height = priv->height;
    params->vpp.Out.CropW = priv->width;
    params->vpp.Out.CropH = priv->height;
}

