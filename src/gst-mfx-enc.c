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
typedef struct _GstMfxEncTask GstMfxEncTask;

struct _GstMfxEncPrivate
{
    GstPad *sink_pad;
    GstPad *src_pad;
    GstCaps *src_pad_caps;
    GstMfxEncTask *task_pool;

    guint32 fs_buf_len;
    guint32 bs_buf_len;
    guint32 task_pool_len;

    GQueue task_queue;
    mfxSession mfx_session;
    mfxVideoParam mfx_video_param;
};

struct _GstMfxEncTask
{
    mfxFrameSurface1 surface;
    mfxBitstream bstream;
    mfxSyncPoint sp;

    GstClockTime duration;
};

static GstFlowReturn gst_mfx_enc_sync_tasks (GstMfxEnc *self, gboolean send);
static gint gst_mfx_enc_find_free_task (GstMfxEnc *self);
static GstFlowReturn gst_mfx_enc_get_free_task (GstMfxEnc *self, gint *tid);
static void gst_mfx_enc_flush_frames (GstMfxEnc *self, gboolean send);
static GstStateChangeReturn gst_mfx_enc_change_state (GstElement * element,
            GstStateChange transition);
static gboolean gst_mfx_enc_sink_pad_setcaps (GstPad *pad,
            GstCaps *caps);
static gboolean gst_mfx_enc_sink_pad_event (GstPad *pad, GstEvent *event);
static GstFlowReturn gst_mfx_enc_sink_pad_bufferalloc (GstPad *pad,
            guint64 offset, guint size, GstCaps *caps, GstBuffer **buf);
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
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_enc_finalize (GObject *obj)
{
    GstMfxEnc *self = GST_MFX_ENC (obj);
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_queue_clear (&priv->task_queue);

    if (priv->task_pool) {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            g_slice_free1 (priv->fs_buf_len,
                        priv->task_pool[i].surface.Data.MemId);
            g_slice_free1 (priv->bs_buf_len,
                        priv->task_pool[i].bstream.Data);
        }
        g_slice_free1 (sizeof (GstMfxEncTask) * priv->task_pool_len,
                    priv->task_pool);
        priv->task_pool_len = 0;
        priv->task_pool = NULL;
    }

    if (priv->src_pad_caps) {
        gst_caps_unref (priv->src_pad_caps);
        priv->src_pad_caps = NULL;
    }

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
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    obj_class->constructor = gst_mfx_enc_constructor;
    obj_class->constructed = gst_mfx_enc_constructed;
    obj_class->dispose = gst_mfx_enc_dispose;
    obj_class->finalize = gst_mfx_enc_finalize;

    element_class->change_state = gst_mfx_enc_change_state;

    g_type_class_add_private (klass, sizeof (GstMfxEncPrivate));
}

static void
gst_mfx_enc_init (GstMfxEnc *self,
            GstMfxEncClass *klass)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_queue_init (&priv->task_queue);

    gst_element_create_all_pads (GST_ELEMENT (self));
    
    priv->sink_pad = gst_element_get_static_pad (
                GST_ELEMENT (self), "sink");
    priv->src_pad = gst_element_get_static_pad (
                GST_ELEMENT (self), "src");

    gst_pad_set_setcaps_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_enc_sink_pad_setcaps));
    gst_pad_set_event_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_enc_sink_pad_event));
    gst_pad_set_bufferalloc_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_enc_sink_pad_bufferalloc));
    gst_pad_set_chain_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_enc_sink_pad_chain));
}

static GstFlowReturn
gst_mfx_enc_sync_tasks (GstMfxEnc *self, gboolean send)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstFlowReturn ret = GST_FLOW_OK;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    for (;;) {
        GstMfxEncTask *ht = NULL;
        mfxStatus hs = MFX_ERR_NONE;

        ht = g_queue_peek_head (&priv->task_queue);
        if (!ht)
          break;

        hs = MFXVideoCORE_SyncOperation (priv->mfx_session, ht->sp, 0);
        /* The async operation is ready, push to src pad */
        if (MFX_ERR_NONE == hs) {
            GstFlowReturn r = GST_FLOW_OK;
            GstBuffer *buffer = NULL;

            if (send) {
                r = gst_pad_alloc_buffer (priv->src_pad,
                            GST_BUFFER_OFFSET_NONE,
                            ht->bstream.DataLength,
                            priv->src_pad_caps,
                            &buffer);
                if (GST_FLOW_OK != r || NULL == buffer) {
                    g_critical ("Alloc buffer from src pad failed!");
                } else {
                    memcpy (GST_BUFFER_DATA (buffer),
                                ht->bstream.Data,
                                ht->bstream.DataLength);
                    if (G_LIKELY (0 == ht->bstream.DataOffset))
                      GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
                    else
                      GST_BUFFER_OFFSET (buffer) = ht->bstream.DataOffset;
                    GST_BUFFER_TIMESTAMP (buffer) = ht->bstream.TimeStamp;
                    GST_BUFFER_DURATION (buffer) = ht->duration;

                    ret = gst_pad_push (priv->src_pad, buffer);
                }
            }
            
            /* Unlock task's surface and pop from task queue */
            ht->sp = NULL;
            ht->surface.Data.Locked = 0;
            ht->bstream.DataOffset = 0;
            ht->bstream.DataLength = 0;
            g_queue_pop_head (&priv->task_queue);

            if (GST_FLOW_OK != ret)
              break;
        } else if (MFX_ERR_NONE < hs) {
            if (MFX_WRN_DEVICE_BUSY == hs)
              g_usleep (100);
        } else {
            /* Sync error, pop from task queue and drop this frame */
            ht->sp = NULL;
            ht->surface.Data.Locked = 0;
            ht->bstream.DataOffset = 0;
            ht->bstream.DataLength = 0;
            g_queue_pop_head (&priv->task_queue);
        }
    }

    return ret;
}

static gint
gst_mfx_enc_find_free_task (GstMfxEnc *self)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    gint i = 0, free = -1;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    /* Find free task */
    for (i=0; i<priv->task_pool_len; i++) {
        if (NULL == priv->task_pool[i].sp) {
            free = i;
            break;
        }
    }

    return free;
}

static GstFlowReturn
gst_mfx_enc_get_free_task (GstMfxEnc *self, gint *tid)
{
    GstFlowReturn ret = GST_FLOW_OK;
    gint free = -1;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_val_if_fail (NULL != tid, GST_FLOW_ERROR);

    for (;;) {

        free = gst_mfx_enc_find_free_task (self);
        if (-1 != free)
          break;

        /* Not found free task, Sync cached async operations */
        ret = gst_mfx_enc_sync_tasks (self, TRUE);
        if (GST_FLOW_OK != ret)
          break;
    }
    *tid = free;

    return ret;
}

static void
gst_mfx_enc_flush_frames (GstMfxEnc *self, gboolean send)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    gst_mfx_enc_sync_tasks (self, send);
}

static GstStateChangeReturn
gst_mfx_enc_change_state (GstElement * element,
            GstStateChange transition)
{
    GstMfxEnc *self = GST_MFX_ENC (element);
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    ret = parent_class->change_state (element, transition);
    if (GST_STATE_CHANGE_FAILURE == ret)
      goto out;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        {
            mfxStatus s = MFX_ERR_NONE;
            mfxVersion ver;
            mfxIMPL impl = MFX_IMPL_AUTO_ANY;

            ver.Minor = 1;
            ver.Major = 1;
            s = MFXInit (impl, &ver, &priv->mfx_session);
            if (MFX_ERR_NONE != s) {
                g_critical ("MFXInit failed(%d)!", s);
                ret = GST_STATE_CHANGE_FAILURE;
                goto out;
            }

            s = MFXQueryIMPL (priv->mfx_session, &impl);
            if (MFX_ERR_NONE == s)
              g_debug ("MFXQueryIMPL -> %d", impl);
            s = MFXQueryVersion (priv->mfx_session, &ver);
            if (MFX_ERR_NONE == s)
              g_debug ("MFXQueryVersion -> %d, %d",
                          ver.Major, ver.Minor);
        }
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_mfx_enc_flush_frames (self, FALSE);
        MFXVideoENCODE_Close (priv->mfx_session);
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        MFXClose (priv->mfx_session);
        break;
    default:
        break;
    }

out:
    return ret;
}

static gboolean
gst_mfx_enc_sink_pad_setcaps (GstPad *pad, GstCaps *caps)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstStructure *structure = NULL;
    mfxStatus s = MFX_ERR_NONE;
    mfxFrameAllocRequest req;
    gint width = 0, height = 0;
    gint numerator = 0, denominator = 0;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!GST_CAPS_IS_SIMPLE (caps))
      goto fail;

    if (priv->task_pool)
      gst_mfx_enc_flush_frames (self, TRUE);

    structure = gst_caps_get_structure (caps, 0);
    if (!structure)
      goto fail;
    if (!gst_structure_get (structure,
                    "width", G_TYPE_INT, &width,
                    "height", G_TYPE_INT, &height,
                    NULL))
      goto fail;
    if (!gst_structure_get_fraction (structure, "framerate",
                    &numerator, &denominator))
      goto fail;

    memset (&priv->mfx_video_param, 0, sizeof (mfxVideoParam));
    priv->mfx_video_param.AsyncDepth = 4;
    priv->mfx_video_param.Protected = 0;
    priv->mfx_video_param.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    priv->mfx_video_param.mfx.CodecId = MFX_CODEC_AVC;
    priv->mfx_video_param.mfx.CodecProfile = MFX_PROFILE_AVC_MAIN;
    priv->mfx_video_param.mfx.TargetKbps = 1024;
    priv->mfx_video_param.mfx.TargetUsage = MFX_TARGETUSAGE_BEST_SPEED;
    priv->mfx_video_param.mfx.EncodedOrder = 0;
    priv->mfx_video_param.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    priv->mfx_video_param.mfx.GopRefDist = 1;
    priv->mfx_video_param.mfx.FrameInfo.Width = width;
    priv->mfx_video_param.mfx.FrameInfo.Height = height;
    priv->mfx_video_param.mfx.FrameInfo.FrameRateExtD = denominator;
    priv->mfx_video_param.mfx.FrameInfo.FrameRateExtN = numerator;
    priv->mfx_video_param.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    priv->mfx_video_param.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    priv->mfx_video_param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    priv->mfx_video_param.mfx.FrameInfo.CropX = 0;
    priv->mfx_video_param.mfx.FrameInfo.CropY = 0;
    priv->mfx_video_param.mfx.FrameInfo.CropW = width;
    priv->mfx_video_param.mfx.FrameInfo.CropH = height;
    s = MFXVideoENCODE_Init (priv->mfx_session, &priv->mfx_video_param);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoENCODE_Init failed(%d)!", s);
        goto fail;
    }

    s = MFXVideoENCODE_GetVideoParam (priv->mfx_session, &priv->mfx_video_param);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoENCODE_GetVideoParam failed(%d)!", s);
        goto fail;
    }
    switch (priv->mfx_video_param.mfx.FrameInfo.FourCC) {
    case MFX_FOURCC_NV12:
        priv->fs_buf_len = width * height +
            (width>>1) * (height>>1) +
            (width>>1) * (height>>1);
        break;
    }
    priv->bs_buf_len = priv->mfx_video_param.mfx.BufferSizeInKB * 1024;

    s = MFXVideoENCODE_QueryIOSurf (priv->mfx_session,
                &priv->mfx_video_param, &req);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoENCODE_QueryIOSurf failed(%d)!", s);
        goto fail;
    }
    /* Free previous task pool */
    if (priv->task_pool) {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            g_slice_free1 (priv->fs_buf_len,
                        priv->task_pool[i].surface.Data.MemId);
            g_slice_free1 (priv->bs_buf_len,
                        priv->task_pool[i].bstream.Data);
        }
        g_slice_free1 (sizeof (GstMfxEncTask) * priv->task_pool_len,
                    priv->task_pool);
    }
    /* Alloc new task pool */
    priv->task_pool_len = req.NumFrameSuggested;
    priv->task_pool = g_slice_alloc0 (sizeof (GstMfxEncTask) * priv->task_pool_len);
    {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            memcpy (&priv->task_pool[i].surface.Info,
                        &priv->mfx_video_param.mfx.FrameInfo,
                        sizeof (mfxFrameInfo));
            /* Alloc buffer for input: mfxFrameSurface1 */
            priv->task_pool[i].surface.Data.MemId =
                g_slice_alloc0 (priv->fs_buf_len);
            switch (priv->mfx_video_param.mfx.FrameInfo.FourCC) {
            case MFX_FOURCC_NV12:
                priv->task_pool[i].surface.Data.Y =
                    priv->task_pool[i].surface.Data.MemId;
                priv->task_pool[i].surface.Data.U =
                    priv->task_pool[i].surface.Data.Y +
                    width * height;
                priv->task_pool[i].surface.Data.V =
                    priv->task_pool[i].surface.Data.U + 1;
                priv->task_pool[i].surface.Data.Pitch = width;
                break;
            }
            /* Alloc buffer for output: mfxBitstream */
            priv->task_pool[i].bstream.Data =
                g_slice_alloc0 (priv->bs_buf_len);
            priv->task_pool[i].bstream.MaxLength = priv->bs_buf_len;
        }
    }

    if (priv->src_pad_caps)
      gst_caps_unref (priv->src_pad_caps);
    structure = gst_structure_new ("video/x-h264",
                "width", G_TYPE_INT, width,
                "height", G_TYPE_INT, height,
                "framerate", GST_TYPE_FRACTION, numerator, denominator,
                "stream-format", G_TYPE_STRING, "avc",
                "alignment", G_TYPE_STRING, "au",
                NULL);
    priv->src_pad_caps = gst_caps_new_full (structure, NULL);

    return TRUE;

fail:

    return FALSE;
}

static gboolean
gst_mfx_enc_sink_pad_event (GstPad *pad, GstEvent *event)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
        gst_mfx_enc_flush_frames (self, TRUE);
        break;
    default:
        break;
    }

    return gst_pad_push_event (priv->src_pad, event);
}

static GstFlowReturn
gst_mfx_enc_sink_pad_bufferalloc (GstPad *pad, guint64 offset,
            guint size, GstCaps *caps, GstBuffer **buf)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstFlowReturn ret = GST_FLOW_OK;
    gint free = -1;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    /* No task pool, alloc a normal buffer */
    if (G_UNLIKELY (!priv->task_pool)) {
        *buf = gst_buffer_new_and_alloc (size);
        if (!*buf)
          return GST_FLOW_ERROR;
        GST_BUFFER_OFFSET (*buf) = offset;
        gst_buffer_set_caps (*buf, caps);

        return GST_FLOW_OK;
    }

    if (G_UNLIKELY (size != priv->fs_buf_len))
      g_assert_not_reached ();

    ret = gst_mfx_enc_get_free_task (self, &free);
    if (GST_FLOW_OK != ret)
      return ret;

    *buf = gst_buffer_new ();
    if (!*buf)
      return GST_FLOW_ERROR;
    GST_BUFFER_DATA (*buf) = priv->task_pool[free].surface.Data.MemId;
    GST_BUFFER_SIZE (*buf) = priv->fs_buf_len;
    if (G_UNLIKELY (GST_BUFFER_OFFSET_NONE != offset))
      g_warning ("Request alloc buffer's offset isn't NONE!");
    GST_BUFFER_OFFSET (*buf) = GST_BUFFER_OFFSET_NONE;
    gst_buffer_set_caps (*buf, caps);

    return GST_FLOW_OK;
}

static GstFlowReturn
gst_mfx_enc_sink_pad_chain (GstPad *pad, GstBuffer *buf)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstMfxEncTask *task = NULL;
    GstFlowReturn ret = GST_FLOW_OK;
    gint i = 0, tid = -1;
    gboolean retry = TRUE, mcpy = FALSE;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    /* Find buf's owner: task */
    for (i=0; i<priv->task_pool_len; i++) {
        if (priv->task_pool[i].surface.Data.MemId ==
                    GST_BUFFER_DATA (buf)) {
            tid = i;
            break;
        }
    }

    /* Not found in task pool, may be is first alloced buffer.
     * Get a free task to handle it and set mcpy = TRUE.
     */
    if (-1 == tid) {
        ret = gst_mfx_enc_get_free_task (self, &tid);
        if (GST_FLOW_OK != ret)
          goto fail;
        mcpy = TRUE;
    }

    task = &priv->task_pool[tid];

    /* Input: mfxFrameSurface1 */
    if (G_UNLIKELY (mcpy)) {
        if (priv->fs_buf_len != GST_BUFFER_SIZE (buf))
          g_assert_not_reached ();
        memcpy (task->surface.Data.MemId,
                    GST_BUFFER_DATA (buf), priv->fs_buf_len);
    }
    task->surface.Data.TimeStamp = GST_BUFFER_TIMESTAMP (buf);

    /* Output: save duration */
    task->duration = GST_BUFFER_DURATION (buf);

    /* Free input buffer: GstBuffer */
    gst_buffer_unref (buf);

    /* Commit the task to MFX Encoder */
    do {
        mfxStatus s = MFX_ERR_NONE;

        s = MFXVideoENCODE_EncodeFrameAsync (priv->mfx_session, NULL,
                    &task->surface, &task->bstream, &task->sp);

        if (MFX_ERR_NONE < s && !task->sp) {
            if (MFX_WRN_DEVICE_BUSY == s)
              g_usleep (100);
            retry = TRUE;
        } else if (MFX_ERR_NONE < s && task->sp) {
            retry = FALSE;
        } else if (MFX_ERR_MORE_DATA == s) {
            retry = TRUE;
        } else if (MFX_ERR_NONE != s) {
            g_critical ("MFXVideoENCODE_EncodeFrameAsync failed(%d)!", s);
            ret = GST_FLOW_ERROR;
            goto fail;
        } else {
            retry = FALSE;
        }
    } while (retry);

    /* Push task to task queue */
    g_queue_push_tail (&priv->task_queue, task);

    return ret;

fail:

    return ret;
}

