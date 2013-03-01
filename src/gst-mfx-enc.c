/*
 ============================================================================
 Name        : gst-mfx-enc.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#include <string.h>
#include <mfxvideo.h>

#include "gst-mfx-enc.h"

#define GST_MFX_ENC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_ENC, GstMfxEncPrivate))

typedef struct _GstMfxEncPrivate GstMfxEncPrivate;

struct _GstMfxEncPrivate
{
    GstPad *sink_pad;
    GstPad *src_pad;
    GstCaps *src_pad_caps;

    gint width;
    gint height;
    guint uv_offset;
    guint32 buffer_length;

    mfxSession mfx_session;
    mfxVideoParam mfx_video_param;
};

static gboolean gst_mfx_enc_sink_pad_setcaps (GstPad *pad,
            GstCaps *caps);
static GstFlowReturn gst_mfx_enc_sink_pad_chain (GstPad *pad,
            GstBuffer *buf);

static const GstElementDetails gst_mfx_enc_details =
GST_ELEMENT_DETAILS (
            "MFX Encoder",
            "Codec/Encoder/Video",
            "MFX Video Encoder",
            "Heiher <admin@heiher.info>");

static GstStaticPadTemplate gst_mfx_enc_sink_template =
GST_STATIC_PAD_TEMPLATE (
            "sink",
            GST_PAD_SINK,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS ("video/x-raw-yuv, "
                "format = (fourcc) { NV12 }, "
                "framerate = (fraction) [0, MAX], "
                "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
            );

static GstStaticPadTemplate gst_mfx_enc_src_template =
GST_STATIC_PAD_TEMPLATE (
            "src",
            GST_PAD_SRC,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS ("video/x-h264, "
                "framerate = (fraction) [0/1, MAX], "
                "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ], "
                "stream-format = (string) { byte-stream, avc }, "
                "alignment = (string) { au }")
            );

GST_BOILERPLATE (GstMfxEnc, gst_mfx_enc,
            GstElement, GST_TYPE_ELEMENT);

static void
gst_mfx_enc_dispose (GObject *obj)
{
    GstMfxEnc *self = GST_MFX_ENC (obj);
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (priv->src_pad_caps) {
        gst_caps_unref (priv->src_pad_caps);
        priv->src_pad_caps = NULL;
    }

    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_enc_finalize (GObject *obj)
{
    GstMfxEnc *self = GST_MFX_ENC (obj);
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    MFXClose (priv->mfx_session);

    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GObject *
gst_mfx_enc_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_mfx_enc_constructed (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_mfx_enc_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_mfx_enc_sink_template));
    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_mfx_enc_src_template));
    gst_element_class_set_details (element_class, &gst_mfx_enc_details);
}

static void
gst_mfx_enc_class_init (GstMfxEncClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    obj_class->constructor = gst_mfx_enc_constructor;
    obj_class->constructed = gst_mfx_enc_constructed;
    obj_class->dispose = gst_mfx_enc_dispose;
    obj_class->finalize = gst_mfx_enc_finalize;

    g_type_class_add_private (klass, sizeof (GstMfxEncPrivate));
}

static void
gst_mfx_enc_init (GstMfxEnc *self,
            GstMfxEncClass *klass)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    mfxStatus s = MFX_ERR_NONE;
    mfxVersion ver;
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    ver.Minor = 1;
    ver.Major = 1;
    s = MFXInit (impl, &ver, &priv->mfx_session);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXInit failed(%d)!", s);
        goto fail;
    }

    s = MFXQueryIMPL (priv->mfx_session, &impl);
    if (MFX_ERR_NONE == s)
      g_debug ("MFXQueryIMPL -> %d", impl);
    s = MFXQueryVersion (priv->mfx_session, &ver);
    if (MFX_ERR_NONE == s)
      g_debug ("MFXQueryVersion -> %d, %d ---", ver.Major, ver.Minor);

    gst_element_create_all_pads (GST_ELEMENT (self));
    
    priv->sink_pad = gst_element_get_static_pad (
                GST_ELEMENT (self), "sink");
    priv->src_pad = gst_element_get_static_pad (
                GST_ELEMENT (self), "src");

    gst_pad_set_setcaps_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_enc_sink_pad_setcaps));
    gst_pad_set_chain_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_enc_sink_pad_chain));
fail:

    return;
}

static gboolean
gst_mfx_enc_sink_pad_setcaps (GstPad *pad, GstCaps *caps)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstStructure *structure = NULL;
    mfxStatus s = MFX_ERR_NONE;
    mfxVideoParam param;
    gint numerator = 0, denominator = 0;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!GST_CAPS_IS_SIMPLE (caps))
      goto fail;

    structure = gst_caps_get_structure (caps, 0);
    if (!structure)
      goto fail;
    if (!gst_structure_get (structure,
                    "width", G_TYPE_INT, &priv->width,
                    "height", G_TYPE_INT, &priv->height,
                    NULL))
      goto fail;
    if (!gst_structure_get_fraction (structure, "framerate",
                    &numerator, &denominator))
      goto fail;

    priv->uv_offset = priv->width * priv->height;
    memset (&priv->mfx_video_param, 0, sizeof (mfxVideoParam));
    priv->mfx_video_param.AsyncDepth = 0;
    priv->mfx_video_param.Protected = 0;
    priv->mfx_video_param.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    priv->mfx_video_param.mfx.CodecId = MFX_CODEC_AVC;
    priv->mfx_video_param.mfx.TargetKbps = 1024;
    priv->mfx_video_param.mfx.EncodedOrder = 0;
    priv->mfx_video_param.mfx.FrameInfo.Width = priv->width;
    priv->mfx_video_param.mfx.FrameInfo.Height = priv->height;
    priv->mfx_video_param.mfx.FrameInfo.FrameRateExtD = denominator;
    priv->mfx_video_param.mfx.FrameInfo.FrameRateExtN = numerator;
    priv->mfx_video_param.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    priv->mfx_video_param.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    priv->mfx_video_param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    priv->mfx_video_param.mfx.FrameInfo.CropX = 0;
    priv->mfx_video_param.mfx.FrameInfo.CropY = 0;
    priv->mfx_video_param.mfx.FrameInfo.CropW = priv->width;
    priv->mfx_video_param.mfx.FrameInfo.CropH = priv->height;
    s = MFXVideoENCODE_Init (priv->mfx_session, &priv->mfx_video_param);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoENCODE_Init failed(%d)!", s);
        goto fail;
    }

    memset (&param, 0, sizeof (param));
    s = MFXVideoENCODE_GetVideoParam (priv->mfx_session, &param);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoENCODE_GetVideoParam failed(%d)!", s);
        goto fail;
    }
    priv->buffer_length = sizeof (mfxU8) * 1024 * param.mfx.BufferSizeInKB;

    if (priv->src_pad_caps)
      gst_caps_unref (priv->src_pad_caps);
    structure = gst_structure_new ("video/x-h264",
                "width", G_TYPE_INT, priv->width,
                "height", G_TYPE_INT, priv->height,
                "framerate", GST_TYPE_FRACTION, numerator, denominator,
                "stream-format", G_TYPE_STRING, "avc",
                "alignment", G_TYPE_STRING, "au",
                NULL);
    priv->src_pad_caps = gst_caps_new_full (structure, NULL);

    return TRUE;

fail:

    return FALSE;
}

static GstFlowReturn
gst_mfx_enc_sink_pad_chain (GstPad *pad, GstBuffer *buf)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstBuffer *obuf = NULL;
    mfxStatus s = MFX_ERR_NONE;
    mfxFrameSurface1 frsf;
    mfxBitstream bs;
    mfxSyncPoint sp;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    memset (&frsf, 0, sizeof (frsf));
    memcpy (&frsf.Info, &priv->mfx_video_param.mfx.FrameInfo,
                sizeof (mfxFrameInfo));
    frsf.Data.Locked = 0;
    frsf.Data.Pitch = priv->width;
    frsf.Data.Y = GST_BUFFER_DATA (buf);
    frsf.Data.UV = GST_BUFFER_DATA (buf) + priv->uv_offset;
    frsf.Data.TimeStamp = GST_BUFFER_TIMESTAMP (buf);

    obuf = gst_buffer_new_and_alloc (priv->buffer_length);
    if (!obuf)
      goto fail;

    memset (&bs, 0, sizeof (bs));
    bs.MaxLength = priv->buffer_length;
    bs.Data = GST_BUFFER_DATA (obuf);

again:
    s = MFXVideoENCODE_EncodeFrameAsync (priv->mfx_session, NULL, &frsf, &bs, &sp);
    if (MFX_ERR_MORE_DATA == s)
      goto again;
    else if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoENCODE_EncodeFrameAsync failed(%d)!", s);
        gst_buffer_unref (obuf);
        goto fail;
    }

    GST_BUFFER_DURATION (obuf) = GST_BUFFER_DURATION (buf);
    gst_buffer_set_caps (obuf, priv->src_pad_caps);

    s = MFXVideoCORE_SyncOperation (priv->mfx_session, sp, -1);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoCORE_SyncOperation failed(%d)!", s);
        gst_buffer_unref (obuf);
        goto fail;
    }

    gst_buffer_unref (buf);
    GST_BUFFER_OFFSET (obuf) = bs.DataOffset;
    GST_BUFFER_SIZE (obuf) = bs.DataLength;
    GST_BUFFER_TIMESTAMP (obuf) = bs.TimeStamp;

    return gst_pad_push (priv->src_pad, obuf);

fail:

    return GST_FLOW_ERROR;
}

