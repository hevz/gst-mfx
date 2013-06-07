/*
 ============================================================================
 Name        : gst-mfx-base.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#include <stdlib.h>

#include "gst-mfx-base.h"

#define GST_MFX_BASE_IMPL_DEFAULT           MFX_IMPL_AUTO_ANY
#define GST_MFX_BASE_VERSION_DEFAULT        "1.1"
#define GST_MFX_BASE_ASYNC_DEPTH_DEFAULT    0
#define GST_MFX_BASE_PROTECTED_DEFAULT      0
#define GST_MFX_BASE_IO_PATTERN_DEFAULT     MFX_IOPATTERN_IN_SYSTEM_MEMORY

#define GST_MFX_BASE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_BASE, GstMfxBasePrivate))
#define GST_CAT_DEFAULT (mfxbase_debug)

enum
{
    PROP_ZERO,
    PROP_IMPL,
    PROP_VERSION,
    PROP_ASYNC_DEPTH,
    PROP_PROTECTED,
    PROP_IO_PATTERN,
    N_PROPERTIES
};

typedef struct _GstMfxBasePrivate GstMfxBasePrivate;

struct _GstMfxBasePrivate
{
    gint32 impl;
    mfxVersion version;
    guint16 async_depth;
    guint16 protected;
    guint16 io_pattern;
};

static void gst_mfx_base_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec);
static void gst_mfx_base_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_mfx_base_change_state (GstElement *element,
            GstStateChange transition);

GST_DEBUG_CATEGORY_STATIC (mfxbase_debug);

GST_BOILERPLATE (GstMfxBase, gst_mfx_base,
            GstElement, GST_TYPE_ELEMENT);

#define GST_TYPE_MFX_BASE_IMPL   \
    (gst_mfx_base_impl_get_type ())
static GType
gst_mfx_base_impl_get_type (void)
{
    static GType impl_type = 0;
    static const GEnumValue impl[] =
    {
        { MFX_IMPL_AUTO, "Auto", "auto" },
        { MFX_IMPL_SOFTWARE, "Software", "software" },
        { MFX_IMPL_HARDWARE, "Hardware", "hardware" },
        { MFX_IMPL_AUTO_ANY, "Auto any", "auto-any" },
        { MFX_IMPL_HARDWARE_ANY, "Hardware any", "hardware-any" },
        { MFX_IMPL_HARDWARE2, "Hardware 2nd device", "hardware2" },
        { MFX_IMPL_HARDWARE3, "Hardware 3rd device", "hardware3" },
        { MFX_IMPL_HARDWARE4, "Hardware 4th device", "hardware4" },
        { 0, NULL, NULL }
    };

    if (!impl_type)
      impl_type = g_enum_register_static ("GstMfxBaseImplType", impl);

    return impl_type;
}

#define GST_TYPE_MFX_BASE_IO_PATTERN   \
    (gst_mfx_base_io_pattern_get_type ())
static GType
gst_mfx_base_io_pattern_get_type (void)
{
    static GType io_pattern_type = 0;
    static const GEnumValue io_pattern[] =
    {
        { MFX_IOPATTERN_IN_VIDEO_MEMORY, "Video meory in", "video-mem-in" },
        { MFX_IOPATTERN_IN_SYSTEM_MEMORY, "System memory in", "system-mem-in" },
        { MFX_IOPATTERN_IN_OPAQUE_MEMORY, "Opaque memory in", "opaque-mem-in" },
        { MFX_IOPATTERN_OUT_VIDEO_MEMORY, "Video memory out", "video-mem-out" },
        { MFX_IOPATTERN_OUT_SYSTEM_MEMORY, "System memory out", "system-mem-out" },
        { MFX_IOPATTERN_OUT_OPAQUE_MEMORY, "Opaque memory out", "opaque-mem-out" },
        { 0, NULL, NULL }
    };

    if (!io_pattern_type)
      io_pattern_type = g_enum_register_static ("GstMfxBaseIOPatternType",
                  io_pattern);

    return io_pattern_type;
}

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

static void
gst_mfx_base_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec)
{
    GstMfxBase *self = GST_MFX_BASE (obj);
    GstMfxBasePrivate *priv = GST_MFX_BASE_GET_PRIVATE (self);

    switch (id) {
    case PROP_IMPL:
        priv->impl = g_value_get_enum (value);
        break;
    case PROP_VERSION:
        {
            gchar **v = g_regex_split_simple ("(\\d+)\\.(\\d+)",
                        g_value_get_string (value), 0, 0);
            if (!v) {
                GST_ERROR ("Split version string failed!");
                break;
            }
            priv->version.Major = atoi (v[1]);
            priv->version.Minor = atoi (v[2]);
            g_strfreev (v);
        }
        break;
    case PROP_ASYNC_DEPTH:
        priv->async_depth = g_value_get_uint (value);
        break;
    case PROP_PROTECTED:
        priv->protected = g_value_get_uint (value);
        break;
    case PROP_IO_PATTERN:
        priv->io_pattern = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
gst_mfx_base_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec)
{
    GstMfxBase *self = GST_MFX_BASE (obj);
    GstMfxBasePrivate *priv = GST_MFX_BASE_GET_PRIVATE (self);

    switch (id) {
    case PROP_IMPL:
        g_value_set_enum (value, priv->impl);
        break;
    case PROP_VERSION:
        {
            gchar *str = g_strdup_printf ("%u.%u",
                        priv->version.Major, priv->version.Minor);
            g_value_set_string (value, str);
            g_free (str);
        }
        break;
    case PROP_ASYNC_DEPTH:
        g_value_set_uint (value, priv->async_depth);
        break;
    case PROP_PROTECTED:
        g_value_set_uint (value, priv->protected);
        break;
    case PROP_IO_PATTERN:
        g_value_set_enum (value, priv->io_pattern);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static GstStateChangeReturn
gst_mfx_base_change_state (GstElement *element,
            GstStateChange transition)
{
    GstMfxBase *self = GST_MFX_BASE (element);
    GstMfxBasePrivate *priv = GST_MFX_BASE_GET_PRIVATE (self);
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
            mfxIMPL impl = priv->impl;

            s = MFXInit (impl, &priv->version, &self->mfx_session);
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
    obj_class->set_property = gst_mfx_base_set_property;
    obj_class->get_property = gst_mfx_base_get_property;
    obj_class->dispose = gst_mfx_base_dispose;
    obj_class->finalize = gst_mfx_base_finalize;

    element_class->change_state = gst_mfx_base_change_state;

    /* Properties */
    g_object_class_install_property (obj_class, PROP_IMPL,
                g_param_spec_enum ("impl", "Implentation",
                    "Implentation.",
                    GST_TYPE_MFX_BASE_IMPL,
                    GST_MFX_BASE_IMPL_DEFAULT,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property (obj_class, PROP_VERSION,
                g_param_spec_string ("version", "Version",
                    "Version.",
                    GST_MFX_BASE_VERSION_DEFAULT,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property (obj_class, PROP_ASYNC_DEPTH,
                g_param_spec_uint ("async-depth", "Async depth",
                    "Async queue depth.",
                    0, G_MAXUINT, GST_MFX_BASE_ASYNC_DEPTH_DEFAULT,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property (obj_class, PROP_PROTECTED,
                g_param_spec_uint ("protected", "Protected",
                    "Protected.",
                    0, G_MAXUINT, GST_MFX_BASE_PROTECTED_DEFAULT,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property (obj_class, PROP_IO_PATTERN,
                g_param_spec_enum ("io-pattern", "IO Pattern",
                    "IO Pattern.",
                    GST_TYPE_MFX_BASE_IO_PATTERN,
                    GST_MFX_BASE_IO_PATTERN_DEFAULT,
                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    g_type_class_add_private (klass, sizeof (GstMfxBasePrivate));

    GST_DEBUG_CATEGORY_INIT (mfxbase_debug, "mfxbase", 0, "MFX Base");
}

static void
gst_mfx_base_init (GstMfxBase *self,
            GstMfxBaseClass *klass)
{
}

