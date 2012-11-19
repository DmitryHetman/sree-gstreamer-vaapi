/*
 *  gstvaapivideomemory.h - Gst VA surfacez memory
 *
 *  Copyright (C) 2012 Intel Corporation
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

#ifndef GST_VAAPI_VIDEO_MEMORY_H
#define GST_VAAPI_VIDEO_MEMORY_H

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

G_BEGIN_DECLS

typedef enum {
   GST_VAAPI_VIDEO_MEMORY_NON_MAPPED = 0,
   GST_VAAPI_VIDEO_MEMORY_MAPPED,
   GST_VAAPI_VIDEO_MEMORY_DERIVED
} GstVaapiVideoMemoryMapFlag;

typedef struct _GstVaapiVideoMemory GstVaapiVideoMemory;

struct _GstVaapiVideoMemory {
    GstMemory memory;

    GstVaapiDisplay *display;
    
    GstVaapiSurface *surface;
    GstVaapiChromaType  chroma_type;
    
    GstVaapiImage *image;
    GstVaapiImageRaw *raw_image;
    GstVaapiImageFormat ycbcr_format;

    gsize slice_size;
    gpointer user_data;
    GDestroyNotify notify;

    GstVideoInfo        *info;

    GstVaapiVideoMemoryMapFlag flag;

    /* Cached data for mapping, copied from gstvdpvideomemory */
    GstMapFlags        map_flags;
    guint              n_planes;
    guint8            *cache;
    void *             cached_data[4];
    gint               destination_pitches[4];
};

#define  GST_VAAPI_VIDEO_MEMORY_NAME  "GstVaapiVideoMemory"

/*------------- VaapiVideoAllocator -----------------*/

#define GST_VAAPI_TYPE_VIDEO_ALLOCATOR        (gst_vaapi_video_allocator_get_type())
#define GST_IS_VAAPI_VIDEO_ALLOCATOR(obj)     (GST_IS_OBJECT_TYPE(obj, GST_VAAPI_TYPE_VIDEO_ALLOCATOR))
#define GST_VAAPI_VIDEO_ALLOCATOR_CAST(obj)   ((GstVaapiVideoAllocator *)(obj))
#define GST_VAAPI_VIDEO_ALLOCATOR(obj)        (GST_VAAPI_VIDEO_ALLOCATOR_CAST(obj))

#define  GST_VAAPI_VIDEO_ALLOCATOR_NAME  "GstVaapiVideoAllocator"

typedef struct _GstVaapiVideoAllocator GstVaapiVideoAllocator;
typedef struct _GstVaapiVideoAllocatorClass GstVaapiVideoAllocatorClass;

struct _GstVaapiVideoAllocator {
    GstAllocator parent;
};

struct _GstVaapiVideoAllocatorClass
{
    GstAllocatorClass parent_class;
};

GstMemory *
gst_vaapi_video_memory_new (GstVaapiDisplay *display, GstVideoInfo *info);

gboolean gst_vaapi_video_memory_map(GstVideoMeta * meta, guint plane,
                                  GstMapInfo * info, gpointer * data,
                                  gint * stride, GstMapFlags flags);
gboolean gst_vaapi_video_memory_unmap(GstVideoMeta * meta, guint plane,
                                    GstMapInfo * info);

GType
gst_vaapi_video_allocator_get_type (void);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_MEMORY_H */
