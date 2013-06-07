/*
 ============================================================================
 Name        : gst-mfx-base.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#include "gst-mfx-base.h"

#define GST_MFX_BASE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_BASE, GstMfxBasePrivate))
#define GST_CAT_DEFAULT (mfxbase_debug)

typedef struct _GstMfxBasePrivate GstMfxBasePrivate;

struct _GstMfxBasePrivate
{
    gchar c;
};

static GstStateChangeReturn gst_mfx_base_change_state (GstElement *element,
            GstStateChange transition);

GST_DEBUG_CATEGORY_STATIC (mfxbase_debug);

GST_BOILERPLATE (GstMfxBase, gst_mfx_base,
            GstElement, GST_TYPE_ELEMENT);

static void
gst_mfx_base_dispose (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_base_finalize (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GObject *
gst_mfx_base_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_mfx_base_constructed (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_mfx_base_base_init (gpointer klass)
{
}

static GstStateChangeReturn
gst_mfx_base_change_state (GstElement *element,
            GstStateChange transition)
{
    GstMfxBase *self = GST_MFX_BASE (element);
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

    if ((GST_STATE_CHANGE_NULL_TO_READY == transition) ||
        (GST_STATE_CHANGE_READY_TO_PAUSED == transition) ||
        (GST_STATE_CHANGE_PAUSED_TO_PLAYING == transition)) {
        ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
        if (GST_STATE_CHANGE_FAILURE == ret)
          goto out;
    }

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        {
            mfxStatus s = MFX_ERR_NONE;
            mfxVersion ver;
            mfxIMPL impl = MFX_IMPL_AUTO_ANY;

            ver.Minor = 1;
            ver.Major = 1;
            s = MFXInit (impl, &ver, &self->mfx_session);
            if (MFX_ERR_NONE != s) {
                GST_ERROR ("MFXInit failed(%d)!", s);
                ret = GST_STATE_CHANGE_FAILURE;
                goto out;
            }

            s = MFXQueryIMPL (self->mfx_session, &impl);
            if (MFX_ERR_NONE == s) {
                gchar *str = "Unknown";

                if (MFX_IMPL_HARDWARE == impl)
                  str = "Hardware";
                if (MFX_IMPL_SOFTWARE == impl)
                  str = "Software";
                GST_DEBUG ("MFXQueryIMPL -> %s", str);
            }
            s = MFXQueryVersion (self->mfx_session, &ver);
            if (MFX_ERR_NONE == s)
              GST_DEBUG ("MFXQueryVersion -> %d.%d",
                          ver.Major, ver.Minor);
        }
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        MFXVideoENCODE_Close (self->mfx_session);
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        MFXClose (self->mfx_session);
        break;
    default:
        break;
    }

    if ((GST_STATE_CHANGE_PLAYING_TO_PAUSED == transition) ||
        (GST_STATE_CHANGE_PAUSED_TO_READY == transition) ||
        (GST_STATE_CHANGE_READY_TO_NULL == transition)) {
        ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
        if (GST_STATE_CHANGE_FAILURE == ret)
          goto out;
    }

out:
    return ret;
}

static void
gst_mfx_base_class_init (GstMfxBaseClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    obj_class->constructor = gst_mfx_base_constructor;
    obj_class->constructed = gst_mfx_base_constructed;
    obj_class->dispose = gst_mfx_base_dispose;
    obj_class->finalize = gst_mfx_base_finalize;

    element_class->change_state = gst_mfx_base_change_state;

    g_type_class_add_private (klass, sizeof (GstMfxBasePrivate));

    GST_DEBUG_CATEGORY_INIT (mfxbase_debug, "mfxbase", 0, "MFX Base");
}

static void
gst_mfx_base_init (GstMfxBase *self,
            GstMfxBaseClass *klass)
{
}

