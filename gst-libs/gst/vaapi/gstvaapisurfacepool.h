/*
 *  gstvaapisurfacepool.h - Gst VA surface pool
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

#ifndef GST_VAAPI_SURFACE_POOL_H
#define GST_VAAPI_SURFACE_POOL_H

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapivideopool.h>
#include <gst/video/gstvideometa.h>
#include <gst/vaapi/gstvaapisurfacememory.h>
G_BEGIN_DECLS

/*#define GST_VAAPI_TYPE_SURFACE_MEMORY \
	(gst_vaapi_surface_memory_get_type())
#define GST_IS_VAAPI_SURFACE_MEMORY(obj)        (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_SURFACE_MEMORY))
#define GST_VAAPI_SURFACE_MEMORY_CAST(obj)      ((GstVaapiSurfaceMemory *)(obj))
#define GST_VAAPI_SURFACE_MEMORY(obj)           (GST_VAAPI_SURFACE_MEMORY_CAST(obj))
*/

#define GST_VAAPI_TYPE_SURFACE_POOL \
    (gst_vaapi_surface_pool_get_type())

#define GST_VAAPI_SURFACE_POOL(obj)                             \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_SURFACE_POOL,    \
                                GstVaapiSurfacePool))

#define GST_VAAPI_SURFACE_POOL_CLASS(klass)                     \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_SURFACE_POOL,       \
                             GstVaapiSurfacePoolClass))

#define GST_VAAPI_IS_SURFACE_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_SURFACE_POOL))

#define GST_VAAPI_IS_SURFACE_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_SURFACE_POOL))

#define GST_VAAPI_SURFACE_POOL_GET_CLASS(obj)                   \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_SURFACE_POOL,     \
                               GstVaapiSurfacePoolClass))

typedef struct _GstVaapiSurfacePool             GstVaapiSurfacePool;
typedef struct _GstVaapiSurfacePoolPrivate      GstVaapiSurfacePoolPrivate;
typedef struct _GstVaapiSurfacePoolClass        GstVaapiSurfacePoolClass;

/**
 * GstVaapiSurfacePool:
 *
 * A pool of lazily allocated #GstVaapiSurface objects.
 */
struct _GstVaapiSurfacePool {
    /*< private >*/
    GstVaapiVideoPool parent_instance;

    GstVaapiSurfacePoolPrivate *priv;
};

/**
 * GstVaapiSurfacePoolClass:
 *
 * A pool of lazily allocated #GstVaapiSurface objects.
 */
struct _GstVaapiSurfacePoolClass {
    /*< private >*/
    GstVaapiVideoPoolClass parent_class;
};

GType
gst_vaapi_surface_pool_get_type (void);

GstVaapiVideoPool *
gst_vaapi_surface_pool_new(GstVaapiDisplay *display, GstCaps *caps);


G_END_DECLS

#endif /* GST_VAAPI_SURFACE_POOL_H */
