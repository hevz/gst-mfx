/*
 ============================================================================
 Name        : gst-mfx-enc.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2013 everyone.
 Description : 
 ============================================================================
 */

#include "gst-mfx-enc.h"

#define GST_MFX_ENC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_ENC, GstMfxEncPrivate))
#define GST_CAT_DEFAULT (mfxenc_debug)

typedef struct _GstMfxEncPrivate GstMfxEncPrivate;
typedef struct _GstMfxEncTask GstMfxEncTask;

struct _GstMfxEncPrivate
{
    GstPad *sink_pad;
    GstPad *src_pad;
    GstCaps *src_pad_caps;
    GstMfxEncTask *task_pool;
    GstMfxEncTask *task_curr;

    guint32 fs_buf_len;
    guint32 bs_buf_len;
    guint32 task_pool_len;

    GMutex exec_mutex;
    GCond exec_cond;
    GQueue exec_queue;
    GMutex idle_mutex;
    GCond idle_cond;
    GQueue idle_queue;

    mfxVideoParam mfx_video_param;

    GstFlowReturn src_pad_ret;
    gboolean src_pad_push_status;
};

struct _GstMfxEncTask
{
    mfxFrameSurface1 input;
    mfxBitstream output;
    mfxSyncPoint sp;

    GstClockTime duration;
};

static void gst_mfx_enc_push_exec_task (GstMfxEnc *self, GstMfxEncTask *task);
static GstMfxEncTask * gst_mfx_enc_pop_exec_task (GstMfxEnc *self);
static void gst_mfx_enc_push_idle_task (GstMfxEnc *self, GstMfxEncTask *task);
static GstMfxEncTask * gst_mfx_enc_pop_idle_task (GstMfxEnc *self);
static GstFlowReturn gst_mfx_enc_sync_task (GstMfxEnc *self, gboolean send);
static void gst_mfx_enc_flush_frames (GstMfxEnc *self, gboolean send);
static GstStateChangeReturn gst_mfx_enc_change_state (GstElement *element,
            GstStateChange transition);
static gboolean gst_mfx_enc_sink_pad_setcaps (GstPad *pad,
            GstCaps *caps);
static gboolean gst_mfx_enc_sink_pad_event (GstPad *pad, GstEvent *event);
static GstFlowReturn gst_mfx_enc_sink_pad_bufferalloc (GstPad *pad,
            guint64 offset, guint size, GstCaps *caps, GstBuffer **buf);
static GstFlowReturn gst_mfx_enc_sink_pad_chain (GstPad *pad,
            GstBuffer *buf);
static gboolean gst_mfx_enc_src_pad_activatepush (GstPad *pad,
            gboolean activate);
static void gst_mfx_enc_src_pad_task_handler (gpointer data);

GST_DEBUG_CATEGORY_STATIC (mfxenc_debug);

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
            GstMfxBase, GST_TYPE_MFX_BASE);

static void
gst_mfx_enc_dispose (GObject *obj)
{
    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_enc_finalize (GObject *obj)
{
    GstMfxEnc *self = GST_MFX_ENC (obj);
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_queue_clear (&priv->idle_queue);
    g_cond_clear (&priv->idle_cond);
    g_mutex_clear (&priv->idle_mutex);
    g_queue_clear (&priv->exec_queue);
    g_cond_clear (&priv->exec_cond);
    g_mutex_clear (&priv->exec_mutex);

    if (priv->task_pool) {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            g_slice_free1 (priv->fs_buf_len,
                        priv->task_pool[i].input.Data.MemId);
            g_slice_free1 (priv->bs_buf_len,
                        priv->task_pool[i].output.Data);
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
    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_mfx_enc_constructed (GObject *obj)
{
    guint16 io_pattern = 0;

    G_OBJECT_CLASS (parent_class)->constructed (obj);

    /* Default IOPattern for encoder */
    g_object_get (obj, "io-pattern", &io_pattern, NULL);
    if (0 == io_pattern)
      g_object_set (obj,
                  "io-pattern", MFX_IOPATTERN_IN_SYSTEM_MEMORY,
                  NULL);
}

static void
gst_mfx_enc_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_mfx_enc_sink_template));
    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_mfx_enc_src_template));
    gst_element_class_set_details_simple (element_class,
                "MFX Encoder",
                "Codec/Encoder/Video",
                "MFX Video Encoder",
                "Heiher <admin@heiher.info>");
}

static void
gst_mfx_enc_class_init (GstMfxEncClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    obj_class->constructor = gst_mfx_enc_constructor;
    obj_class->constructed = gst_mfx_enc_constructed;
    obj_class->dispose = gst_mfx_enc_dispose;
    obj_class->finalize = gst_mfx_enc_finalize;

    element_class->change_state = gst_mfx_enc_change_state;

    g_type_class_add_private (klass, sizeof (GstMfxEncPrivate));

    GST_DEBUG_CATEGORY_INIT (mfxenc_debug, "mfxenc", 0, "MFX Encoder");
}

static void
gst_mfx_enc_init (GstMfxEnc *self,
            GstMfxEncClass *klass)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_mutex_init (&priv->exec_mutex);
    g_cond_init (&priv->exec_cond);
    g_queue_init (&priv->exec_queue);
    g_mutex_init (&priv->idle_mutex);
    g_cond_init (&priv->idle_cond);
    g_queue_init (&priv->idle_queue);

    priv->src_pad_ret = GST_FLOW_OK;

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

    gst_pad_set_activatepush_function (priv->src_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_enc_src_pad_activatepush));
}

static void
gst_mfx_enc_push_exec_task (GstMfxEnc *self, GstMfxEncTask *task)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_return_if_fail (NULL != task);

    g_mutex_lock (&priv->exec_mutex);
    g_queue_push_tail (&priv->exec_queue, task);
    g_cond_signal (&priv->exec_cond);
    g_mutex_unlock (&priv->exec_mutex);
}

static GstMfxEncTask *
gst_mfx_enc_pop_exec_task (GstMfxEnc *self)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstMfxEncTask *task = NULL;

    g_mutex_lock (&priv->exec_mutex);
    while (priv->src_pad_push_status &&
                !g_queue_peek_head (&priv->exec_queue))
      g_cond_wait (&priv->exec_cond, &priv->exec_mutex);
    task = g_queue_pop_head (&priv->exec_queue);
    g_mutex_unlock (&priv->exec_mutex);

    return task;
}

static void
gst_mfx_enc_push_idle_task (GstMfxEnc *self, GstMfxEncTask *task)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    g_return_if_fail (NULL != task);

    task->sp = NULL;
    task->input.Data.Locked = 0;
    task->output.DataOffset = 0;
    task->output.DataLength = 0;

    g_mutex_lock (&priv->idle_mutex);
    g_queue_push_tail (&priv->idle_queue, task);
    g_cond_signal (&priv->idle_cond);
    g_mutex_unlock (&priv->idle_mutex);
}

static GstMfxEncTask *
gst_mfx_enc_pop_idle_task (GstMfxEnc *self)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstMfxEncTask *task = NULL;

    g_mutex_lock (&priv->idle_mutex);
    while (priv->src_pad_push_status &&
                !g_queue_peek_head (&priv->idle_queue))
      g_cond_wait (&priv->idle_cond, &priv->idle_mutex);
    task = g_queue_pop_head (&priv->idle_queue);
    g_mutex_unlock (&priv->idle_mutex);

    return task;
}

static GstFlowReturn
gst_mfx_enc_sync_task (GstMfxEnc *self, gboolean send)
{
    GstMfxBase *parent = GST_MFX_BASE (self);
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstFlowReturn ret = GST_FLOW_OK;
    GstMfxEncTask *task = NULL;
    mfxStatus s = MFX_ERR_NONE;

    /* Pop task from exec queue */
    task = gst_mfx_enc_pop_exec_task (self);
    if (G_UNLIKELY (NULL == task))
      return GST_FLOW_UNEXPECTED;
    do {
        s = MFXVideoCORE_SyncOperation (parent->mfx_session,
                    task->sp, G_MAXUINT32);
        /* The async operation is ready, push to src pad */
        if (MFX_ERR_NONE == s) {
            GstBuffer *buffer = NULL;

            if (send) {
                ret = gst_pad_alloc_buffer (priv->src_pad,
                            GST_BUFFER_OFFSET_NONE,
                            task->output.DataLength,
                            priv->src_pad_caps,
                            &buffer);
                if (GST_FLOW_OK == ret && NULL != buffer) {
                    memcpy (GST_BUFFER_DATA (buffer),
                                task->output.Data,
                                task->output.DataLength);
                    if (G_LIKELY (0 == task->output.DataOffset))
                      GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
                    else
                      GST_BUFFER_OFFSET (buffer) = task->output.DataOffset;
                    GST_BUFFER_TIMESTAMP (buffer) = task->output.TimeStamp;
                    GST_BUFFER_DURATION (buffer) = task->duration;

                    ret = gst_pad_push (priv->src_pad, buffer);
                }
            }
            
            /* Push task to idle queue */
            gst_mfx_enc_push_idle_task (self, task);
        } else if (MFX_ERR_NONE > s) {
            /* Push task to idle queue */
            gst_mfx_enc_push_idle_task (self, task);
        }
    } while (MFX_ERR_NONE < s);

    return ret;
}

static void
gst_mfx_enc_flush_frames (GstMfxEnc *self, gboolean send)
{
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    while (g_queue_peek_head (&priv->exec_queue))
      gst_mfx_enc_sync_task (self, send);
}

static GstStateChangeReturn
gst_mfx_enc_change_state (GstElement *element,
            GstStateChange transition)
{
    GstMfxEnc *self = GST_MFX_ENC (element);
    GstMfxBase *parent = GST_MFX_BASE (self);
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
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_mfx_enc_flush_frames (self, FALSE);
        MFXVideoENCODE_Close (parent->mfx_session);
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
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

static gboolean
gst_mfx_enc_sink_pad_setcaps (GstPad *pad, GstCaps *caps)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstMfxBase *parent = GST_MFX_BASE (self);
    GstStructure *structure = NULL;
    mfxStatus s = MFX_ERR_NONE;
    mfxFrameAllocRequest req;
    gint width = 0, height = 0;
    gint numerator = 0, denominator = 0;

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
    g_object_get (self,
                "async-depth", &priv->mfx_video_param.AsyncDepth,
                "protected", &priv->mfx_video_param.Protected,
                "io-pattern", &priv->mfx_video_param.IOPattern,
                NULL);
    priv->mfx_video_param.mfx.CodecId = MFX_CODEC_AVC;
    priv->mfx_video_param.mfx.CodecProfile = MFX_PROFILE_AVC_MAIN;
    priv->mfx_video_param.mfx.TargetKbps = 1024;
    priv->mfx_video_param.mfx.TargetUsage = MFX_TARGETUSAGE_BEST_SPEED;
    priv->mfx_video_param.mfx.EncodedOrder = 0;
    priv->mfx_video_param.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    priv->mfx_video_param.mfx.GopPicSize = 24;
    priv->mfx_video_param.mfx.GopRefDist = 1;
    priv->mfx_video_param.mfx.IdrInterval = 0;
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
    s = MFXVideoENCODE_Init (parent->mfx_session, &priv->mfx_video_param);
    if (MFX_ERR_NONE != s) {
        GST_ERROR ("MFXVideoENCODE_Init failed(%d)!", s);
        goto fail;
    }

    s = MFXVideoENCODE_GetVideoParam (parent->mfx_session, &priv->mfx_video_param);
    if (MFX_ERR_NONE != s) {
        GST_ERROR ("MFXVideoENCODE_GetVideoParam failed(%d)!", s);
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

    s = MFXVideoENCODE_QueryIOSurf (parent->mfx_session,
                &priv->mfx_video_param, &req);
    if (MFX_ERR_NONE != s) {
        GST_ERROR ("MFXVideoENCODE_QueryIOSurf failed(%d)!", s);
        goto fail;
    }
    /* Free previous task pool */
    if (priv->task_pool) {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            g_slice_free1 (priv->fs_buf_len,
                        priv->task_pool[i].input.Data.MemId);
            g_slice_free1 (priv->bs_buf_len,
                        priv->task_pool[i].output.Data);
        }
        g_slice_free1 (sizeof (GstMfxEncTask) * priv->task_pool_len,
                    priv->task_pool);

        while (g_queue_pop_head (&priv->exec_queue));
        while (g_queue_pop_head (&priv->idle_queue));
    }
    /* Alloc new task pool */
    priv->task_pool_len = req.NumFrameSuggested;
    priv->task_pool = g_slice_alloc0 (sizeof (GstMfxEncTask) * priv->task_pool_len);
    {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            GstMfxEncTask *task = &priv->task_pool[i];

            /* Set input frame info */
            memcpy (&task->input.Info,
                        &priv->mfx_video_param.mfx.FrameInfo,
                        sizeof (mfxFrameInfo));
            /* Alloc buffer for input: mfxFrameSurface1 */
            task->input.Data.MemId = g_slice_alloc0 (priv->fs_buf_len);
            switch (priv->mfx_video_param.mfx.FrameInfo.FourCC) {
            case MFX_FOURCC_NV12:
                task->input.Data.Y = task->input.Data.MemId;
                task->input.Data.U = task->input.Data.Y + width * height;
                task->input.Data.V = task->input.Data.U + 1;
                task->input.Data.Pitch = width;
                break;
            }
            /* Alloc buffer for output: mfxBitstream */
            task->output.Data = g_slice_alloc0 (priv->bs_buf_len);
            task->output.MaxLength = priv->bs_buf_len;

            /* Push task to idle queue */
            gst_mfx_enc_push_idle_task (self, task);
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
    GstMfxEncTask *task = NULL;

    if (G_UNLIKELY (GST_FLOW_OK != priv->src_pad_ret))
      return priv->src_pad_ret;

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

    /* Pop task from idle queue */
    task = gst_mfx_enc_pop_idle_task (self);
    if (NULL == task)
      return GST_FLOW_ERROR;

    *buf = gst_buffer_new ();
    if (!*buf)
      return GST_FLOW_ERROR;
    GST_BUFFER_DATA (*buf) = task->input.Data.MemId;
    GST_BUFFER_SIZE (*buf) = priv->fs_buf_len;
    GST_BUFFER_OFFSET (*buf) = GST_BUFFER_OFFSET_NONE;
    gst_buffer_set_caps (*buf, caps);
    /* Save the task in task_curr */
    priv->task_curr = task;

    return GST_FLOW_OK;
}

static GstFlowReturn
gst_mfx_enc_sink_pad_chain (GstPad *pad, GstBuffer *buf)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    GstMfxBase *parent = GST_MFX_BASE (self);
    GstMfxEncTask *task = NULL;
    GstFlowReturn ret = GST_FLOW_OK;
    gboolean retry = TRUE, mcpy = FALSE;

    /* Guess the input buf is in task_curr */
    if (priv->task_curr && GST_BUFFER_DATA (buf) ==
                priv->task_curr->input.Data.MemId) {
        task = priv->task_curr;
        priv->task_curr = NULL;
    } else {
        gint i = 0;
        /* Oh, is wrong! find buf's owner: task */
        for (i=0; i<priv->task_pool_len; i++) {
            if (priv->task_pool[i].input.Data.MemId ==
                        GST_BUFFER_DATA (buf)) {
                task = &priv->task_pool[i];
                break;
            }
        }
    }

    /* Not found in task pool, may be is first alloced buffer.
     * Get a idle task to handle it and set mcpy = TRUE.
     */
    if (NULL == task) {
        /* Pop task from idle queue */
        task = gst_mfx_enc_pop_idle_task (self);
        if (NULL == task)
          goto fail;
        mcpy = TRUE;
    }

    /* Input: mfxFrameSurface1 */
    if (G_UNLIKELY (mcpy)) {
        if (priv->fs_buf_len != GST_BUFFER_SIZE (buf))
          g_assert_not_reached ();
        memcpy (task->input.Data.MemId,
                    GST_BUFFER_DATA (buf), priv->fs_buf_len);
    }
    task->input.Data.TimeStamp = GST_BUFFER_TIMESTAMP (buf);

    /* Output: save duration */
    task->duration = GST_BUFFER_DURATION (buf);

    /* Free input buffer: GstBuffer */
    gst_buffer_unref (buf);

    /* Commit the task to MFX Encoder */
    do {
        mfxStatus s = MFX_ERR_NONE;

        s = MFXVideoENCODE_EncodeFrameAsync (parent->mfx_session, NULL,
                    &task->input, &task->output, &task->sp);

        if (MFX_ERR_NONE < s && !task->sp) {
            if (MFX_WRN_DEVICE_BUSY == s)
              g_usleep (100);
            retry = TRUE;
        } else if (MFX_ERR_NONE < s && task->sp) {
            retry = FALSE;
        } else if (MFX_ERR_MORE_DATA == s) {
            retry = TRUE;
        } else if (MFX_ERR_NONE != s) {
            GST_ERROR ("MFXVideoENCODE_EncodeFrameAsync failed(%d)!", s);
            ret = GST_FLOW_ERROR;
            goto fail;
        } else {
            retry = FALSE;
        }
    } while (retry);

    /* Push task to exec queue */
    gst_mfx_enc_push_exec_task (self, task);

    return ret;

fail:

    return ret;
}

static gboolean
gst_mfx_enc_src_pad_activatepush (GstPad *pad, gboolean activate)
{
    GstMfxEnc *self = GST_MFX_ENC (GST_OBJECT_PARENT (pad));
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);
    gboolean ret = TRUE;

    if (activate) {
        priv->src_pad_push_status = TRUE;
        ret = gst_pad_start_task (priv->src_pad,
                    gst_mfx_enc_src_pad_task_handler, self);
    } else {
        /* Send a quit signal to task thread */
        g_mutex_lock (&priv->exec_mutex);
        priv->src_pad_push_status = FALSE;
        g_cond_signal (&priv->exec_cond);
        g_mutex_unlock (&priv->exec_mutex);
        ret = gst_pad_stop_task (priv->src_pad);
    }

    return ret;
}

static void
gst_mfx_enc_src_pad_task_handler (gpointer data)
{
    GstMfxEnc *self = GST_MFX_ENC (data);
    GstMfxEncPrivate *priv = GST_MFX_ENC_GET_PRIVATE (self);

    priv->src_pad_ret = gst_mfx_enc_sync_task (self, TRUE);
    if (G_UNLIKELY (GST_FLOW_OK != priv->src_pad_ret))
      gst_pad_pause_task (priv->src_pad);
}

