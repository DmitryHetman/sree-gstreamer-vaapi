/*
 *  gstvaapicontext.c - VA context abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapicontext
 * @short_description: VA context abstraction
 */

#include "sysdeps.h"
#include <assert.h>
#include "gstvaapicompat.h"
#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapivideopool.h"
#include "gstvaapiimage.h"
#include "gstvaapisubpicture.h"
#include "gstvaapiutils.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiContext, gst_vaapi_context, GST_VAAPI_TYPE_OBJECT)

#define GST_VAAPI_CONTEXT_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_CONTEXT,	\
                                 GstVaapiContextPrivate))

typedef struct _GstVaapiOverlayRectangle GstVaapiOverlayRectangle;
struct _GstVaapiOverlayRectangle {
    GstVaapiContext    *context;
    GstVaapiSubpicture *subpicture;
    GstVaapiRectangle   rect;
    guint               seq_num;
};

/* XXX: optimize for the effective number of reference frames */
struct _GstVaapiContextPrivate {
    VAConfigID           config_id;
    GPtrArray           *surfaces;
    GstVaapiVideoPool   *video_pool;
    GPtrArray           *overlay;
    GstVaapiProfile      profile;
    GstVaapiEntrypoint   entrypoint;
    guint                width;
    guint                height;
    guint                ref_frames;
    guint                is_constructed  : 1;
};

enum {
    PROP_0,

    PROP_PROFILE,
    PROP_ENTRYPOINT,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_REF_FRAMES,
    PROP_POOL
};

static guint
get_max_ref_frames(GstVaapiProfile profile)
{
    guint ref_frames;

    switch (gst_vaapi_profile_get_codec(profile)) {
    case GST_VAAPI_CODEC_H264:  ref_frames = 16; break;
    case GST_VAAPI_CODEC_JPEG:  ref_frames =  0; break;
    default:                    ref_frames =  2; break;
    }
    return ref_frames;
}

static GstVaapiOverlayRectangle *
overlay_rectangle_new(GstVaapiContext *context)
{
    GstVaapiOverlayRectangle *overlay;

    overlay = g_slice_new0(GstVaapiOverlayRectangle);
    if (!overlay)
        return NULL;

    overlay->context = context;
    return overlay;
}

static void
overlay_rectangle_destroy(GstVaapiOverlayRectangle *overlay)
{
    GstVaapiContextPrivate *priv;
    guint i;

    if (!overlay)
        return;
    priv = overlay->context->priv;

    if (overlay->subpicture) {
        if (priv->surfaces) {
            GstVaapiSubpicture * const subpicture = overlay->subpicture;
            for (i = 0; i < priv->surfaces->len; i++) {
                GstVaapiSurface * const surface =
                    g_ptr_array_index(priv->surfaces, i);
                gst_vaapi_surface_deassociate_subpicture(surface, subpicture);
            }
        }
        g_object_unref(overlay->subpicture);
        overlay->subpicture = NULL;
    }
    g_slice_free(GstVaapiOverlayRectangle, overlay);
}

static void
destroy_overlay_cb(gpointer data, gpointer user_data)
{
    GstVaapiOverlayRectangle * const overlay = data;

    overlay_rectangle_destroy(overlay);
}

static void
gst_vaapi_context_destroy_overlay(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;

    if (!priv->overlay)
        return;

    g_ptr_array_foreach(priv->overlay, destroy_overlay_cb, priv);
    g_ptr_array_free(priv->overlay, TRUE);
    priv->overlay = NULL;
}

static void
unref_surface_cb(gpointer data, gpointer user_data)
{
    GstVaapiSurface * const surface = GST_VAAPI_SURFACE(data);

    gst_vaapi_surface_set_parent_context(surface, NULL);
    g_object_unref(surface);
}

static void
gst_vaapi_context_destroy_surfaces(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;

    gst_vaapi_context_destroy_overlay(context);

    if (priv->surfaces) {
        g_ptr_array_foreach(priv->surfaces, unref_surface_cb, NULL);
        g_ptr_array_free(priv->surfaces, TRUE);
        priv->surfaces = NULL;
    }

    g_clear_object(&priv->video_pool);
}

static void
gst_vaapi_context_destroy(GstVaapiContext *context)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(context);
    GstVaapiContextPrivate * const priv = context->priv;
    VAContextID context_id;
    VAStatus status;

    context_id = GST_VAAPI_OBJECT_ID(context);
    GST_DEBUG("context %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(context_id));

    if (context_id != VA_INVALID_ID) {
        GST_VAAPI_DISPLAY_LOCK(display);
        status = vaDestroyContext(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            context_id
        );
        GST_VAAPI_DISPLAY_UNLOCK(display);
        if (!vaapi_check_status(status, "vaDestroyContext()"))
            g_warning("failed to destroy context %" GST_VAAPI_ID_FORMAT,
                      GST_VAAPI_ID_ARGS(context_id));
        GST_VAAPI_OBJECT_ID(context) = VA_INVALID_ID;
    }

    if (priv->config_id != VA_INVALID_ID) {
        GST_VAAPI_DISPLAY_LOCK(display);
        status = vaDestroyConfig(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            priv->config_id
        );
        GST_VAAPI_DISPLAY_UNLOCK(display);
        if (!vaapi_check_status(status, "vaDestroyConfig()"))
            g_warning("failed to destroy config %" GST_VAAPI_ID_FORMAT,
                      GST_VAAPI_ID_ARGS(priv->config_id));
        priv->config_id = VA_INVALID_ID;
    }
}

static gboolean
gst_vaapi_context_create_overlay(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;

    if (!priv->overlay) {
        priv->overlay = g_ptr_array_new();
        if (!priv->overlay)
            return FALSE;
    }
    return TRUE;
}

static gboolean
gst_vaapi_context_create_surfaces(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;
    GstCaps *caps;
    GstVaapiSurface *surface;
    guint i, num_surfaces;
    guint size;
    GstStructure *config;
    GstBuffer *out;
    GstVaapiVideoMeta *meta;

    /* Number of scratch surfaces beyond those used as reference */
    const guint SCRATCH_SURFACES_COUNT = 4;

    if (!gst_vaapi_context_create_overlay(context))
        return FALSE;

    if (!priv->surfaces) {
        priv->surfaces = g_ptr_array_new();
        if (!priv->surfaces) 
	    return FALSE;
    }

    num_surfaces = priv->ref_frames + SCRATCH_SURFACES_COUNT;

    config = gst_buffer_pool_get_config ((GstBufferPool *)priv->video_pool);
    gst_buffer_pool_config_get_params (config, &caps, &size, NULL, NULL);
    gst_buffer_pool_config_set_params (config, caps, size, num_surfaces, num_surfaces);
    gst_buffer_pool_set_config ((GstBufferPool *)priv->video_pool, config);

    if (!gst_buffer_pool_set_active ((GstBufferPool *)priv->video_pool, TRUE)) {
	GST_ERROR_OBJECT (context, "Failed to activate the video_pool from GstVaapiContext");
	return FALSE;
    }

    for (i = priv->surfaces->len; i < num_surfaces; i++) {

        if (GST_FLOW_OK != gst_buffer_pool_acquire_buffer ((GstBufferPool *)priv->video_pool, &out, NULL)) {
	    GST_ERROR_OBJECT (priv->video_pool, "Failed to acquire buffer from Va Video Pool");
	    return FALSE;
	}
	meta =gst_buffer_get_vaapi_video_meta(out);

	if (!meta) {
	   GST_ERROR ("Failed to get the VaapiVideoMeta");
	   return FALSE;
	}	
	surface = gst_vaapi_video_meta_get_surface(meta);
        if (!surface)
        {
                GST_DEBUG ("Mapping failed...");
                return FALSE;
        }
	/* Even though the surface pool destroyed at some point, surfaces will stay alive 
	 * until context destruction, which will release the final ref to surfaces*/
	g_object_ref(surface);
        g_ptr_array_add(priv->surfaces, surface);
        gst_buffer_pool_release_buffer ((GstBufferPool *)priv->video_pool, out);
    }
    return TRUE;
}

static gboolean
gst_vaapi_context_create(GstVaapiContext *context)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(context);
    GstVaapiContextPrivate * const priv = context->priv;
    VAProfile va_profile;
    VAEntrypoint va_entrypoint;
    VAConfigAttrib attrib;
    VAContextID context_id;
    VASurfaceID surface_id;
    VAStatus status;
    GArray *surfaces = NULL;
    gboolean success = FALSE;
    guint i;

    if (!priv->surfaces && !gst_vaapi_context_create_surfaces(context))
        goto end;

    surfaces = g_array_sized_new(
        FALSE,
        FALSE,
        sizeof(VASurfaceID),
        priv->surfaces->len
    );
    if (!surfaces)
        goto end;

    for (i = 0; i < priv->surfaces->len; i++) {
        GstVaapiSurface * const surface = g_ptr_array_index(priv->surfaces, i);
        if (!surface)
            goto end;
        surface_id = GST_VAAPI_OBJECT_ID(surface);
        g_array_append_val(surfaces, surface_id);
    }
    assert(surfaces->len == priv->surfaces->len);

    if (!priv->profile || !priv->entrypoint)
        goto end;
    va_profile    = gst_vaapi_profile_get_va_profile(priv->profile);
    va_entrypoint = gst_vaapi_entrypoint_get_va_entrypoint(priv->entrypoint);

    GST_VAAPI_DISPLAY_LOCK(display);
    attrib.type = VAConfigAttribRTFormat;
    status = vaGetConfigAttributes(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        va_profile,
        va_entrypoint,
        &attrib, 1
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaGetConfigAttributes()"))
        goto end;
    if (!(attrib.value & VA_RT_FORMAT_YUV420))
        goto end;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateConfig(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        va_profile,
        va_entrypoint,
        &attrib, 1,
        &priv->config_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateConfig()"))
        goto end;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateContext(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        priv->config_id,
        priv->width, priv->height,
        VA_PROGRESSIVE,
        (VASurfaceID *)surfaces->data, surfaces->len,
        &context_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateContext()"))
        goto end;

    GST_DEBUG("context %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(context_id));
    GST_VAAPI_OBJECT_ID(context) = context_id;
    success = TRUE;
end:
    if (surfaces)
        g_array_free(surfaces, TRUE);
    return success;
}

static void
gst_vaapi_context_finalize(GObject *object)
{
    GstVaapiContext * const context = GST_VAAPI_CONTEXT(object);
    GstVaapiContextPrivate * const priv    = context->priv;

    gst_vaapi_context_destroy(context);
    gst_vaapi_context_destroy_surfaces(context);
   
    if(priv->video_pool)
        gst_object_unref (priv->video_pool);

    G_OBJECT_CLASS(gst_vaapi_context_parent_class)->finalize(object);
}

static void
gst_vaapi_context_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiContext        * const context = GST_VAAPI_CONTEXT(object);
    GstVaapiContextPrivate * const priv    = context->priv;

    switch (prop_id) {
    case PROP_PROFILE:
        gst_vaapi_context_set_profile(context, g_value_get_uint(value));
        break;
    case PROP_ENTRYPOINT:
        priv->entrypoint = g_value_get_uint(value);
        break;
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    case PROP_REF_FRAMES:
        priv->ref_frames = g_value_get_uint(value);
        break;
    case PROP_POOL:
	priv->video_pool = g_value_get_pointer(value);
	if (priv->video_pool)
  	    gst_object_ref (priv->video_pool);
	break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_context_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiContext        * const context = GST_VAAPI_CONTEXT(object);
    GstVaapiContextPrivate * const priv    = context->priv;

    switch (prop_id) {
    case PROP_PROFILE:
        g_value_set_uint(value, gst_vaapi_context_get_profile(context));
        break;
    case PROP_ENTRYPOINT:
        g_value_set_uint(value, gst_vaapi_context_get_entrypoint(context));
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, priv->height);
        break;
    case PROP_REF_FRAMES:
        g_value_set_uint(value, priv->ref_frames);
        break;
    case PROP_POOL:
	g_value_set_pointer(value, priv->video_pool);
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_context_class_init(GstVaapiContextClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiContextPrivate));

    object_class->finalize     = gst_vaapi_context_finalize;
    object_class->set_property = gst_vaapi_context_set_property;
    object_class->get_property = gst_vaapi_context_get_property;

    g_object_class_install_property
        (object_class,
         PROP_PROFILE,
         g_param_spec_uint("profile",
                           "Profile",
                           "The profile used for decoding",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class,
         PROP_ENTRYPOINT,
         g_param_spec_uint("entrypoint",
                           "Entrypoint",
                           "The decoder entrypoint",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "The width of decoded surfaces",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "Height",
                           "The height of the decoded surfaces",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_REF_FRAMES,
         g_param_spec_uint("ref-frames",
                           "Reference Frames",
                           "The number of reference frames",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_POOL,
         g_param_spec_pointer("pool",
                              "pool",
                              "The Vaapi Surface Pool",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

}

static void
gst_vaapi_context_init(GstVaapiContext *context)
{
    GstVaapiContextPrivate *priv = GST_VAAPI_CONTEXT_GET_PRIVATE(context);

    context->priv       = priv;
    priv->config_id     = VA_INVALID_ID;
    priv->surfaces      = NULL;
    priv->video_pool    = NULL;
    priv->overlay       = NULL;
    priv->profile       = 0;
    priv->entrypoint    = 0;
    priv->width         = 0;
    priv->height        = 0;
    priv->ref_frames    = 0;
}

/**
 * gst_vaapi_context_new:
 * @display: a #GstVaapiDisplay
 * @profile: a #GstVaapiProfile
 * @entrypoint: a #GstVaapiEntrypoint
 * @width: coded width from the bitstream
 * @height: coded height from the bitstream
 *
 * Creates a new #GstVaapiContext with the specified codec @profile
 * and @entrypoint.
 *
 * Return value: the newly allocated #GstVaapiContext object
 */
GstVaapiContext *
gst_vaapi_context_new(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height,
    GstVaapiVideoPool *pool
)
{
    GstVaapiContextInfo info;

    info.profile    = profile;
    info.entrypoint = entrypoint;
    info.width      = width;
    info.height     = height;
    info.ref_frames = get_max_ref_frames(profile);
    info.pool	    = pool;
    return gst_vaapi_context_new_full(display, &info);
}

/**
 * gst_vaapi_context_new_full:
 * @display: a #GstVaapiDisplay
 * @cip: a pointer to the #GstVaapiContextInfo
 *
 * Creates a new #GstVaapiContext with the configuration specified by
 * @cip, thus including profile, entry-point, encoded size and maximum
 * number of reference frames reported by the bitstream.
 *
 * Return value: the newly allocated #GstVaapiContext object
 */
GstVaapiContext *
gst_vaapi_context_new_full(GstVaapiDisplay *display, GstVaapiContextInfo *cip)
{
    GstVaapiContext *context;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(cip->profile, NULL);
    g_return_val_if_fail(cip->entrypoint, NULL);
    g_return_val_if_fail(cip->width > 0, NULL);
    g_return_val_if_fail(cip->height > 0, NULL);

    context = g_object_new(
        GST_VAAPI_TYPE_CONTEXT,
        "display",      display,
        "id",           GST_VAAPI_ID(VA_INVALID_ID),
        "entrypoint",   cip->entrypoint,
        "width",        cip->width,
        "height",       cip->height,
        "ref-frames",   cip->ref_frames,
	"pool",		cip->pool,
        "profile",      cip->profile,
        NULL
    );
    if (!context->priv->is_constructed) {
        g_object_unref(context);
        return NULL;
    }
    
    return context;
}

/**
 * gst_vaapi_context_reset:
 * @context: a #GstVaapiContext
 * @profile: a #GstVaapiProfile
 * @entrypoint: a #GstVaapiEntrypoint
 * @width: coded width from the bitstream
 * @height: coded height from the bitstream
 *
 * Resets @context to the specified codec @profile and @entrypoint.
 * The surfaces will be reallocated if the coded size changed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_context_reset(
    GstVaapiContext    *context,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    unsigned int        width,
    unsigned int        height,
    GstVaapiVideoPool *pool
)
{
    GstVaapiContextPrivate * const priv = context->priv;
    GstVaapiContextInfo info;

    info.profile    = profile;
    info.entrypoint = entrypoint;
    info.width      = width;
    info.height     = height;
    info.ref_frames = priv->ref_frames;
    info.pool	    = pool;

    return gst_vaapi_context_reset_full(context, &info);
}

/**
 * gst_vaapi_context_reset_full:
 * @context: a #GstVaapiContext
 * @cip: a pointer to the new #GstVaapiContextInfo details
 *
 * Resets @context to the configuration specified by @cip, thus
 * including profile, entry-point, encoded size and maximum number of
 * reference frames reported by the bitstream.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_context_reset_full(GstVaapiContext *context, GstVaapiContextInfo *cip)
{
    GstVaapiContextPrivate * const priv = context->priv;
    gboolean size_changed, codec_changed;

    size_changed = priv->width != cip->width || priv->height != cip->height;
    if (size_changed) {
        gst_vaapi_context_destroy_surfaces(context);
        priv->width  = cip->width;
        priv->height = cip->height;
    }

    codec_changed = priv->profile != cip->profile || priv->entrypoint != cip->entrypoint;
    if (codec_changed) {
        gst_vaapi_context_destroy(context);
        priv->profile    = cip->profile;
        priv->entrypoint = cip->entrypoint;
    }

/*Fixme: check whether the pool is same or not*/
    if (priv->video_pool)
	gst_object_unref(priv->video_pool);
    else
        priv->video_pool  = gst_object_ref(cip->pool);

    if (size_changed && !gst_vaapi_context_create_surfaces(context))
        return FALSE;

    if (codec_changed && !gst_vaapi_context_create(context))
        return FALSE;

/*Fixme*/
    priv->is_constructed = TRUE;
    return TRUE;
}

/**
 * gst_vaapi_context_get_id:
 * @context: a #GstVaapiContext
 *
 * Returns the underlying VAContextID of the @context.
 *
 * Return value: the underlying VA context id
 */
GstVaapiID
gst_vaapi_context_get_id(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), VA_INVALID_ID);

    return GST_VAAPI_OBJECT_ID(context);
}

/**
 * gst_vaapi_context_get_profile:
 * @context: a #GstVaapiContext
 *
 * Returns the VA profile used by the @context.
 *
 * Return value: the VA profile used by the @context
 */
GstVaapiProfile
gst_vaapi_context_get_profile(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), 0);

    return context->priv->profile;
}

/**
 * gst_vaapi_context_set_profile:
 * @context: a #GstVaapiContext
 * @profile: the new #GstVaapiProfile to use
 *
 * Sets the new @profile to use with the @context. If @profile matches
 * the previous profile, this call has no effect. Otherwise, the
 * underlying VA context is recreated, while keeping the previously
 * allocated surfaces.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_context_set_profile(GstVaapiContext *context, GstVaapiProfile profile)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), FALSE);
    g_return_val_if_fail(profile, FALSE);

    return gst_vaapi_context_reset(context,
                                   profile,
                                   context->priv->entrypoint,
                                   context->priv->width,
                                   context->priv->height,
				   context->priv->video_pool);
}

/**
 * gst_vaapi_context_get_entrypoint:
 * @context: a #GstVaapiContext
 *
 * Returns the VA entrypoint used by the @context
 *
 * Return value: the VA entrypoint used by the @context
 */
GstVaapiEntrypoint
gst_vaapi_context_get_entrypoint(GstVaapiContext *context)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), 0);

    return context->priv->entrypoint;
}

/**
 * gst_vaapi_context_get_size:
 * @context: a #GstVaapiContext
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the size of the surfaces attached to @context.
 */
void
gst_vaapi_context_get_size(
    GstVaapiContext *context,
    guint           *pwidth,
    guint           *pheight
)
{
    g_return_if_fail(GST_VAAPI_IS_CONTEXT(context));

    if (pwidth)
        *pwidth = context->priv->width;

    if (pheight)
        *pheight = context->priv->height;
}

/**
 * gst_vaapi_context_get_surface_buffer:
 * @context: a #GstVaapiContext
 *
 * Acquires a free surface_buffer. The returned surface but be released with
 * gst_vaapi_context_put_surface_buffer(). This function returns %NULL if
 * there is no free surface_buffer available in the pool. The surface_buffers are
 * pre-allocated during context creation though.
 *
 * Return value: a free surface, or %NULL if none is available
 */
GstBuffer *
gst_vaapi_context_get_surface_buffer(GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;
    GstVaapiSurface *surface;
    GstBuffer *buffer;
    GstMapInfo info;
    GstVaapiVideoMeta *meta;

    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), NULL);

    if (gst_buffer_pool_acquire_buffer((GstBufferPool *)priv->video_pool, &buffer, NULL) != GST_FLOW_OK){
	GST_ERROR("Failed to acquire buffer");
	buffer = NULL;
    }
    if (buffer) {
	meta =gst_buffer_get_vaapi_video_meta(buffer);
	surface = gst_vaapi_video_meta_get_surface(meta);
	if (surface)
            gst_vaapi_surface_set_parent_context(GST_VAAPI_SURFACE(surface), context);
    }
    return buffer;
}

/**
 * gst_vaapi_context_get_video_pool:
 * @context: a #GstVaapiContext
 *
 * caller of this API is responsible for unreffing it after the usage.
 */

GstVaapiVideoPool *
gst_vaapi_context_get_video_pool (GstVaapiContext *context)
{
    GstVaapiContextPrivate * const priv = context->priv;
    if (priv->video_pool)
        return gst_object_ref (GST_OBJECT(priv->video_pool));
    else
	return NULL;
}

void
gst_vaapi_context_put_surface_buffer (GstVaapiContext *context, GstBuffer *buffer)
{
    GstVaapiContextPrivate * const priv = context->priv;
    GstVaapiSurface *surface;
    GstMapInfo info;
    GstVaapiVideoMeta *meta;
   
    if (buffer) {
	meta =gst_buffer_get_vaapi_video_meta(buffer);
	surface = gst_vaapi_video_meta_get_surface(meta);
	if (surface)
            gst_vaapi_surface_set_parent_context(GST_VAAPI_SURFACE(surface), NULL);
       
	gst_buffer_unref(buffer);
    }
}

/**
 * gst_vaapi_context_find_surface_by_id:
 * @context: a #GstVaapiContext
 * @id: the VA surface id to find
 *
 * Finds VA surface by @id in the list of surfaces attached to the @context.
 *
 * Return value: the matching #GstVaapiSurface object, or %NULL if
 *   none was found
 */
GstVaapiSurface *
gst_vaapi_context_find_surface_by_id(GstVaapiContext *context, GstVaapiID id)
{
    GstVaapiContextPrivate *priv;
    GstVaapiSurface *surface;
    guint i;

    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), NULL);

    priv = context->priv;
    g_return_val_if_fail(priv->surfaces, NULL);

    for (i = 0; i < priv->surfaces->len; i++) {
        surface = g_ptr_array_index(priv->surfaces, i);
        if (GST_VAAPI_OBJECT_ID(surface) == id)
            return surface;
    }
    return NULL;
}

/* Check if composition changed */
static gboolean
gst_vaapi_context_composition_changed(
    GstVaapiContext            *context,
    GstVideoOverlayComposition *composition
)
{
    GstVaapiContextPrivate * const priv = context->priv;
    GstVaapiOverlayRectangle *overlay;
    GstVideoOverlayRectangle *rect;
    guint i, n_rectangles;

    if (!priv->overlay || !composition)
        return TRUE;

    n_rectangles = gst_video_overlay_composition_n_rectangles(composition);
    if (priv->overlay->len != n_rectangles)
        return TRUE;

    for (i = 0; i < n_rectangles; i++) {
        rect = gst_video_overlay_composition_get_rectangle(composition, i);
        g_return_val_if_fail(rect, TRUE);
        overlay = g_ptr_array_index(priv->overlay, i);
        g_return_val_if_fail(overlay, TRUE);
        if (overlay->seq_num != gst_video_overlay_rectangle_get_seqnum(rect))
            return TRUE;
    }
    return FALSE;
}

/**
 * gst_vaapi_context_apply_composition:
 * @context: a #GstVaapiContext
 * @composition: a #GstVideoOverlayComposition
 *
 * Applies video composition planes to all surfaces bound to @context.
 * This helper function resets any additional subpictures the user may
 * have associated himself. A %NULL @composition will also clear all
 * the existing subpictures.
 *
 * Return value: %TRUE if all composition planes could be applied,
 *   %FALSE otherwise
 */
gboolean
gst_vaapi_context_apply_composition(
    GstVaapiContext            *context,
    GstVideoOverlayComposition *composition
)
{
    GstVaapiContextPrivate *priv;
    GstVideoOverlayRectangle *rect;
    GstVaapiOverlayRectangle *overlay = NULL;
    GstVaapiDisplay *display;
    guint i, j, n_rectangles;

    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), FALSE);

    priv = context->priv;
    if (!priv->surfaces)
        return FALSE;

    display = GST_VAAPI_OBJECT_DISPLAY(context);
    if (!display)
        return FALSE;

    if (!gst_vaapi_context_composition_changed(context, composition))
        return TRUE;
    gst_vaapi_context_destroy_overlay(context);

    if (!composition)
        return TRUE;
    if (!gst_vaapi_context_create_overlay(context))
        return FALSE;

    n_rectangles = gst_video_overlay_composition_n_rectangles(composition);
    for (i = 0; i < n_rectangles; i++) {
        rect = gst_video_overlay_composition_get_rectangle(composition, i);

        overlay = overlay_rectangle_new(context);
        if (!overlay) {
            GST_WARNING("could not create VA overlay rectangle");
            return FALSE;
        }
        overlay->seq_num = gst_video_overlay_rectangle_get_seqnum(rect);

        overlay->subpicture = gst_vaapi_subpicture_new_from_overlay_rectangle(
            display,
            rect
        );
        if (!overlay->subpicture) {
            overlay_rectangle_destroy(overlay);
            return FALSE;
        }

        gst_video_overlay_rectangle_get_render_rectangle(
            rect,
            (gint *)&overlay->rect.x,
            (gint *)&overlay->rect.y,
            &overlay->rect.width,
            &overlay->rect.height
        );

        for (j = 0; j < priv->surfaces->len; j++) {
            GstVaapiSurface * const surface =
                g_ptr_array_index(priv->surfaces, j);
            if (!gst_vaapi_surface_associate_subpicture(surface,
                         overlay->subpicture, NULL, &overlay->rect)) {
                GST_WARNING("could not render overlay rectangle %p", rect);
                overlay_rectangle_destroy(overlay);
                return FALSE;
            }
        }
        g_ptr_array_add(priv->overlay, overlay);
    }
    return TRUE;
}
