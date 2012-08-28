/*
 *  gstvaapivideopool.h - Video object pool abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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

#ifndef GST_VAAPI_VIDEO_POOL_H
#define GST_VAAPI_VIDEO_POOL_H

#include <glib.h>
#include <gst/gstcaps.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/gstvideometa.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_VIDEO_POOL \
    (gst_vaapi_video_pool_get_type())

#define GST_VAAPI_VIDEO_POOL(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_VIDEO_POOL,      \
                                GstVaapiVideoPool))

#define GST_VAAPI_VIDEO_POOL_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_VIDEO_POOL,         \
                             GstVaapiVideoPoolClass))

#define GST_VAAPI_IS_VIDEO_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_VIDEO_POOL))

#define GST_VAAPI_IS_VIDEO_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_VIDEO_POOL))

#define GST_VAAPI_VIDEO_POOL_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_VIDEO_POOL,       \
                               GstVaapiVideoPoolClass))

typedef struct _GstVaapiVideoPool               GstVaapiVideoPool;
typedef struct _GstVaapiVideoPoolPrivate        GstVaapiVideoPoolPrivate;
typedef struct _GstVaapiVideoPoolClass          GstVaapiVideoPoolClass;

/**
 * GstVaapiVideoPool:
 *
 * A pool of lazily allocated video objects. e.g. surfaces, images.
 */
struct _GstVaapiVideoPool {
    /*< private >*/
    GstBufferPool bufferpool;	

    GstVaapiVideoPoolPrivate *priv;
};

/**
 * GstVaapiVideoPoolClass:
 * @set_caps: virtual function for notifying the subclass of the
 *   negotiated caps
 *
 * A pool base class used to hold video objects. e.g. surfaces, images.
 */
struct _GstVaapiVideoPoolClass {
    /*< private >*/
    GstBufferPoolClass parent_class;
    /*< public >*/
    void     (*set_caps)    (GstVaapiVideoPool *pool, GstCaps *caps);
};

GType
gst_vaapi_video_pool_get_type(void);

GstVaapiDisplay *
gst_vaapi_video_pool_get_display(GstVaapiVideoPool *pool);

GstCaps *
gst_vaapi_video_pool_get_caps(GstVaapiVideoPool *pool);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_POOL_H */
