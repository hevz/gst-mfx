/*
 ============================================================================
 Name        : gst-mfx-scl.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#include <string.h>
#include <mfxvideo.h>

#include "gst-mfx-scl.h"

#define GST_MFX_SCL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MFX_SCL, GstMfxSclPrivate))

enum
{
    PROP_ZERO,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPERTIES
};

typedef struct _GstMfxSclPrivate GstMfxSclPrivate;
typedef struct _GstMfxSclTask GstMfxSclTask;

struct _GstMfxSclPrivate
{
    gint width;
    gint height;

    GstPad *sink_pad;
    GstPad *src_pad;
    GstCaps *src_pad_caps;
    GstMfxSclTask *task_pool;

    guint32 in_buf_len;
    guint32 out_buf_len;
    guint32 task_pool_len;

    GQueue task_queue;
    mfxSession mfx_session;
    mfxVideoParam mfx_video_param;
};

struct _GstMfxSclTask
{
    gint id;

    mfxFrameSurface1 input;
    mfxFrameSurface1 output;
    mfxSyncPoint sp;

    GstClockTime duration;
};

static GstFlowReturn gst_mfx_scl_sync_tasks (GstMfxScl *self,
            gboolean send, gint *tid);
static gint gst_mfx_scl_find_free_task (GstMfxScl *self);
static GstFlowReturn gst_mfx_scl_get_free_task (GstMfxScl *self, gint *tid);
static void gst_mfx_scl_flush_frames (GstMfxScl *self, gboolean send);
static GstStateChangeReturn gst_mfx_scl_change_state (GstElement * element,
            GstStateChange transition);
static gboolean gst_mfx_scl_sink_pad_setcaps (GstPad *pad,
            GstCaps *caps);
static gboolean gst_mfx_scl_sink_pad_event (GstPad *pad, GstEvent *event);
static GstFlowReturn gst_mfx_scl_sink_pad_bufferalloc (GstPad *pad,
            guint64 offset, guint size, GstCaps *caps, GstBuffer **buf);
static GstFlowReturn gst_mfx_scl_sink_pad_chain (GstPad *pad,
            GstBuffer *buf);

static const GstElementDetails gst_mfx_scl_details =
GST_ELEMENT_DETAILS (
            "MFX Scaler",
            "Filter/Converter/Video/Scaler",
            "MFX Video Scaler",
            "Heiher <admin@heiher.info>");

static GstStaticPadTemplate gst_mfx_scl_sink_template =
GST_STATIC_PAD_TEMPLATE (
            "sink",
            GST_PAD_SINK,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS ("video/x-raw-yuv, "
                "format = (fourcc) { NV12 }, "
                "framerate = (fraction) [0, MAX], "
                "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
            );

static GstStaticPadTemplate gst_mfx_scl_src_template =
GST_STATIC_PAD_TEMPLATE (
            "src",
            GST_PAD_SRC,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS ("video/x-raw-yuv, "
                "format = (fourcc) { NV12 }, "
                "framerate = (fraction) [0, MAX], "
                "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
            );

GST_BOILERPLATE (GstMfxScl, gst_mfx_scl,
            GstElement, GST_TYPE_ELEMENT);

static void
gst_mfx_scl_dispose (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mfx_scl_finalize (GObject *obj)
{
    GstMfxScl *self = GST_MFX_SCL (obj);
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_queue_clear (&priv->task_queue);

    if (priv->task_pool) {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            g_slice_free1 (priv->in_buf_len,
                        priv->task_pool[i].input.Data.MemId);
            g_slice_free1 (priv->out_buf_len,
                        priv->task_pool[i].output.Data.MemId);
        }
        g_slice_free1 (sizeof (GstMfxSclTask) * priv->task_pool_len,
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
gst_mfx_scl_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_mfx_scl_constructed (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_mfx_scl_base_init (gpointer klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_mfx_scl_sink_template));
    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_mfx_scl_src_template));
    gst_element_class_set_details (element_class, &gst_mfx_scl_details);
}

static void
gst_mfx_scl_class_init (GstMfxSclClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    obj_class->constructor = gst_mfx_scl_constructor;
    obj_class->constructed = gst_mfx_scl_constructed;
    obj_class->dispose = gst_mfx_scl_dispose;
    obj_class->finalize = gst_mfx_scl_finalize;

    element_class->change_state = gst_mfx_scl_change_state;

    g_type_class_add_private (klass, sizeof (GstMfxSclPrivate));
}

static void
gst_mfx_scl_init (GstMfxScl *self,
            GstMfxSclClass *klass)
{
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_queue_init (&priv->task_queue);

    gst_element_create_all_pads (GST_ELEMENT (self));
    
    priv->sink_pad = gst_element_get_static_pad (
                GST_ELEMENT (self), "sink");
    priv->src_pad = gst_element_get_static_pad (
                GST_ELEMENT (self), "src");

    gst_pad_set_setcaps_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_scl_sink_pad_setcaps));
    gst_pad_set_event_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_scl_sink_pad_event));
    gst_pad_set_bufferalloc_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_scl_sink_pad_bufferalloc));
    gst_pad_set_chain_function (priv->sink_pad,
                GST_DEBUG_FUNCPTR (gst_mfx_scl_sink_pad_chain));
}

static GstFlowReturn
gst_mfx_scl_sync_tasks (GstMfxScl *self, gboolean send, gint *tid)
{
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);
    GstFlowReturn ret = GST_FLOW_OK;
    gint ftid = -1;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    for (;;) {
        GstMfxSclTask *ht = NULL;
        mfxStatus hs = MFX_ERR_NONE;

        ht = g_queue_peek_head (&priv->task_queue);
        if (!ht) {
            if (tid)
              *tid = ftid;
            break;
        }

        hs = MFXVideoCORE_SyncOperation (priv->mfx_session, ht->sp, 0);
        /* The async operation is ready, push to src pad */
        if (MFX_ERR_NONE == hs) {
            GstFlowReturn r = GST_FLOW_OK;
            GstBuffer *buffer = NULL;

            if (send) {
                r = gst_pad_alloc_buffer (priv->src_pad,
                            GST_BUFFER_OFFSET_NONE,
                            priv->out_buf_len,
                            priv->src_pad_caps,
                            &buffer);
                if (GST_FLOW_OK != r || NULL == buffer) {
                    g_critical ("Alloc buffer from src pad failed!");
                } else {
                    memcpy (GST_BUFFER_DATA (buffer),
                                ht->output.Data.MemId,
                                priv->out_buf_len);
                    GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
                    GST_BUFFER_TIMESTAMP (buffer) = ht->output.Data.TimeStamp;
                    GST_BUFFER_DURATION (buffer) = ht->duration;

                    ret = gst_pad_push (priv->src_pad, buffer);
                }
            }
            
            /* Unlock task's surface and pop from task queue */
            ht->sp = NULL;
            ht->input.Data.Locked = 0;
            ht->output.Data.Locked = 0;
            g_queue_pop_head (&priv->task_queue);
            ftid = ht->id;

            if (GST_FLOW_OK != ret)
              break;
        } else if (MFX_ERR_NONE < hs) {
            /* Have an exception, one task has been freed, return */
            if (tid && -1 != ftid) {
                *tid = ftid;
                break;
            }

            if (MFX_WRN_DEVICE_BUSY == hs)
              g_usleep (100);
        } else {
            /* Sync error, pop from task queue and drop this frame */
            ht->sp = NULL;
            ht->input.Data.Locked = 0;
            ht->output.Data.Locked = 0;
            g_queue_pop_head (&priv->task_queue);

            /* Have an exception, current task is freed, return */
            if (tid) {
                *tid = ht->id;
                break;
            }
        }
    }

    return ret;
}

static gint
gst_mfx_scl_find_free_task (GstMfxScl *self)
{
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);
    gint i = 0, free = -1;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    /* Find free task */
    for (i=0; i<priv->task_pool_len; i++) {
        if (NULL == priv->task_pool[i].sp) {
            free = i; /* i == task id */
            break;
        }
    }

    return free;
}

static GstFlowReturn
gst_mfx_scl_get_free_task (GstMfxScl *self, gint *tid)
{
    GstFlowReturn ret = GST_FLOW_OK;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_val_if_fail (NULL != tid, GST_FLOW_ERROR);

    *tid = gst_mfx_scl_find_free_task (self);
    if (-1 == *tid) {
        /* Not found free task, Sync cached async operations */
        ret = gst_mfx_scl_sync_tasks (self, TRUE, tid);
    }

    return ret;
}

static void
gst_mfx_scl_flush_frames (GstMfxScl *self, gboolean send)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    gst_mfx_scl_sync_tasks (self, send, NULL);
}

static GstStateChangeReturn
gst_mfx_scl_change_state (GstElement * element,
            GstStateChange transition)
{
    GstMfxScl *self = GST_MFX_SCL (element);
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);
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
        gst_mfx_scl_flush_frames (self, FALSE);
        MFXVideoVPP_Close (priv->mfx_session);
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
gst_mfx_scl_sink_pad_setcaps (GstPad *pad, GstCaps *caps)
{
    GstMfxScl *self = GST_MFX_SCL (GST_OBJECT_PARENT (pad));
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);
    GstStructure *structure = NULL;
    mfxStatus s = MFX_ERR_NONE;
    mfxFrameAllocRequest reqs[2];
    gint width = 0, height = 0;
    gint numerator = 0, denominator = 0;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!GST_CAPS_IS_SIMPLE (caps))
      goto fail;

    if (priv->task_pool)
      gst_mfx_scl_flush_frames (self, TRUE);

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

    if (0 == priv->width || 0 == priv->height) {
        priv->width = width;
        priv->height = height;
    }

    memset (&priv->mfx_video_param, 0, sizeof (mfxVideoParam));
    priv->mfx_video_param.AsyncDepth = 0;
    priv->mfx_video_param.Protected = 0;
    priv->mfx_video_param.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY |
                                    MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    priv->mfx_video_param.vpp.In.Width = width;
    priv->mfx_video_param.vpp.In.Height = height;
    priv->mfx_video_param.vpp.In.FrameRateExtD = denominator;
    priv->mfx_video_param.vpp.In.FrameRateExtN = numerator;
    priv->mfx_video_param.vpp.In.FourCC = MFX_FOURCC_NV12;
    priv->mfx_video_param.vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    priv->mfx_video_param.vpp.In.CropX = 0;
    priv->mfx_video_param.vpp.In.CropY = 0;
    priv->mfx_video_param.vpp.In.CropW = width;
    priv->mfx_video_param.vpp.In.CropH = height;
    priv->mfx_video_param.vpp.Out.Width = priv->width;
    priv->mfx_video_param.vpp.Out.Height = priv->height;
    priv->mfx_video_param.vpp.Out.FrameRateExtD = denominator;
    priv->mfx_video_param.vpp.Out.FrameRateExtN = numerator;
    priv->mfx_video_param.vpp.Out.FourCC = MFX_FOURCC_NV12;
    priv->mfx_video_param.vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    priv->mfx_video_param.vpp.Out.CropX = 0;
    priv->mfx_video_param.vpp.Out.CropY = 0;
    priv->mfx_video_param.vpp.Out.CropW = priv->width;
    priv->mfx_video_param.vpp.Out.CropH = priv->height;
    s = MFXVideoVPP_Init (priv->mfx_session, &priv->mfx_video_param);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoVPP_Init failed(%d)!", s);
        goto fail;
    }

    s = MFXVideoVPP_GetVideoParam (priv->mfx_session, &priv->mfx_video_param);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoVPP_GetVideoParam failed(%d)!", s);
        goto fail;
    }
    /* Calc input buffer length */
    switch (priv->mfx_video_param.vpp.In.FourCC) {
    case MFX_FOURCC_NV12:
        priv->in_buf_len = width * height +
            (width>>1) * (height>>1) +
            (width>>1) * (height>>1);
        break;
    }
    /* Calc output buffer length */
    switch (priv->mfx_video_param.vpp.Out.FourCC) {
    case MFX_FOURCC_NV12:
        priv->out_buf_len = priv->width * priv->height +
            (priv->width>>1) * (priv->height>>1) +
            (priv->width>>1) * (priv->height>>1);
        break;
    }

    s = MFXVideoVPP_QueryIOSurf (priv->mfx_session,
                &priv->mfx_video_param, reqs);
    if (MFX_ERR_NONE != s) {
        g_critical ("MFXVideoVPP_QueryIOSurf failed(%d)!", s);
        goto fail;
    }
    /* Free previous task pool */
    if (priv->task_pool) {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            g_slice_free1 (priv->in_buf_len,
                        priv->task_pool[i].input.Data.MemId);
            g_slice_free1 (priv->out_buf_len,
                        priv->task_pool[i].output.Data.MemId);
        }
        g_slice_free1 (sizeof (GstMfxSclTask) * priv->task_pool_len,
                    priv->task_pool);
    }
    /* Alloc new task pool */
    priv->task_pool_len = (reqs[0].NumFrameSuggested +
                reqs[1].NumFrameSuggested) / 2;
    priv->task_pool = g_slice_alloc0 (sizeof (GstMfxSclTask) * priv->task_pool_len);
    {
        guint i = 0;

        for (i=0; i<priv->task_pool_len; i++) {
            /* Id */
            priv->task_pool[i].id = i;
            /* Set input frame info */
            memcpy (&priv->task_pool[i].input.Info,
                        &priv->mfx_video_param.vpp.In,
                        sizeof (mfxFrameInfo));
            /* Set output frame info */
            memcpy (&priv->task_pool[i].output.Info,
                        &priv->mfx_video_param.vpp.Out,
                        sizeof (mfxFrameInfo));
            /* Alloc buffer for input: mfxFrameSurface1 */
            priv->task_pool[i].input.Data.MemId =
                g_slice_alloc0 (priv->in_buf_len);
            switch (priv->mfx_video_param.vpp.In.FourCC) {
            case MFX_FOURCC_NV12:
                priv->task_pool[i].input.Data.Y =
                    priv->task_pool[i].input.Data.MemId;
                priv->task_pool[i].input.Data.U =
                    priv->task_pool[i].input.Data.Y +
                    width * height;
                priv->task_pool[i].input.Data.V =
                    priv->task_pool[i].input.Data.U + 1;
                priv->task_pool[i].input.Data.Pitch = width;
                break;
            }
            /* Alloc buffer for output: mfxFrameSurface1 */
            priv->task_pool[i].output.Data.MemId =
                g_slice_alloc0 (priv->out_buf_len);
            switch (priv->mfx_video_param.vpp.Out.FourCC) {
            case MFX_FOURCC_NV12:
                priv->task_pool[i].output.Data.Y =
                    priv->task_pool[i].output.Data.MemId;
                priv->task_pool[i].output.Data.U =
                    priv->task_pool[i].output.Data.Y +
                    priv->width * priv->height;
                priv->task_pool[i].output.Data.V =
                    priv->task_pool[i].output.Data.U + 1;
                priv->task_pool[i].output.Data.Pitch = priv->width;
                break;
            }
        }
    }

    if (priv->src_pad_caps)
      gst_caps_unref (priv->src_pad_caps);
    structure = gst_structure_new ("video/x-raw-yuv",
                "width", G_TYPE_INT, priv->width,
                "height", G_TYPE_INT, priv->height,
                "framerate", GST_TYPE_FRACTION, numerator, denominator,
                "format", GST_TYPE_FOURCC,
                                GST_MAKE_FOURCC ('N', 'V', '1', '2'),
                NULL);
    priv->src_pad_caps = gst_caps_new_full (structure, NULL);

    return TRUE;

fail:

    return FALSE;
}

static gboolean
gst_mfx_scl_sink_pad_event (GstPad *pad, GstEvent *event)
{
    GstMfxScl *self = GST_MFX_SCL (GST_OBJECT_PARENT (pad));
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
        gst_mfx_scl_flush_frames (self, TRUE);
        break;
    default:
        break;
    }

    return gst_pad_push_event (priv->src_pad, event);
}

static GstFlowReturn
gst_mfx_scl_sink_pad_bufferalloc (GstPad *pad, guint64 offset,
            guint size, GstCaps *caps, GstBuffer **buf)
{
    GstMfxScl *self = GST_MFX_SCL (GST_OBJECT_PARENT (pad));
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);
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

    if (G_UNLIKELY (size != priv->in_buf_len))
      g_assert_not_reached ();

    ret = gst_mfx_scl_get_free_task (self, &free);
    if (GST_FLOW_OK != ret)
      return ret;

    *buf = gst_buffer_new ();
    if (!*buf)
      return GST_FLOW_ERROR;
    GST_BUFFER_DATA (*buf) = priv->task_pool[free].input.Data.MemId;
    GST_BUFFER_SIZE (*buf) = priv->in_buf_len;
    GST_BUFFER_OFFSET (*buf) = GST_BUFFER_OFFSET_NONE;
    gst_buffer_set_caps (*buf, caps);

    return GST_FLOW_OK;
}

static GstFlowReturn
gst_mfx_scl_sink_pad_chain (GstPad *pad, GstBuffer *buf)
{
    GstMfxScl *self = GST_MFX_SCL (GST_OBJECT_PARENT (pad));
    GstMfxSclPrivate *priv = GST_MFX_SCL_GET_PRIVATE (self);
    GstMfxSclTask *task = NULL;
    GstFlowReturn ret = GST_FLOW_OK;
    gint i = 0, tid = -1;
    gboolean retry = TRUE, mcpy = FALSE;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    /* Find buf's owner: task */
    for (i=0; i<priv->task_pool_len; i++) {
        if (priv->task_pool[i].input.Data.MemId ==
                    GST_BUFFER_DATA (buf)) {
            tid = i;
            break;
        }
    }

    /* Not found in task pool, may be is first alloced buffer.
     * Get a free task to handle it and set mcpy = TRUE.
     */
    if (-1 == tid) {
        ret = gst_mfx_scl_get_free_task (self, &tid);
        if (GST_FLOW_OK != ret)
          goto fail;
        mcpy = TRUE;
    }

    task = &priv->task_pool[tid];

    /* Input: mfxFrameSurface1 */
    if (G_UNLIKELY (mcpy)) {
        if (priv->in_buf_len != GST_BUFFER_SIZE (buf))
          g_assert_not_reached ();
        memcpy (task->input.Data.MemId,
                    GST_BUFFER_DATA (buf), priv->in_buf_len);
    }
    task->input.Data.TimeStamp = GST_BUFFER_TIMESTAMP (buf);

    /* Output: save duration */
    task->duration = GST_BUFFER_DURATION (buf);

    /* Free input buffer: GstBuffer */
    gst_buffer_unref (buf);

    /* Commit the task to MFX Encoder */
    do {
        mfxStatus s = MFX_ERR_NONE;

        s = MFXVideoVPP_RunFrameVPPAsync (priv->mfx_session,
                    &task->input, &task->output, NULL, &task->sp);

        if (MFX_ERR_NONE < s && !task->sp) {
            if (MFX_WRN_DEVICE_BUSY == s)
              g_usleep (100);
            retry = TRUE;
        } else if (MFX_ERR_NONE < s && task->sp) {
            retry = FALSE;
        } else if (MFX_ERR_MORE_DATA == s) {
            retry = TRUE;
        } else if (MFX_ERR_NONE != s) {
            g_critical ("MFXVideoVPP_RunFrameVPPAsync failed(%d)!", s);
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

