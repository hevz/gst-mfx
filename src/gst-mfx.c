/*
 ============================================================================
 Name        : gst-mfx.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#include "gst-mfx.h"

#define PACKAGE "gstmfx"
#define VERSION "0.0.1"

static gboolean plugin_init (GstPlugin *);

#ifdef GST_PLUGIN_BUILD_STATIC
GST_PLUGIN_DEFINE2 (
#else
GST_PLUGIN_DEFINE (
#endif
    GST_VERSION_MAJOR,  /* major */
    GST_VERSION_MINOR,  /* minor */
#ifdef GST_PLUGIN_BUILD_STATIC
    mfx,                /* short unique name */
#else
    "mfx",              /* short unique name */
#endif
    "Video en/decoder via MFX",  /* info */
    plugin_init,    /* GstPlugin::plugin_init */
    VERSION,        /* version */
    "GPL",          /* license */
    PACKAGE,        /* package-name, usually the file archive name */
    "http://www.lemote.com" /* origin */
    )

static gboolean
plugin_init (GstPlugin *plugin)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!gst_element_register (plugin, "mfxenc",
                    GST_RANK_NONE, GST_TYPE_MFX_ENC))
      return FALSE;
    if (!gst_element_register (plugin, "mfxdec",
                    GST_RANK_NONE, GST_TYPE_MFX_DEC))
      return FALSE;
    if (!gst_element_register (plugin, "mfxtrans",
                    GST_RANK_NONE, GST_TYPE_MFX_TRANS))
      return FALSE;
    if (!gst_element_register (plugin, "mfxscl",
                    GST_RANK_NONE, GST_TYPE_MFX_SCL))
      return FALSE;

    return TRUE;
}

