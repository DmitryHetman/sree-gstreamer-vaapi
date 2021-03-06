/*
 *  gstvaapidecoder_h264.h - H.264 decoder
 *
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

#ifndef GST_VAAPI_DECODER_H264_H
#define GST_VAAPI_DECODER_H264_H

#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DECODER_H264 \
    (gst_vaapi_decoder_h264_get_type())

#define GST_VAAPI_DECODER_H264(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                         \
                                GST_VAAPI_TYPE_DECODER_H264,   \
                                GstVaapiDecoderH264))

#define GST_VAAPI_DECODER_H264_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                          \
                             GST_VAAPI_TYPE_DECODER_H264,      \
                             GstVaapiDecoderH264Class))

#define GST_VAAPI_IS_DECODER_H264(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DECODER_H264))

#define GST_VAAPI_IS_DECODER_H264_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DECODER_H264))

#define GST_VAAPI_DECODER_H264_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                          \
                               GST_VAAPI_TYPE_DECODER_H264,    \
                               GstVaapiDecoderH264Class))

typedef struct _GstVaapiDecoderH264             GstVaapiDecoderH264;
typedef struct _GstVaapiDecoderH264Private      GstVaapiDecoderH264Private;
typedef struct _GstVaapiDecoderH264Class        GstVaapiDecoderH264Class;

/**
 * GstVaapiDecoderH264:
 *
 * A decoder based on H264.
 */
struct _GstVaapiDecoderH264 {
    /*< private >*/
    GstVaapiDecoder parent_instance;

    GstVaapiDecoderH264Private *priv;
};

/**
 * GstVaapiDecoderH264Class:
 *
 * A decoder class based on H264.
 */
struct _GstVaapiDecoderH264Class {
    /*< private >*/
    GstVaapiDecoderClass parent_class;
};

GType
gst_vaapi_decoder_h264_get_type(void) G_GNUC_CONST;

GstVaapiDecoder *
gst_vaapi_decoder_h264_new(GstVaapiDisplay *display, GstCaps *caps);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_H264_H */
