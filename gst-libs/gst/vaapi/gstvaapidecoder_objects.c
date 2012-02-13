/*
 *  gstvaapidecoder_objects.c - VA decoder objects helpers
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

#include "sysdeps.h"
#include <string.h>
#include <gst/vaapi/gstvaapicontext.h>
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GET_DECODER(obj)    GST_VAAPI_DECODER_CAST((obj)->parent_instance.codec)
#define GET_CONTEXT(obj)    GET_DECODER(obj)->priv->context
#define GET_VA_DISPLAY(obj) GET_DECODER(obj)->priv->va_display
#define GET_VA_CONTEXT(obj) GET_DECODER(obj)->priv->va_context

/* ------------------------------------------------------------------------- */
/* --- Pictures                                                          --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiPicture,
                            gst_vaapi_picture,
                            GST_VAAPI_TYPE_CODEC_OBJECT)

static void
destroy_slice_cb(gpointer data, gpointer user_data)
{
    GstMiniObject * const object = data;

    gst_mini_object_unref(object);
}

static void
gst_vaapi_picture_destroy(GstVaapiPicture *picture)
{
    if (picture->slices) {
        g_ptr_array_foreach(picture->slices, destroy_slice_cb, NULL);
        g_ptr_array_free(picture->slices, TRUE);
        picture->slices = NULL;
    }

    if (picture->iq_matrix) {
        gst_mini_object_unref(GST_MINI_OBJECT(picture->iq_matrix));
        picture->iq_matrix = NULL;
    }

    if (picture->bitplane) {
        gst_mini_object_unref(GST_MINI_OBJECT(picture->bitplane));
        picture->bitplane = NULL;
    }

    if (picture->proxy) {
        g_object_unref(picture->proxy);
        picture->proxy = NULL;
    }
    else if (picture->surface) {
        /* Explicitly release any surface that was not bound to a proxy */
        gst_vaapi_context_put_surface(GET_CONTEXT(picture), picture->surface);
    }
    picture->surface_id = VA_INVALID_ID;
    picture->surface = NULL;

    vaapi_destroy_buffer(GET_VA_DISPLAY(picture), &picture->param_id);
    picture->param = NULL;
}

static gboolean
gst_vaapi_picture_create(
    GstVaapiPicture                          *picture,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    gboolean success;

    picture->surface = gst_vaapi_context_get_surface(GET_CONTEXT(picture));
    if (!picture->surface)
        return FALSE;
    picture->surface_id = gst_vaapi_surface_get_id(picture->surface);

    picture->proxy =
        gst_vaapi_surface_proxy_new(GET_CONTEXT(picture), picture->surface);
    if (!picture->proxy)
        return FALSE;

    success = vaapi_create_buffer(
        GET_VA_DISPLAY(picture),
        GET_VA_CONTEXT(picture),
        VAPictureParameterBufferType,
        args->param_size,
        args->param,
        &picture->param_id,
        &picture->param
    );
    if (!success)
        return FALSE;

    picture->slices = g_ptr_array_new();
    if (!picture->slices)
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_picture_init(GstVaapiPicture *picture)
{
    picture->type       = GST_VAAPI_PICTURE_TYPE_NONE;
    picture->surface    = NULL;
    picture->proxy      = NULL;
    picture->surface_id = VA_INVALID_ID;
    picture->param      = NULL;
    picture->param_id   = VA_INVALID_ID;
    picture->slices     = NULL;
    picture->iq_matrix  = NULL;
    picture->bitplane   = NULL;
    picture->pts        = GST_CLOCK_TIME_NONE;
    picture->render_flag = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
}

GstVaapiPicture *
gst_vaapi_picture_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size
)
{
    GstVaapiCodecObject *object;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    object = gst_vaapi_codec_object_new(
        GST_VAAPI_TYPE_PICTURE,
        GST_VAAPI_CODEC_BASE(decoder),
        param, param_size,
        NULL, 0
    );
    if (!object)
        return NULL;
    return GST_VAAPI_PICTURE_CAST(object);
}

void
gst_vaapi_picture_add_slice(GstVaapiPicture *picture, GstVaapiSlice *slice)
{
    g_return_if_fail(GST_VAAPI_IS_PICTURE(picture));
    g_return_if_fail(GST_VAAPI_IS_SLICE(slice));

    g_ptr_array_add(picture->slices, slice);
}

gboolean
gst_vaapi_picture_decode(GstVaapiPicture *picture)
{
    GstVaapiIqMatrix *iq_matrix;
    GstVaapiBitPlane *bitplane;
    VADisplay va_display;
    VAContextID va_context;
    VABufferID va_buffers[3];
    guint i, n_va_buffers = 0;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_PICTURE(picture), FALSE);

    va_display = GET_VA_DISPLAY(picture);
    va_context = GET_VA_CONTEXT(picture);

    GST_DEBUG("decode picture 0x%08x", picture->surface_id);

    vaapi_unmap_buffer(va_display, picture->param_id, &picture->param);
    va_buffers[n_va_buffers++] = picture->param_id;

    iq_matrix = picture->iq_matrix;
    if (iq_matrix) {
        vaapi_unmap_buffer(
            va_display,
            iq_matrix->param_id, &iq_matrix->param
        );
        va_buffers[n_va_buffers++] = iq_matrix->param_id;
    }

    bitplane = picture->bitplane;
    if (bitplane) {
        vaapi_unmap_buffer(
            va_display,
            bitplane->data_id, (void **)&bitplane->data
        );
        va_buffers[n_va_buffers++] = bitplane->data_id;
    }

    status = vaBeginPicture(va_display, va_context, picture->surface_id);
    if (!vaapi_check_status(status, "vaBeginPicture()"))
        return FALSE;

    status = vaRenderPicture(va_display, va_context, va_buffers, n_va_buffers);
    if (!vaapi_check_status(status, "vaRenderPicture()"))
        return FALSE;

    for (i = 0; i < picture->slices->len; i++) {
        GstVaapiSlice * const slice = g_ptr_array_index(picture->slices, i);

        vaapi_unmap_buffer(va_display, slice->param_id, NULL);
        va_buffers[0] = slice->param_id;
        va_buffers[1] = slice->data_id;
        n_va_buffers  = 2;

        status = vaRenderPicture(
            va_display,
            va_context,
            va_buffers, n_va_buffers
        );
        if (!vaapi_check_status(status, "vaRenderPicture()"))
            return FALSE;
    }

    status = vaEndPicture(va_display, va_context);
    if (!vaapi_check_status(status, "vaEndPicture()"))
        return FALSE;
    return TRUE;
}

gboolean
gst_vaapi_picture_output(GstVaapiPicture *picture)
{
    GstVaapiSurfaceProxy *proxy;

    g_return_val_if_fail(GST_VAAPI_IS_PICTURE(picture), FALSE);

    if (!picture->proxy)
        return FALSE;

    proxy = g_object_ref(picture->proxy);
    gst_vaapi_surface_proxy_set_timestamp(proxy, picture->pts);
    gst_vaapi_decoder_push_surface_proxy(GET_DECODER(picture), proxy);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* --- Slices                                                            --- */
/* ------------------------------------------------------------------------- */

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiSlice,
                            gst_vaapi_slice,
                            GST_VAAPI_TYPE_CODEC_OBJECT)

static void
gst_vaapi_slice_destroy(GstVaapiSlice *slice)
{
    VADisplay const va_display = GET_VA_DISPLAY(slice);

    vaapi_destroy_buffer(va_display, &slice->data_id);
    vaapi_destroy_buffer(va_display, &slice->param_id);
    slice->param = NULL;
}

static gboolean
gst_vaapi_slice_create(
    GstVaapiSlice                            *slice,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    VASliceParameterBufferBase *slice_param;
    gboolean success;

    success = vaapi_create_buffer(
        GET_VA_DISPLAY(slice),
        GET_VA_CONTEXT(slice),
        VASliceDataBufferType,
        args->data_size,
        args->data,
        &slice->data_id,
        NULL
    );
    if (!success)
        return FALSE;

    success = vaapi_create_buffer(
        GET_VA_DISPLAY(slice),
        GET_VA_CONTEXT(slice),
        VASliceParameterBufferType,
        args->param_size,
        args->param,
        &slice->param_id,
        &slice->param
    );
    if (!success)
        return FALSE;

    slice_param                    = slice->param;
    slice_param->slice_data_size   = args->data_size;
    slice_param->slice_data_offset = 0;
    slice_param->slice_data_flag   = VA_SLICE_DATA_FLAG_ALL;
    return TRUE;
}

static void
gst_vaapi_slice_init(GstVaapiSlice *slice)
{
    slice->param        = NULL;
    slice->param_id     = VA_INVALID_ID;
    slice->data_id      = VA_INVALID_ID;
}

GstVaapiSlice *
gst_vaapi_slice_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size,
    const guchar    *data,
    guint            data_size
)
{
    GstVaapiCodecObject *object;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    object = gst_vaapi_codec_object_new(
        GST_VAAPI_TYPE_SLICE,
        GST_VAAPI_CODEC_BASE(decoder),
        param, param_size,
        data, data_size
    );
    return GST_VAAPI_SLICE_CAST(object);
}
