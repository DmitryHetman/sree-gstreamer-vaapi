/*
 *  gstvaapisurfacemeta.c - Gst VA surface meta
 *
 *  Copyright (C) 2012 Intel Corporation
 *  Copyright (C) : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 *  Contact : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstvaapisurfacemeta.h"

static gboolean
gst_vaapi_surface_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
    GstVaapiSurfaceMeta *dmeta, *smeta;
    guint i;
    
    smeta = (GstVaapiSurfaceMeta *) meta;
    
    if (GST_META_TRANSFORM_IS_COPY (type)) {
        GstMetaTransformCopy *copy = data;
        if (!copy->region) {
        /* only copy if the complete data is copied as well */
        dmeta =
            (GstVaapiSurfaceMeta *) gst_buffer_add_meta (dest, GST_VAAPI_SURFACE_META_INFO,
                NULL);
        GST_DEBUG ("copy vaapi surface metadata");
        dmeta->display      = g_object_ref(G_OBJECT(smeta->display));
        dmeta->surface_mem  = (GstVaapiSurfaceMemory *)gst_memory_ref((GstMemory *)(smeta->surface_mem));
        dmeta->render_flags = smeta->render_flags;
      }
    } 

    return TRUE;
}

static void
gst_vaapi_surface_meta_free (GstVaapiSurfaceMeta * meta, GstBuffer * buffer)
{
    if (meta->display) {
        g_object_unref(G_OBJECT(meta->display));
	meta->display = NULL;
    }
    if (meta->surface_mem) {
	gst_memory_unref((GstMemory *)(meta->surface_mem));
	meta->surface_mem = NULL;
    }   
}

/* vaapi_surface metadata */
GType
gst_vaapi_surface_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] =
      { "memory", "size", "colorspace", "orientation", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVaapiSurfaceMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_vaapi_surface_meta_get_info (void)
{
  static const GstMetaInfo *vaapi_surface_meta_info = NULL;

  if (vaapi_surface_meta_info == NULL) {
    vaapi_surface_meta_info =
        gst_meta_register (GST_VAAPI_SURFACE_META_API_TYPE, "GstVaapiSurfaceMeta",
        sizeof (GstVaapiSurfaceMeta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_vaapi_surface_meta_free,
        (GstMetaTransformFunction) gst_vaapi_surface_meta_transform);
  }
  return vaapi_surface_meta_info;
}

GstVaapiSurfaceMeta*
gst_buffer_add_vaapi_surface_meta (GstBuffer *buffer, GstVaapiDisplay *display)
{
    GstVaapiSurfaceMeta *meta;

    meta = (GstVaapiSurfaceMeta *)gst_buffer_add_meta (buffer, GST_VAAPI_SURFACE_META_INFO, NULL);

    meta->render_flags = 0x00000000;

    if (display)
        meta->display = g_object_ref(G_OBJECT(display));

    meta->surface_mem = (GstVaapiSurfaceMemory *) gst_buffer_get_memory (buffer, 0);
   
    return meta;
}

GstVaapiSurface * gst_vaapi_surface_meta_get_surface (GstVaapiSurfaceMeta *meta)
{
    g_return_val_if_fail(meta != NULL, NULL);
    if (meta->surface_mem)
	return meta->surface_mem->surface;
    else
	return NULL;
}

GstVaapiImage   * gst_vaapi_surface_meta_get_image (GstVaapiSurfaceMeta *meta)
{
    g_return_val_if_fail(meta != NULL, NULL);
    if (meta->surface_mem)
	return meta->surface_mem->image;
    else
	return NULL;
}
