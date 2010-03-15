/*
 *  gstvaapiimage.h - VA image abstraction
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef GST_VAAPI_IMAGE_H
#define GST_VAAPI_IMAGE_H

#include <gst/gstbuffer.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiimageformat.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_IMAGE \
    (gst_vaapi_image_get_type())

#define GST_VAAPI_IMAGE(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_IMAGE,   \
                                GstVaapiImage))

#define GST_VAAPI_IMAGE_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_IMAGE,      \
                             GstVaapiImageClass))

#define GST_VAAPI_IS_IMAGE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_IMAGE))

#define GST_VAAPI_IS_IMAGE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_IMAGE))

#define GST_VAAPI_IMAGE_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_IMAGE,    \
                               GstVaapiImageClass))

#define GST_VAAPI_IMAGE_FORMAT(img)     gst_vaapi_image_get_format(img)
#define GST_VAAPI_IMAGE_WIDTH(img)      gst_vaapi_image_get_width(img)
#define GST_VAAPI_IMAGE_HEIGHT(img)     gst_vaapi_image_get_height(img)

typedef struct _GstVaapiImage                   GstVaapiImage;
typedef struct _GstVaapiImagePrivate            GstVaapiImagePrivate;
typedef struct _GstVaapiImageClass              GstVaapiImageClass;

struct _GstVaapiImage {
    /*< private >*/
    GObject parent_instance;

    GstVaapiImagePrivate *priv;
};

struct _GstVaapiImageClass {
    /*< private >*/
    GObjectClass parent_class;
};

GType
gst_vaapi_image_get_type(void);

GstVaapiImage *
gst_vaapi_image_new(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format,
    guint               width,
    guint               height
);

VAImageID
gst_vaapi_image_get_id(GstVaapiImage *image);

GstVaapiDisplay *
gst_vaapi_image_get_display(GstVaapiImage *image);

GstVaapiImageFormat
gst_vaapi_image_get_format(GstVaapiImage *image);

guint
gst_vaapi_image_get_width(GstVaapiImage *image);

guint
gst_vaapi_image_get_height(GstVaapiImage *image);

void
gst_vaapi_image_get_size(GstVaapiImage *image, guint *pwidth, guint *pheight);

gboolean
gst_vaapi_image_is_mapped(GstVaapiImage *image);

gboolean
gst_vaapi_image_map(GstVaapiImage *image);

gboolean
gst_vaapi_image_unmap(GstVaapiImage *image);

guint
gst_vaapi_image_get_plane_count(GstVaapiImage *image);

guchar *
gst_vaapi_image_get_plane(GstVaapiImage *image, guint plane);

guint
gst_vaapi_image_get_pitch(GstVaapiImage *image, guint plane);

gboolean
gst_vaapi_image_update_from_buffer(GstVaapiImage *image, GstBuffer *buffer);

G_END_DECLS

#endif /* GST_VAAPI_IMAGE_H */
