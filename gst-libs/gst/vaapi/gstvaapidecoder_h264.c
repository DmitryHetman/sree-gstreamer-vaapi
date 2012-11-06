/*
 *  gstvaapidecoder_h264.c - H.264 decoder
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

/**
 * SECTION:gstvaapidecoder_h264
 * @short_description: H.264 decoder
 */

#include "sysdeps.h"
#include <string.h>
#include <gst/base/gstadapter.h>
#include <gst/codecparsers/gsth264parser.h>
#include "gstvaapidecoder_h264.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Defined to 1 if strict ordering of DPB is needed. Only useful for debug */
#define USE_STRICT_DPB_ORDERING 0

typedef struct _GstVaapiPictureH264             GstVaapiPictureH264;
typedef struct _GstVaapiSliceH264               GstVaapiSliceH264;

// Used for field_poc[]
#define TOP_FIELD       0
#define BOTTOM_FIELD    1

/* ------------------------------------------------------------------------- */
/* --- H.264 Pictures                                                    --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_PICTURE_H264          (gst_vaapi_picture_h264_get_type())
#define GST_VAAPI_IS_PICTURE_H264(obj)       (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_PICTURE_H264))
#define GST_VAAPI_PICTURE_H264_CAST(obj)     ((GstVaapiPictureH264 *)(obj))
#define GST_VAAPI_PICTURE_H264(obj)          (GST_VAAPI_PICTURE_H264_CAST (obj))  

/*
 * Extended picture flags:
 *
 * @GST_VAAPI_PICTURE_FLAG_IDR: flag that specifies an IDR picture
 * @GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE: flag that specifies
 *     "used for short-term reference"
 * @GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE: flag that specifies
 *     "used for long-term reference"
 * @GST_VAAPI_PICTURE_FLAGS_REFERENCE: mask covering any kind of
 *     reference picture (short-term reference or long-term reference)
 */
enum {
    GST_VAAPI_PICTURE_FLAG_IDR = (GST_VAAPI_PICTURE_FLAG_LAST << 0),

    GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE = (
        GST_VAAPI_PICTURE_FLAG_REFERENCE),
    GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE = (
        GST_VAAPI_PICTURE_FLAG_REFERENCE | (GST_VAAPI_PICTURE_FLAG_LAST << 1)),
    GST_VAAPI_PICTURE_FLAGS_REFERENCE = (
        GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE |
        GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE),
};

#define GST_VAAPI_PICTURE_IS_IDR(picture) \
    (GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_IDR))

#define GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE(picture)      \
    ((GST_VAAPI_PICTURE_FLAGS(picture) &                        \
      GST_VAAPI_PICTURE_FLAGS_REFERENCE) ==                     \
     GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE)

#define GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE(picture)       \
    ((GST_VAAPI_PICTURE_FLAGS(picture) &                        \
      GST_VAAPI_PICTURE_FLAGS_REFERENCE) ==                     \
     GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE)

struct _GstVaapiPictureH264 {
    GstVaapiPicture             base;
    GstH264PPS                 *pps;
    gint32                      field_poc[2];
    gint32                      frame_num;              // Original frame_num from slice_header()
    gint32                      frame_num_wrap;         // Temporary for ref pic marking: FrameNumWrap
    gint32                      long_term_frame_idx;    // Temporary for ref pic marking: LongTermFrameIdx
    gint32                      pic_num;                // Temporary for ref pic marking: PicNum
    gint32                      long_term_pic_num;      // Temporary for ref pic marking: LongTermPicNum
    guint                       output_flag             : 1;
    guint                       output_needed           : 1;
};

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiPictureH264,
                            gst_vaapi_picture_h264)

void
gst_vaapi_picture_h264_destroy(GstVaapiPictureH264 *decoder)
{
    gst_vaapi_picture_destroy(GST_VAAPI_PICTURE_CAST(decoder));
}

gboolean
gst_vaapi_picture_h264_create(
    GstVaapiPictureH264                      *picture,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    if(!gst_vaapi_picture_create(GST_VAAPI_PICTURE_CAST(picture),args)) {
	GST_ERROR("Failed to create VaapiPicture");
	return FALSE;
    }
    return TRUE;
}

void
gst_vaapi_picture_h264_init(GstVaapiPictureH264 *picture)
{
    picture->field_poc[0]       = G_MAXINT32;
    picture->field_poc[1]       = G_MAXINT32;
    picture->output_needed      = FALSE;
}

static inline GstVaapiPictureH264 *
gst_vaapi_picture_h264_new(GstVaapiDecoderH264 *decoder)
{
    GstVaapiCodecObject *object;
    GstVaapiPictureH264 *obj;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    obj = g_slice_new0(GstVaapiPictureH264);
    if (!obj)
        return NULL;

    gst_vaapi_picture_h264_initialize(obj);
    gst_vaapi_picture_h264_init(obj);

    object = gst_vaapi_codec_object_create(
        GST_VAAPI_CODEC_OBJECT_CAST(obj),
        GST_VAAPI_CODEC_BASE(decoder),
        NULL, sizeof(VAPictureParameterBufferH264),
        NULL, 0,
	0
    );

    if (!object)
        return NULL;
    return GST_VAAPI_PICTURE_H264_CAST(object);
}

static inline GstVaapiSliceH264 *
gst_vaapi_picture_h264_get_last_slice(GstVaapiPictureH264 *picture)
{
    g_return_val_if_fail(picture != NULL, NULL);

    if (G_UNLIKELY(picture->base.slices->len < 1))
        return NULL;
    return g_ptr_array_index(picture->base.slices,
        picture->base.slices->len - 1);
}

/* ------------------------------------------------------------------------- */
/* --- Slices                                                            --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_SLICE_H264       (gst_vaapi_slice_h264_get_type())
#define GST_VAAPI_IS_SLICE_H264(obj)    (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_SLICE_H264)) 
#define GST_VAAPI_SLICE_H264_CAST(obj)  ((GstVaapiSliceH264 *)(obj))
#define GST_VAAPI_SLICE_H264 (obj)      (GST_VAAPI_SLICE_H264_CAST(obj)) 

struct _GstVaapiSliceH264 {
    GstVaapiSlice               base;
    GstH264SliceHdr             slice_hdr;              // parsed slice_header()
};

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiSliceH264,
                            gst_vaapi_slice_h264)

void
gst_vaapi_slice_h264_destroy(GstVaapiSliceH264 *slice)
{
    gst_vaapi_slice_destroy(GST_VAAPI_SLICE_CAST(slice));
}

gboolean
gst_vaapi_slice_h264_create(
    GstVaapiSliceH264                        *slice,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    if(!gst_vaapi_slice_create(GST_VAAPI_SLICE_CAST(slice),args)) {
	GST_ERROR("Failed to create VaapiPicture");
	return FALSE;
    }
    return TRUE;
}

void
gst_vaapi_slice_h264_init(GstVaapiSliceH264 *slice)
{
}

static inline GstVaapiSliceH264 *
gst_vaapi_slice_h264_new(
    GstVaapiDecoderH264 *decoder,
    const guint8        *data,
    guint                data_size
)
{
    GstVaapiCodecObject *object;
    GstVaapiSliceH264 *obj;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    obj = g_slice_new0(GstVaapiSliceH264);
    if (!obj)
        return NULL;

    gst_vaapi_slice_h264_initialize(obj);
    gst_vaapi_slice_h264_init(obj);

    object = gst_vaapi_codec_object_create(
        GST_VAAPI_CODEC_OBJECT_CAST(obj),
        GST_VAAPI_CODEC_BASE(decoder),
	NULL, sizeof(VASliceParameterBufferH264),
        data, data_size,
	0
    );

    if (!object)
        return NULL;
    return GST_VAAPI_SLICE_H264_CAST(object);
}

/* ------------------------------------------------------------------------- */
/* --- H.264 Decoder                                                     --- */
/* ------------------------------------------------------------------------- */

G_DEFINE_TYPE(GstVaapiDecoderH264,
              gst_vaapi_decoder_h264,
              GST_VAAPI_TYPE_DECODER)

#define GST_VAAPI_DECODER_H264_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER_H264,   \
                                 GstVaapiDecoderH264Private))

struct _GstVaapiDecoderH264Private {
    GstAdapter                 *adapter;
    GstH264NalParser           *parser;
    /* Last decoded SPS. May not be the last activated one. Just here because
       it may not fit stack memory allocation in decode_sps() */
    GstH264SPS                  last_sps;
    /* Last decoded PPS. May not be the last activated one. Just here because
       it may not fit stack memory allocation in decode_pps() */
    GstH264PPS                  last_pps;
    GstVaapiPictureH264        *current_picture;
    GstVaapiPictureH264        *dpb[16];
    guint                       dpb_count;
    guint                       dpb_size;
    GstVaapiProfile             profile;
    GstVaapiEntrypoint          entrypoint;
    GstVaapiChromaType          chroma_type;
    GstVaapiPictureH264        *short_ref[16];
    guint                       short_ref_count;
    GstVaapiPictureH264        *long_ref[16];
    guint                       long_ref_count;
    GstVaapiPictureH264        *RefPicList0[32];
    guint                       RefPicList0_count;
    GstVaapiPictureH264        *RefPicList1[32];
    guint                       RefPicList1_count;
    guint                       nal_length_size;
    guint                       width;
    guint                       height;
    gint32                      field_poc[2];           // 0:TopFieldOrderCnt / 1:BottomFieldOrderCnt
    gint32                      poc_msb;                // PicOrderCntMsb
    gint32                      poc_lsb;                // pic_order_cnt_lsb (from slice_header())
    gint32                      prev_poc_msb;           // prevPicOrderCntMsb
    gint32                      prev_poc_lsb;           // prevPicOrderCntLsb
    gint32                      frame_num_offset;       // FrameNumOffset
    gint32                      frame_num;              // frame_num (from slice_header())
    gint32                      prev_frame_num;         // prevFrameNum
    gboolean                    prev_pic_has_mmco5;     // prevMmco5Pic
    gboolean                    prev_pic_structure;     // previous picture structure
    guint                       is_constructed          : 1;
    guint                       is_opened               : 1;
    guint                       is_avc                  : 1;
    guint                       has_context             : 1;
    guint			ready_to_dec		: 1;
    guint			reset_context		: 1;
};

static gboolean
exec_ref_pic_marking(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture);

/* Get number of reference frames to use */
static guint
get_max_dec_frame_buffering(GstH264SPS *sps)
{
    guint max_dec_frame_buffering, MaxDpbMbs, PicSizeMbs;

    /* Table A-1 - Level limits */
    switch (sps->level_idc) {
    case 10: MaxDpbMbs = 396;    break;
    case 11: MaxDpbMbs = 900;    break;
    case 12: MaxDpbMbs = 2376;   break;
    case 13: MaxDpbMbs = 2376;   break;
    case 20: MaxDpbMbs = 2376;   break;
    case 21: MaxDpbMbs = 4752;   break;
    case 22: MaxDpbMbs = 8100;   break;
    case 30: MaxDpbMbs = 8100;   break;
    case 31: MaxDpbMbs = 18000;  break;
    case 32: MaxDpbMbs = 20480;  break;
    case 40: MaxDpbMbs = 32768;  break;
    case 41: MaxDpbMbs = 32768;  break;
    case 42: MaxDpbMbs = 34816;  break;
    case 50: MaxDpbMbs = 110400; break;
    case 51: MaxDpbMbs = 184320; break;
    default:
        g_assert(0 && "unhandled level");
        break;
    }

    PicSizeMbs = ((sps->pic_width_in_mbs_minus1 + 1) *
                  (sps->pic_height_in_map_units_minus1 + 1) *
                  (sps->frame_mbs_only_flag ? 1 : 2));
    max_dec_frame_buffering = MaxDpbMbs / PicSizeMbs;

    /* VUI parameters */
    if (sps->vui_parameters_present_flag) {
        GstH264VUIParams * const vui_params = &sps->vui_parameters;
        if (vui_params->bitstream_restriction_flag)
            max_dec_frame_buffering = vui_params->max_dec_frame_buffering;
        else {
            switch (sps->profile_idc) {
            case 44:  // CAVLC 4:4:4 Intra profile
            case 86:  // Scalable High profile
            case 100: // High profile
            case 110: // High 10 profile
            case 122: // High 4:2:2 profile
            case 244: // High 4:4:4 Predictive profile
                if (sps->constraint_set3_flag)
                    max_dec_frame_buffering = 0;
                break;
            }
        }
    }

    if (max_dec_frame_buffering > 16)
        max_dec_frame_buffering = 16;
    else if (max_dec_frame_buffering < sps->num_ref_frames)
        max_dec_frame_buffering = sps->num_ref_frames;
    return MAX(1, max_dec_frame_buffering);
}

static void
dpb_remove_index(GstVaapiDecoderH264 *decoder, guint index)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i, num_pictures = --priv->dpb_count;

    if (USE_STRICT_DPB_ORDERING) {
        for (i = index; i < num_pictures; i++)
            gst_vaapi_picture_replace(&priv->dpb[i], priv->dpb[i + 1]);
    }
    else if (index != num_pictures)
        gst_vaapi_picture_replace(&priv->dpb[index], priv->dpb[num_pictures]);
    gst_vaapi_picture_replace(&priv->dpb[num_pictures], NULL);
}

static inline gboolean
dpb_output(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    /* XXX: update cropping rectangle */
    picture->output_needed = FALSE;
    return gst_vaapi_picture_output(GST_VAAPI_PICTURE_CAST(picture));
}

static gboolean
dpb_bump(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i, lowest_poc_index;
    gboolean success;

    for (i = 0; i < priv->dpb_count; i++) {
        if (priv->dpb[i]->output_needed)
            break;
    }
    if (i == priv->dpb_count)
        return FALSE;

    lowest_poc_index = i++;
    for (; i < priv->dpb_count; i++) {
        GstVaapiPictureH264 * const picture = priv->dpb[i];
        if (picture->output_needed && picture->base.poc < priv->dpb[lowest_poc_index]->base.poc)
            lowest_poc_index = i;
    }
    success = dpb_output(decoder, priv->dpb[lowest_poc_index]);
    if (!GST_VAAPI_PICTURE_IS_REFERENCE(priv->dpb[lowest_poc_index]))
        dpb_remove_index(decoder, lowest_poc_index);
    return success;
}

static void
dpb_clear(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i;

    for (i = 0; i < priv->dpb_count; i++)
        gst_vaapi_picture_replace(&priv->dpb[i], NULL);
    priv->dpb_count = 0;
}

static void
dpb_flush(GstVaapiDecoderH264 *decoder)
{
    while (dpb_bump(decoder))
        ;
    dpb_clear(decoder);
}

static gboolean
dpb_add(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i;
    // Remove all unused pictures
    if (!GST_VAAPI_PICTURE_IS_IDR(picture)) {
        i = 0;
        while (i < priv->dpb_count) {
            GstVaapiPictureH264 * const picture = priv->dpb[i];
            if (!picture->output_needed &&
                !GST_VAAPI_PICTURE_IS_REFERENCE(picture))
                dpb_remove_index(decoder, i);
            else
                i++;
        }
    }

    // C.4.5.1 - Storage and marking of a reference decoded picture into the DPB
    if (GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
        while (priv->dpb_count == priv->dpb_size) {
            if (!dpb_bump(decoder))
                return FALSE;
        }
        gst_vaapi_picture_replace(&priv->dpb[priv->dpb_count++], picture);
        if (picture->output_flag)
            picture->output_needed = TRUE;
    }

    // C.4.5.2 - Storage and marking of a non-reference decoded picture into the DPB
    else {
        if (!picture->output_flag)
            return TRUE;
        while (priv->dpb_count == priv->dpb_size) {
            for (i = 0; i < priv->dpb_count; i++) {
                if (priv->dpb[i]->output_needed &&
                    priv->dpb[i]->base.poc < picture->base.poc)
                    break;
            }
            if (i == priv->dpb_count)
                return dpb_output(decoder, picture);
            if (!dpb_bump(decoder))
                return FALSE;
        }
        gst_vaapi_picture_replace(&priv->dpb[priv->dpb_count++], picture);
        picture->output_needed = TRUE;
    }
    return TRUE;
}

static inline void
dpb_reset(GstVaapiDecoderH264 *decoder, GstH264SPS *sps)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    priv->dpb_size = get_max_dec_frame_buffering(sps);
    GST_DEBUG("DPB size %u", priv->dpb_size);
}

static GstVaapiDecoderStatus
get_status(GstH264ParserResult result)
{
    GstVaapiDecoderStatus status;

    switch (result) {
    case GST_H264_PARSER_OK:
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
    case GST_H264_PARSER_NO_NAL_END:
        status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        break;
    case GST_H264_PARSER_ERROR:
        status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        break;
    default:
        status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        break;
    }
    return status;
}

static void
gst_vaapi_decoder_h264_close(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    gst_vaapi_picture_replace(&priv->current_picture, NULL);

    dpb_clear(decoder);

    if (priv->parser) {
        gst_h264_nal_parser_free(priv->parser);
        priv->parser = NULL;
    }

}

static gboolean
gst_vaapi_decoder_h264_open(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    gst_vaapi_decoder_h264_close(decoder);

    priv->parser = gst_h264_nal_parser_new();
    if (!priv->parser)
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_decoder_h264_destroy(GstVaapiDecoderH264 *decoder)
{
    gst_vaapi_decoder_h264_close(decoder);
}

static gboolean
gst_vaapi_decoder_h264_create(GstVaapiDecoderH264 *decoder)
{
    if (!GST_VAAPI_DECODER_CODEC(decoder))
        return FALSE;
    return TRUE;
}

static guint
h264_get_profile(GstH264SPS *sps)
{
    guint profile = 0;

    switch (sps->profile_idc) {
    case 66:
        profile = GST_VAAPI_PROFILE_H264_BASELINE;
        break;
    case 77:
        profile = GST_VAAPI_PROFILE_H264_MAIN;
        break;
    case 100:
        profile = GST_VAAPI_PROFILE_H264_HIGH;
        break;
    }
    return profile;
}

static guint
h264_get_chroma_type(GstH264SPS *sps)
{
    guint chroma_type = 0;

    switch (sps->chroma_format_idc) {
    case 1:
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
        break;
    case 2:
        chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
        break;
    case 3:
        if (!sps->separate_colour_plane_flag)
            chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
        break;
    }
    return chroma_type;
}

static GstVaapiProfile
get_profile(GstVaapiDecoderH264 *decoder, GstH264SPS *sps)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDisplay * const display = GST_VAAPI_DECODER_DISPLAY(decoder);
    GstVaapiProfile profile, profiles[2];
    guint i, n_profiles = 0;

    profile = h264_get_profile(sps);
    if (!profile)
        return GST_VAAPI_PROFILE_UNKNOWN;

    profiles[n_profiles++] = profile;
    switch (profile) {
    case GST_VAAPI_PROFILE_H264_MAIN:
        profiles[n_profiles++] = GST_VAAPI_PROFILE_H264_HIGH;
        break;
    default:
        break;
    }

    /* If the preferred profile (profiles[0]) matches one that we already
       found, then just return it now instead of searching for it again */
    if (profiles[0] == priv->profile)
        return priv->profile;

    for (i = 0; i < n_profiles; i++) {
        if (gst_vaapi_display_has_decoder(display, profiles[i], priv->entrypoint))
            return profiles[i];
    }
    return GST_VAAPI_PROFILE_UNKNOWN;
}

/* context is only creating from decide_allocation*/
static GstVaapiDecoderStatus
ensure_context(GstVaapiDecoderH264 *decoder, GstH264SPS *sps)
{
    GstVaapiDecoder * const base_decoder = GST_VAAPI_DECODER_CAST(decoder);
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiContextInfo info;
    GstVaapiProfile profile;
    GstVaapiChromaType chroma_type;
    gboolean reset_context = FALSE;

    profile = get_profile(decoder, sps);
    if (!profile) {
        GST_ERROR("unsupported profile_idc %u", sps->profile_idc);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }

    if (priv->profile != profile) {
        GST_DEBUG("profile changed");
        priv->reset_context = TRUE;
        priv->profile = profile;
    }

    chroma_type = h264_get_chroma_type(sps);
    if (!chroma_type || chroma_type != GST_VAAPI_CHROMA_TYPE_YUV420) {
        GST_ERROR("unsupported chroma_format_idc %u", sps->chroma_format_idc);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;
    }

    if (priv->chroma_type != chroma_type) {
        GST_DEBUG("chroma format changed");
        priv->reset_context = TRUE;

        /* XXX: theoritically, we could handle 4:2:2 format */
        if (sps->chroma_format_idc != 1)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;
        priv->chroma_type = chroma_type;
    }

    if (priv->width != sps->width || priv->height != sps->height) {
        GST_DEBUG("size changed");
	/*Fixme: have to merge has_context and reset_context */
	priv->has_context	 = TRUE;
        priv->reset_context      = TRUE;
        priv->width      	 = sps->width;
        priv->height     	 = sps->height;
    }

    gst_vaapi_decoder_set_pixel_aspect_ratio(
        base_decoder,
        sps->vui_parameters.par_n,
        sps->vui_parameters.par_d
    );

    if (!priv->reset_context && priv->has_context)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;
     
    /* Reset DPB */
    if (priv->reset_context)
        dpb_reset(decoder, sps);

    if(priv->reset_context)
        gst_vaapi_decoder_emit_caps_change(GST_VAAPI_DECODER_CAST(decoder), priv->width, priv->height);

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
fill_iq_matrix_4x4(VAIQMatrixBufferH264 *iq_matrix, const GstH264PPS *pps)
{
    const guint8 (* const ScalingList4x4)[6][16] = &pps->scaling_lists_4x4;
    guint i, j;

    /* There are always 6 4x4 scaling lists */
    g_assert(G_N_ELEMENTS(iq_matrix->ScalingList4x4) == 6);
    g_assert(G_N_ELEMENTS(iq_matrix->ScalingList4x4[0]) == 16);

    if (sizeof(iq_matrix->ScalingList4x4[0][0]) == 1)
        memcpy(iq_matrix->ScalingList4x4, *ScalingList4x4,
               sizeof(iq_matrix->ScalingList4x4));
    else {
        for (i = 0; i < G_N_ELEMENTS(iq_matrix->ScalingList4x4); i++) {
            for (j = 0; j < G_N_ELEMENTS(iq_matrix->ScalingList4x4[i]); j++)
                iq_matrix->ScalingList4x4[i][j] = (*ScalingList4x4)[i][j];
        }
    }
}

static void
fill_iq_matrix_8x8(VAIQMatrixBufferH264 *iq_matrix, const GstH264PPS *pps)
{
    const guint8 (* const ScalingList8x8)[6][64] = &pps->scaling_lists_8x8;
    const GstH264SPS * const sps = pps->sequence;
    guint i, j, n;

    /* If chroma_format_idc != 3, there are up to 2 8x8 scaling lists */
    if (!pps->transform_8x8_mode_flag)
        return;

    g_assert(G_N_ELEMENTS(iq_matrix->ScalingList8x8) >= 2);
    g_assert(G_N_ELEMENTS(iq_matrix->ScalingList8x8[0]) == 64);

    if (sizeof(iq_matrix->ScalingList8x8[0][0]) == 1)
        memcpy(iq_matrix->ScalingList8x8, *ScalingList8x8,
               sizeof(iq_matrix->ScalingList8x8));
    else {
        n = (sps->chroma_format_idc != 3) ? 2 : 6;
        for (i = 0; i < n; i++) {
            for (j = 0; j < G_N_ELEMENTS(iq_matrix->ScalingList8x8[i]); j++)
                iq_matrix->ScalingList8x8[i][j] = (*ScalingList8x8)[i][j];
        }
    }
}

static GstVaapiDecoderStatus
ensure_quant_matrix(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    GstVaapiPicture * const base_picture = &picture->base;
    GstH264PPS * const pps = picture->pps;
    GstH264SPS * const sps = pps->sequence;
    VAIQMatrixBufferH264 *iq_matrix;

    base_picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW(H264, decoder);
    if (!base_picture->iq_matrix) {
        GST_ERROR("failed to allocate IQ matrix");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    iq_matrix = base_picture->iq_matrix->param;

    /* XXX: we can only support 4:2:0 or 4:2:2 since ScalingLists8x8[]
       is not large enough to hold lists for 4:4:4 */
    if (sps->chroma_format_idc == 3)
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;

    fill_iq_matrix_4x4(iq_matrix, pps);
    fill_iq_matrix_8x8(iq_matrix, pps);

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_current_picture(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 * const picture = priv->current_picture;
    GstVaapiDecoderStatus status;

    if (!picture)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;

    status = ensure_context(decoder, picture->pps->sequence);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    if (!exec_ref_pic_marking(decoder, picture))
        goto error;
    if (!dpb_add(decoder, picture))
        goto error;
    if (!gst_vaapi_picture_decode(GST_VAAPI_PICTURE_CAST(picture)))
        goto error;
    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

error:
    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
}

static GstVaapiDecoderStatus
decode_sps(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SPS * const sps = &priv->last_sps;
    GstH264ParserResult result;

    GST_DEBUG("decode SPS");

    memset(sps, 0, sizeof(*sps));
    result = gst_h264_parser_parse_sps(priv->parser, nalu, sps, TRUE);
    if (result != GST_H264_PARSER_OK)
        return get_status(result);

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_pps(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = &priv->last_pps;
    GstH264ParserResult result;

    GST_DEBUG("decode PPS");

    memset(pps, 0, sizeof(*pps));
    result = gst_h264_parser_parse_pps(priv->parser, nalu, pps);
    if (result != GST_H264_PARSER_OK)
        return get_status(result);

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sei(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SEIMessage sei;
    GstH264ParserResult result;

    GST_DEBUG("decode SEI");

    memset(&sei, 0, sizeof(sei));
    result = gst_h264_parser_parse_sei(priv->parser, nalu, &sei);
    if (result != GST_H264_PARSER_OK) {
        GST_WARNING("failed to decode SEI, payload type:%d", sei.payloadType);
        return get_status(result);
    }

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderStatus status;

    GST_DEBUG("decode sequence-end");

    status = decode_current_picture(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    dpb_flush(decoder);
    return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
}

/* 8.2.1.1 - Decoding process for picture order count type 0 */
static void
init_picture_poc_0(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    gint32 temp_poc;

    GST_DEBUG("decode picture order count type 0");

    if (GST_VAAPI_PICTURE_IS_IDR(picture)) {
        priv->prev_poc_msb = 0;
        priv->prev_poc_lsb = 0;
    }
    else if (priv->prev_pic_has_mmco5) {
        priv->prev_poc_msb = 0;
        priv->prev_poc_lsb =
            (priv->prev_pic_structure == GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD ?
             0 : priv->field_poc[TOP_FIELD]);
    }
    else {
        priv->prev_poc_msb = priv->poc_msb;
        priv->prev_poc_lsb = priv->poc_lsb;
    }

    // (8-3)
    priv->poc_lsb = slice_hdr->pic_order_cnt_lsb;
    if (priv->poc_lsb < priv->prev_poc_lsb &&
        (priv->prev_poc_lsb - priv->poc_lsb) >= (MaxPicOrderCntLsb / 2))
        priv->poc_msb = priv->prev_poc_msb + MaxPicOrderCntLsb;
    else if (priv->poc_lsb > priv->prev_poc_lsb &&
             (priv->poc_lsb - priv->prev_poc_lsb) > (MaxPicOrderCntLsb / 2))
        priv->poc_msb = priv->prev_poc_msb - MaxPicOrderCntLsb;
    else
        priv->poc_msb = priv->prev_poc_msb;

    temp_poc = priv->poc_msb + priv->poc_lsb;
    switch (picture->base.structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_FRAME:
        // (8-4, 8-5)
        priv->field_poc[TOP_FIELD] = temp_poc;
        priv->field_poc[BOTTOM_FIELD] = temp_poc +
            slice_hdr->delta_pic_order_cnt_bottom;
        break;
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
        // (8-4)
        priv->field_poc[TOP_FIELD] = temp_poc;
        break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
        // (8-5)
        priv->field_poc[BOTTOM_FIELD] = temp_poc;
        break;
    }
}

/* 8.2.1.2 - Decoding process for picture order count type 1 */
static void
init_picture_poc_1(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
    gint32 prev_frame_num_offset, abs_frame_num, expected_poc;
    guint i;

    GST_DEBUG("decode picture order count type 1");

    if (priv->prev_pic_has_mmco5)
        prev_frame_num_offset = 0;
    else
        prev_frame_num_offset = priv->frame_num_offset;

    // (8-6)
    if (GST_VAAPI_PICTURE_IS_IDR(picture))
        priv->frame_num_offset = 0;
    else if (priv->prev_frame_num > priv->frame_num)
        priv->frame_num_offset = prev_frame_num_offset + MaxFrameNum;
    else
        priv->frame_num_offset = prev_frame_num_offset;

    // (8-7)
    if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0)
        abs_frame_num = priv->frame_num_offset + priv->frame_num;
    else
        abs_frame_num = 0;
    if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture) && abs_frame_num > 0)
        abs_frame_num = abs_frame_num - 1;

    if (abs_frame_num > 0) {
        gint32 expected_delta_per_poc_cycle;
        gint32 poc_cycle_cnt, frame_num_in_poc_cycle;

        expected_delta_per_poc_cycle = 0;
        for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
            expected_delta_per_poc_cycle += sps->offset_for_ref_frame[i];

        // (8-8)
        poc_cycle_cnt = (abs_frame_num - 1) /
            sps->num_ref_frames_in_pic_order_cnt_cycle;
        frame_num_in_poc_cycle = (abs_frame_num - 1) %
            sps->num_ref_frames_in_pic_order_cnt_cycle;

        // (8-9)
        expected_poc = poc_cycle_cnt * expected_delta_per_poc_cycle;
        for (i = 0; i <= frame_num_in_poc_cycle; i++)
            expected_poc += sps->offset_for_ref_frame[i];
    }
    else
        expected_poc = 0;
    if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
        expected_poc += sps->offset_for_non_ref_pic;

    // (8-10)
    switch (picture->base.structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_FRAME:
        priv->field_poc[TOP_FIELD] = expected_poc +
            slice_hdr->delta_pic_order_cnt[0];
        priv->field_poc[BOTTOM_FIELD] = priv->field_poc[TOP_FIELD] +
            sps->offset_for_top_to_bottom_field +
            slice_hdr->delta_pic_order_cnt[1];
        break;
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
        priv->field_poc[TOP_FIELD] = expected_poc +
            slice_hdr->delta_pic_order_cnt[0];
        break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
        priv->field_poc[BOTTOM_FIELD] = expected_poc + 
            sps->offset_for_top_to_bottom_field +
            slice_hdr->delta_pic_order_cnt[0];
        break;
    }
}

/* 8.2.1.3 - Decoding process for picture order count type 2 */
static void
init_picture_poc_2(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
    gint32 prev_frame_num_offset, temp_poc;

    GST_DEBUG("decode picture order count type 2");

    if (priv->prev_pic_has_mmco5)
        prev_frame_num_offset = 0;
    else
        prev_frame_num_offset = priv->frame_num_offset;

    // (8-11)
    if (GST_VAAPI_PICTURE_IS_IDR(picture))
        priv->frame_num_offset = 0;
    else if (priv->prev_frame_num > priv->frame_num)
        priv->frame_num_offset = prev_frame_num_offset + MaxFrameNum;
    else
        priv->frame_num_offset = prev_frame_num_offset;

    // (8-12)
    if (GST_VAAPI_PICTURE_IS_IDR(picture))
        temp_poc = 0;
    else if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
        temp_poc = 2 * (priv->frame_num_offset + priv->frame_num) - 1;
    else
        temp_poc = 2 * (priv->frame_num_offset + priv->frame_num);

    // (8-13)
    if (picture->base.structure != GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
        priv->field_poc[TOP_FIELD] = temp_poc;
    if (picture->base.structure != GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
        priv->field_poc[BOTTOM_FIELD] = temp_poc;
}

/* 8.2.1 - Decoding process for picture order count */
static void
init_picture_poc(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;

    switch (sps->pic_order_cnt_type) {
    case 0:
        init_picture_poc_0(decoder, picture, slice_hdr);
        break;
    case 1:
        init_picture_poc_1(decoder, picture, slice_hdr);
        break;
    case 2:
        init_picture_poc_2(decoder, picture, slice_hdr);
        break;
    }

    if (picture->base.structure != GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
        picture->field_poc[TOP_FIELD] = priv->field_poc[TOP_FIELD];
    if (picture->base.structure != GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
        picture->field_poc[BOTTOM_FIELD] = priv->field_poc[BOTTOM_FIELD];
    picture->base.poc = MIN(picture->field_poc[0], picture->field_poc[1]);
}

static int
compare_picture_pic_num_dec(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picB->pic_num - picA->pic_num;
}

static int
compare_picture_long_term_pic_num_inc(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picA->long_term_pic_num - picB->long_term_pic_num;
}

static int
compare_picture_poc_dec(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picB->base.poc - picA->base.poc;
}

static int
compare_picture_poc_inc(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picA->base.poc - picB->base.poc;
}

static int
compare_picture_frame_num_wrap_dec(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picB->frame_num_wrap - picA->frame_num_wrap;
}

static int
compare_picture_long_term_frame_idx_inc(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picA->long_term_frame_idx - picB->long_term_frame_idx;
}

/* 8.2.4.1 - Decoding process for picture numbers */
static void
init_picture_refs_pic_num(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
    guint i;

    GST_DEBUG("decode picture numbers");

    for (i = 0; i < priv->short_ref_count; i++) {
        GstVaapiPictureH264 * const pic = priv->short_ref[i];

        // (8-27)
        if (pic->frame_num > priv->frame_num)
            pic->frame_num_wrap = pic->frame_num - MaxFrameNum;
        else
            pic->frame_num_wrap = pic->frame_num;

        // (8-28, 8-30, 8-31)
        if (GST_VAAPI_PICTURE_IS_FRAME(picture))
            pic->pic_num = pic->frame_num_wrap;
        else {
            if (pic->base.structure == picture->base.structure)
                pic->pic_num = 2 * pic->frame_num_wrap + 1;
            else
                pic->pic_num = 2 * pic->frame_num_wrap;
        }
    }

    for (i = 0; i < priv->long_ref_count; i++) {
        GstVaapiPictureH264 * const pic = priv->long_ref[i];

        // (8-29, 8-32, 8-33)
        if (GST_VAAPI_PICTURE_IS_FRAME(picture))
            pic->long_term_pic_num = pic->long_term_frame_idx;
        else {
            if (pic->base.structure == picture->base.structure)
                pic->long_term_pic_num = 2 * pic->long_term_frame_idx + 1;
            else
                pic->long_term_pic_num = 2 * pic->long_term_frame_idx;
        }
    }
}

#define SORT_REF_LIST(list, n, compare_func) \
    qsort(list, n, sizeof(*(list)), compare_picture_##compare_func)

static void
init_picture_refs_p_slice(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 **ref_list;
    guint i;

    GST_DEBUG("decode reference picture list for P and SP slices");

    if (GST_VAAPI_PICTURE_IS_FRAME(picture)) {
        /* 8.2.4.2.1 - P and SP slices in frames */
        if (priv->short_ref_count > 0) {
            ref_list = priv->RefPicList0;
            for (i = 0; i < priv->short_ref_count; i++)
                ref_list[i] = priv->short_ref[i];
            SORT_REF_LIST(ref_list, i, pic_num_dec);
            priv->RefPicList0_count += i;
        }

        if (priv->long_ref_count > 0) {
            ref_list = &priv->RefPicList0[priv->RefPicList0_count];
            for (i = 0; i < priv->long_ref_count; i++)
                ref_list[i] = priv->long_ref[i];
            SORT_REF_LIST(ref_list, i, long_term_pic_num_inc);
            priv->RefPicList0_count += i;
        }
    }
    else {
        /* 8.2.4.2.2 - P and SP slices in fields */
        GstVaapiPictureH264 *short_ref[32];
        guint short_ref_count = 0;
        GstVaapiPictureH264 *long_ref[32];
        guint long_ref_count = 0;

        // XXX: handle second field if current field is marked as
        // "used for short-term reference"
        if (priv->short_ref_count > 0) {
            for (i = 0; i < priv->short_ref_count; i++)
                short_ref[i] = priv->short_ref[i];
            SORT_REF_LIST(short_ref, i, frame_num_wrap_dec);
            short_ref_count = i;
        }

        // XXX: handle second field if current field is marked as
        // "used for long-term reference"
        if (priv->long_ref_count > 0) {
            for (i = 0; i < priv->long_ref_count; i++)
                long_ref[i] = priv->long_ref[i];
            SORT_REF_LIST(long_ref, i, long_term_frame_idx_inc);
            long_ref_count = i;
        }

        // XXX: handle 8.2.4.2.5
    }
}

static void
init_picture_refs_b_slice(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 **ref_list;
    guint i, n;

    GST_DEBUG("decode reference picture list for B slices");

    if (GST_VAAPI_PICTURE_IS_FRAME(picture)) {
        /* 8.2.4.2.3 - B slices in frames */

        /* RefPicList0 */
        if (priv->short_ref_count > 0) {
            // 1. Short-term references
            ref_list = priv->RefPicList0;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc < picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            priv->RefPicList0_count += n;

            ref_list = &priv->RefPicList0[priv->RefPicList0_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc >= picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            priv->RefPicList0_count += n;
        }

        if (priv->long_ref_count > 0) {
            // 2. Long-term references
            ref_list = &priv->RefPicList0[priv->RefPicList0_count];
            for (n = 0, i = 0; i < priv->long_ref_count; i++)
                ref_list[n++] = priv->long_ref[i];
            SORT_REF_LIST(ref_list, n, long_term_pic_num_inc);
            priv->RefPicList0_count += n;
        }

        /* RefPicList1 */
        if (priv->short_ref_count > 0) {
            // 1. Short-term references
            ref_list = priv->RefPicList1;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc > picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            priv->RefPicList1_count += n;

            ref_list = &priv->RefPicList1[priv->RefPicList1_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc <= picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            priv->RefPicList1_count += n;
        }

        if (priv->long_ref_count > 0) {
            // 2. Long-term references
            ref_list = &priv->RefPicList1[priv->RefPicList1_count];
            for (n = 0, i = 0; i < priv->long_ref_count; i++)
                ref_list[n++] = priv->long_ref[i];
            SORT_REF_LIST(ref_list, n, long_term_pic_num_inc);
            priv->RefPicList1_count += n;
        }
    }
    else {
        /* 8.2.4.2.4 - B slices in fields */
        GstVaapiPictureH264 *short_ref0[32];
        guint short_ref0_count = 0;
        GstVaapiPictureH264 *short_ref1[32];
        guint short_ref1_count = 0;
        GstVaapiPictureH264 *long_ref[32];
        guint long_ref_count = 0;

        /* refFrameList0ShortTerm */
        if (priv->short_ref_count > 0) {
            ref_list = short_ref0;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc <= picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            short_ref0_count += n;

            ref_list = &short_ref0[short_ref0_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc > picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            short_ref0_count += n;
        }

        /* refFrameList1ShortTerm */
        if (priv->short_ref_count > 0) {
            ref_list = short_ref1;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc > picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            short_ref1_count += n;

            ref_list = &short_ref1[short_ref1_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->base.poc <= picture->base.poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            short_ref1_count += n;
        }

        /* refFrameListLongTerm */
        if (priv->long_ref_count > 0) {
            for (i = 0; i < priv->long_ref_count; i++)
                long_ref[i] = priv->long_ref[i];
            SORT_REF_LIST(long_ref, i, long_term_frame_idx_inc);
            long_ref_count = i;
        }

        // XXX: handle 8.2.4.2.5
    }

    /* Check whether RefPicList1 is identical to RefPicList0, then
       swap if necessary */
    if (priv->RefPicList1_count > 1 &&
        priv->RefPicList1_count == priv->RefPicList0_count &&
        memcmp(priv->RefPicList0, priv->RefPicList1,
               priv->RefPicList0_count * sizeof(priv->RefPicList0[0])) == 0) {
        GstVaapiPictureH264 * const tmp = priv->RefPicList1[0];
        priv->RefPicList1[0] = priv->RefPicList1[1];
        priv->RefPicList1[1] = tmp;
    }
}

#undef SORT_REF_LIST

static gboolean
remove_reference_at(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264 **pictures,
    guint                *picture_count,
    guint                 index
)
{
    guint num_pictures = *picture_count;
    GstVaapiPictureH264 *picture;

    g_return_val_if_fail(index < num_pictures, FALSE);

    picture = pictures[index];
    GST_VAAPI_PICTURE_FLAG_UNSET(picture, GST_VAAPI_PICTURE_FLAGS_REFERENCE);

    if (index != --num_pictures)
        pictures[index] = pictures[num_pictures];
    pictures[num_pictures] = NULL;
    *picture_count = num_pictures;
    return TRUE;
}

static gint
find_short_term_reference(GstVaapiDecoderH264 *decoder, gint32 pic_num)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i;

    for (i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->pic_num == pic_num)
            return i;
    }
    GST_ERROR("found no short-term reference picture with PicNum = %d",
              pic_num);
    return -1;
}

static gint
find_long_term_reference(GstVaapiDecoderH264 *decoder, gint32 long_term_pic_num)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i;

    for (i = 0; i < priv->long_ref_count; i++) {
        if (priv->long_ref[i]->long_term_pic_num == long_term_pic_num)
            return i;
    }
    GST_ERROR("found no long-term reference picture with LongTermPicNum = %d",
              long_term_pic_num);
    return -1;
}

static void
exec_picture_refs_modification_1(
    GstVaapiDecoderH264           *decoder,
    GstVaapiPictureH264           *picture,
    GstH264SliceHdr               *slice_hdr,
    guint                          list
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    GstH264RefPicListModification *ref_pic_list_modification;
    guint num_ref_pic_list_modifications;
    GstVaapiPictureH264 **ref_list;
    guint *ref_list_count_ptr, ref_list_count, ref_list_idx = 0;
    guint i, j, n, num_refs;
    gint found_ref_idx;
    gint32 MaxPicNum, CurrPicNum, picNumPred;

    GST_DEBUG("modification process of reference picture list %u", list);

    if (list == 0) {
        ref_pic_list_modification      = slice_hdr->ref_pic_list_modification_l0;
        num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l0;
        ref_list                       = priv->RefPicList0;
        ref_list_count_ptr             = &priv->RefPicList0_count;
        num_refs                       = slice_hdr->num_ref_idx_l0_active_minus1 + 1;
    }
    else {
        ref_pic_list_modification      = slice_hdr->ref_pic_list_modification_l1;
        num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l1;
        ref_list                       = priv->RefPicList1;
        ref_list_count_ptr             = &priv->RefPicList1_count;
        num_refs                       = slice_hdr->num_ref_idx_l1_active_minus1 + 1;
    }
    ref_list_count = *ref_list_count_ptr;

    if (!GST_VAAPI_PICTURE_IS_FRAME(picture)) {
        MaxPicNum  = 1 << (sps->log2_max_frame_num_minus4 + 5); // 2 * MaxFrameNum
        CurrPicNum = 2 * slice_hdr->frame_num + 1;              // 2 * frame_num + 1
    }
    else {
        MaxPicNum  = 1 << (sps->log2_max_frame_num_minus4 + 4); // MaxFrameNum
        CurrPicNum = slice_hdr->frame_num;                      // frame_num
    }

    picNumPred = CurrPicNum;

    for (i = 0; i < num_ref_pic_list_modifications; i++) {
        GstH264RefPicListModification * const l = &ref_pic_list_modification[i];
        if (l->modification_of_pic_nums_idc == 3)
            break;

        /* 8.2.4.3.1 - Short-term reference pictures */
        if (l->modification_of_pic_nums_idc == 0 || l->modification_of_pic_nums_idc == 1) {
            gint32 abs_diff_pic_num = l->value.abs_diff_pic_num_minus1 + 1;
            gint32 picNum, picNumNoWrap;

            // (8-34)
            if (l->modification_of_pic_nums_idc == 0) {
                picNumNoWrap = picNumPred - abs_diff_pic_num;
                if (picNumNoWrap < 0)
                    picNumNoWrap += MaxPicNum;
            }

            // (8-35)
            else {
                picNumNoWrap = picNumPred + abs_diff_pic_num;
                if (picNumNoWrap >= MaxPicNum)
                    picNumNoWrap -= MaxPicNum;
            }
            picNumPred = picNumNoWrap;

            // (8-36)
            picNum = picNumNoWrap;
            if (picNum > CurrPicNum)
                picNum -= MaxPicNum;

            // (8-37)
            for (j = num_refs; j > ref_list_idx; j--)
                ref_list[j] = ref_list[j - 1];
            found_ref_idx = find_short_term_reference(decoder, picNum);
            ref_list[ref_list_idx++] =
                found_ref_idx >= 0 ? priv->short_ref[found_ref_idx] : NULL;
            n = ref_list_idx;
            for (j = ref_list_idx; j <= num_refs; j++) {
                gint32 PicNumF;
                if (!ref_list[j])
                    continue;
                PicNumF =
                    GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE(ref_list[j]) ?
                    ref_list[j]->pic_num : MaxPicNum;
                if (PicNumF != picNum)
                    ref_list[n++] = ref_list[j];
            }
        }

        /* 8.2.4.3.2 - Long-term reference pictures */
        else {

            for (j = num_refs; j > ref_list_idx; j--)
                ref_list[j] = ref_list[j - 1];
            found_ref_idx =
                find_long_term_reference(decoder, l->value.long_term_pic_num);
            ref_list[ref_list_idx++] =
                found_ref_idx >= 0 ? priv->long_ref[found_ref_idx] : NULL;
            n = ref_list_idx;
            for (j = ref_list_idx; j <= num_refs; j++) {
                gint32 LongTermPicNumF;
                if (!ref_list[j])
                    continue;
                LongTermPicNumF =
                    GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE(ref_list[j]) ?
                    ref_list[j]->long_term_pic_num : INT_MAX;
                if (LongTermPicNumF != l->value.long_term_pic_num)
                    ref_list[n++] = ref_list[j];
            }
        }
    }

#if DEBUG
    for (i = 0; i < num_refs; i++)
        if (!ref_list[i])
            GST_ERROR("list %u entry %u is empty", list, i);
#endif
    *ref_list_count_ptr = num_refs;
}

/* 8.2.4.3 - Modification process for reference picture lists */
static void
exec_picture_refs_modification(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GST_DEBUG("execute ref_pic_list_modification()");

    /* RefPicList0 */
    if (!GST_H264_IS_I_SLICE(slice_hdr) && !GST_H264_IS_SI_SLICE(slice_hdr) &&
        slice_hdr->ref_pic_list_modification_flag_l0)
        exec_picture_refs_modification_1(decoder, picture, slice_hdr, 0);

    /* RefPicList1 */
    if (GST_H264_IS_B_SLICE(slice_hdr) &&
        slice_hdr->ref_pic_list_modification_flag_l1)
        exec_picture_refs_modification_1(decoder, picture, slice_hdr, 1);
}

static void
init_picture_ref_lists(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i, short_ref_count, long_ref_count;

    short_ref_count = 0;
    long_ref_count  = 0;
    for (i = 0; i < priv->dpb_count; i++) {
        GstVaapiPictureH264 * const picture = priv->dpb[i];

        if (GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE(picture))
            priv->short_ref[short_ref_count++] = picture;
        else if (GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE(picture))
            priv->long_ref[long_ref_count++] = picture;
    }

    for (i = short_ref_count; i < priv->short_ref_count; i++)
        priv->short_ref[i] = NULL;
    priv->short_ref_count = short_ref_count;

    for (i = long_ref_count; i < priv->long_ref_count; i++)
        priv->long_ref[i] = NULL;
    priv->long_ref_count = long_ref_count;
}

static void
init_picture_refs(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPicture * const base_picture = &picture->base;
    guint i, num_refs;

    init_picture_ref_lists(decoder);
    init_picture_refs_pic_num(decoder, picture, slice_hdr);

    priv->RefPicList0_count = 0;
    priv->RefPicList1_count = 0;

    switch (base_picture->type) {
    case GST_VAAPI_PICTURE_TYPE_P:
    case GST_VAAPI_PICTURE_TYPE_SP:
        init_picture_refs_p_slice(decoder, picture, slice_hdr);
        break;
    case GST_VAAPI_PICTURE_TYPE_B:
        init_picture_refs_b_slice(decoder, picture, slice_hdr);
        break;
    default:
        break;
    }

    exec_picture_refs_modification(decoder, picture, slice_hdr);

    switch (base_picture->type) {
    case GST_VAAPI_PICTURE_TYPE_B:
        num_refs = 1 + slice_hdr->num_ref_idx_l1_active_minus1;
        for (i = priv->RefPicList1_count; i < num_refs; i++)
            priv->RefPicList1[i] = NULL;
        priv->RefPicList1_count = num_refs;

        // fall-through
    case GST_VAAPI_PICTURE_TYPE_P:
    case GST_VAAPI_PICTURE_TYPE_SP:
        num_refs = 1 + slice_hdr->num_ref_idx_l0_active_minus1;
        for (i = priv->RefPicList0_count; i < num_refs; i++)
            priv->RefPicList0[i] = NULL;
        priv->RefPicList0_count = num_refs;
        break;
    default:
        break;
    }
}

static gboolean
init_picture(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr,
    GstH264NalUnit      *nalu
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPicture * const base_picture = &picture->base;

    priv->prev_frame_num        = priv->frame_num;
    priv->frame_num             = slice_hdr->frame_num;
    picture->frame_num          = priv->frame_num;
    picture->frame_num_wrap     = priv->frame_num;
    picture->output_flag        = TRUE; /* XXX: conformant to Annex A only */
    base_picture->pts           = gst_adapter_prev_timestamp(priv->adapter, NULL);

    /* Reset decoder state for IDR pictures */
    if (nalu->type == GST_H264_NAL_SLICE_IDR) {
        GST_DEBUG("<IDR>");
        GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_IDR);
        dpb_flush(decoder);
    }

    /* Initialize slice type */
    switch (slice_hdr->type % 5) {
    case GST_H264_P_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_P;
        break;
    case GST_H264_B_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_B;
        break;
    case GST_H264_I_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_I;
        break;
    case GST_H264_SP_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_SP;
        break;
    case GST_H264_SI_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_SI;
        break;
    }

    /* Initialize picture structure */
    if (!slice_hdr->field_pic_flag)
        base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    else if (!slice_hdr->bottom_field_flag)
        base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD;
    else
        base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD;

    /* Initialize reference flags */
    if (nalu->ref_idc) {
        GstH264DecRefPicMarking * const dec_ref_pic_marking =
            &slice_hdr->dec_ref_pic_marking;

        if (GST_VAAPI_PICTURE_IS_IDR(picture) &&
            dec_ref_pic_marking->long_term_reference_flag)
            GST_VAAPI_PICTURE_FLAG_SET(picture,
                GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE);
        else
            GST_VAAPI_PICTURE_FLAG_SET(picture,
                GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE);
    }

    init_picture_poc(decoder, picture, slice_hdr);
    init_picture_refs(decoder, picture, slice_hdr);
    return TRUE;
}

/* 8.2.5.3 - Sliding window decoded reference picture marking process */
static gboolean
exec_ref_pic_marking_sliding_window(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = priv->current_picture->pps;
    GstH264SPS * const sps = pps->sequence;
    guint i, max_num_ref_frames, lowest_frame_num_index;
    gint32 lowest_frame_num;

    GST_DEBUG("reference picture marking process (sliding window)");

    max_num_ref_frames = sps->num_ref_frames;
    if (max_num_ref_frames == 0)
        max_num_ref_frames = 1;

    if (priv->short_ref_count + priv->long_ref_count < max_num_ref_frames)
        return TRUE;
    if (priv->short_ref_count < 1)
        return FALSE;

    lowest_frame_num = priv->short_ref[0]->frame_num_wrap;
    lowest_frame_num_index = 0;
    for (i = 1; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->frame_num_wrap < lowest_frame_num) {
            lowest_frame_num = priv->short_ref[i]->frame_num_wrap;
            lowest_frame_num_index = i;
        }
    }

    remove_reference_at(
        decoder,
        priv->short_ref, &priv->short_ref_count,
        lowest_frame_num_index
    );
    return TRUE;
}

static inline gint32
get_picNumX(GstVaapiPictureH264 *picture, GstH264RefPicMarking *ref_pic_marking)
{
    gint32 pic_num;

    if (GST_VAAPI_PICTURE_IS_FRAME(picture))
        pic_num = picture->frame_num_wrap;
    else
        pic_num = 2 * picture->frame_num_wrap + 1;
    pic_num -= ref_pic_marking->difference_of_pic_nums_minus1 + 1;
    return pic_num;
}

/* 8.2.5.4.1. Mark short-term reference picture as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_1(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264  *picture,
    GstH264RefPicMarking *ref_pic_marking
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    gint32 i, picNumX;

    picNumX = get_picNumX(picture, ref_pic_marking);
    i = find_short_term_reference(decoder, picNumX);
    if (i < 0)
        return;
    remove_reference_at(decoder, priv->short_ref, &priv->short_ref_count, i);
}

/* 8.2.5.4.2. Mark long-term reference picture as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_2(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264  *picture,
    GstH264RefPicMarking *ref_pic_marking
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    gint32 i;

    i = find_long_term_reference(decoder, ref_pic_marking->long_term_pic_num);
    if (i < 0)
        return;
    remove_reference_at(decoder, priv->long_ref, &priv->long_ref_count, i);
}

/* 8.2.5.4.3. Assign LongTermFrameIdx to a short-term reference picture */
static void
exec_ref_pic_marking_adaptive_mmco_3(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264  *picture,
    GstH264RefPicMarking *ref_pic_marking
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    gint32 i, picNumX;

    for (i = 0; i < priv->long_ref_count; i++) {
        if (priv->long_ref[i]->long_term_frame_idx == ref_pic_marking->long_term_frame_idx)
            break;
    }
    if (i != priv->long_ref_count)
        remove_reference_at(decoder, priv->long_ref, &priv->long_ref_count, i);

    picNumX = get_picNumX(picture, ref_pic_marking);
    i = find_short_term_reference(decoder, picNumX);
    if (i < 0)
        return;

    picture = priv->short_ref[i];
    remove_reference_at(decoder, priv->short_ref, &priv->short_ref_count, i);
    priv->long_ref[priv->long_ref_count++] = picture;

    picture->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
    GST_VAAPI_PICTURE_FLAG_SET(picture,
        GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE);
}

/* 8.2.5.4.4. Mark pictures with LongTermFramIdx > max_long_term_frame_idx
 * as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_4(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264  *picture,
    GstH264RefPicMarking *ref_pic_marking
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    gint32 i, long_term_frame_idx;

    long_term_frame_idx = ref_pic_marking->max_long_term_frame_idx_plus1 - 1;

    for (i = 0; i < priv->long_ref_count; i++) {
        if (priv->long_ref[i]->long_term_frame_idx <= long_term_frame_idx)
            continue;
        remove_reference_at(decoder, priv->long_ref, &priv->long_ref_count, i);
        i--;
    }
}

/* 8.2.5.4.5. Mark all reference pictures as "unused for reference" */
static void
exec_ref_pic_marking_adaptive_mmco_5(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264  *picture,
    GstH264RefPicMarking *ref_pic_marking
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    dpb_flush(decoder);

    priv->prev_pic_has_mmco5 = TRUE;

    /* The picture shall be inferred to have had frame_num equal to 0 (7.4.3) */
    priv->frame_num = 0;
    priv->frame_num_offset = 0;
    picture->frame_num = 0;

    /* Update TopFieldOrderCnt and BottomFieldOrderCnt (8.2.1) */
    if (picture->base.structure != GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
        picture->field_poc[TOP_FIELD] -= picture->base.poc;
    if (picture->base.structure != GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
        picture->field_poc[BOTTOM_FIELD] -= picture->base.poc;
    picture->base.poc = 0;
}

/* 8.2.5.4.6. Assign a long-term frame index to the current picture */
static void
exec_ref_pic_marking_adaptive_mmco_6(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264  *picture,
    GstH264RefPicMarking *ref_pic_marking
)
{
    picture->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
    GST_VAAPI_PICTURE_FLAG_SET(picture,
        GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE);
}

/* 8.2.5.4. Adaptive memory control decoded reference picture marking process */
static gboolean
exec_ref_pic_marking_adaptive(
    GstVaapiDecoderH264     *decoder,
    GstVaapiPictureH264     *picture,
    GstH264DecRefPicMarking *dec_ref_pic_marking
)
{
    guint i;

    GST_DEBUG("reference picture marking process (adaptive memory control)");

    typedef void (*exec_ref_pic_marking_adaptive_mmco_func)(
        GstVaapiDecoderH264  *decoder,
        GstVaapiPictureH264  *picture,
        GstH264RefPicMarking *ref_pic_marking
    );

    static const exec_ref_pic_marking_adaptive_mmco_func mmco_funcs[] = {
        NULL,
        exec_ref_pic_marking_adaptive_mmco_1,
        exec_ref_pic_marking_adaptive_mmco_2,
        exec_ref_pic_marking_adaptive_mmco_3,
        exec_ref_pic_marking_adaptive_mmco_4,
        exec_ref_pic_marking_adaptive_mmco_5,
        exec_ref_pic_marking_adaptive_mmco_6,
    };

    for (i = 0; i < dec_ref_pic_marking->n_ref_pic_marking; i++) {
        GstH264RefPicMarking * const ref_pic_marking =
            &dec_ref_pic_marking->ref_pic_marking[i];

        const guint mmco = ref_pic_marking->memory_management_control_operation;
        if (mmco < G_N_ELEMENTS(mmco_funcs) && mmco_funcs[mmco])
            mmco_funcs[mmco](decoder, picture, ref_pic_marking);
        else {
            GST_ERROR("unhandled MMCO %u", mmco);
            return FALSE;
        }
    }
    return TRUE;
}

/* 8.2.5 - Execute reference picture marking process */
static gboolean
exec_ref_pic_marking(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    priv->prev_pic_has_mmco5 = FALSE;
    priv->prev_pic_structure = picture->base.structure;

    if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
        return TRUE;

    if (!GST_VAAPI_PICTURE_IS_IDR(picture)) {
        GstVaapiSliceH264 * const slice =
            gst_vaapi_picture_h264_get_last_slice(picture);
        GstH264DecRefPicMarking * const dec_ref_pic_marking =
            &slice->slice_hdr.dec_ref_pic_marking;
        if (dec_ref_pic_marking->adaptive_ref_pic_marking_mode_flag) {
            if (!exec_ref_pic_marking_adaptive(decoder, picture, dec_ref_pic_marking))
                return FALSE;
        }
        else {
            if (!exec_ref_pic_marking_sliding_window(decoder))
                return FALSE;
        }
    }
    return TRUE;
}

static void
vaapi_init_picture(VAPictureH264 *pic)
{
    pic->picture_id           = VA_INVALID_ID;
    pic->frame_idx            = 0;
    pic->flags                = VA_PICTURE_H264_INVALID;
    pic->TopFieldOrderCnt     = 0;
    pic->BottomFieldOrderCnt  = 0;
}

static void
vaapi_fill_picture(VAPictureH264 *pic, GstVaapiPictureH264 *picture)
{
    pic->picture_id = picture->base.surface_id;
    pic->flags = 0;

    if (GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE(picture)) {
        pic->flags |= VA_PICTURE_H264_LONG_TERM_REFERENCE;
        pic->frame_idx = picture->long_term_frame_idx;
    }
    else {
        if (GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE(picture))
            pic->flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        pic->frame_idx = picture->frame_num;
    }

    switch (picture->base.structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_FRAME:
        pic->TopFieldOrderCnt = picture->field_poc[TOP_FIELD];
        pic->BottomFieldOrderCnt = picture->field_poc[BOTTOM_FIELD];
        break;
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
        pic->flags |= VA_PICTURE_H264_TOP_FIELD;
        pic->TopFieldOrderCnt = picture->field_poc[TOP_FIELD];
        pic->BottomFieldOrderCnt = 0;
        break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
        pic->flags |= VA_PICTURE_H264_BOTTOM_FIELD;
        pic->BottomFieldOrderCnt = picture->field_poc[BOTTOM_FIELD];
        pic->TopFieldOrderCnt = 0;
        break;
    }
}

static gboolean
fill_picture(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr,
    GstH264NalUnit      *nalu
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPicture * const base_picture = &picture->base;
    GstH264PPS * const pps = picture->pps;
    GstH264SPS * const sps = pps->sequence;
    VAPictureParameterBufferH264 * const pic_param = base_picture->param;
    guint i, n;

    /* Fill in VAPictureParameterBufferH264 */
    vaapi_fill_picture(&pic_param->CurrPic, picture);
    for (i = 0, n = 0; i < priv->short_ref_count; i++, n++)
        vaapi_fill_picture(&pic_param->ReferenceFrames[n], priv->short_ref[i]);
    for (i = 0; i < priv->long_ref_count; i++, n++)
        vaapi_fill_picture(&pic_param->ReferenceFrames[n], priv->long_ref[i]);
    for (; n < G_N_ELEMENTS(pic_param->ReferenceFrames); n++)
        vaapi_init_picture(&pic_param->ReferenceFrames[n]);

#define COPY_FIELD(s, f) \
    pic_param->f = (s)->f

#define COPY_BFM(a, s, f) \
    pic_param->a.bits.f = (s)->f

    pic_param->picture_width_in_mbs_minus1  = ((priv->width + 15) >> 4)  - 1;
    pic_param->picture_height_in_mbs_minus1 = ((priv->height + 15) >> 4) - 1;
    pic_param->frame_num                    = priv->frame_num;

    COPY_FIELD(sps, bit_depth_luma_minus8);
    COPY_FIELD(sps, bit_depth_chroma_minus8);
    COPY_FIELD(sps, num_ref_frames);
    COPY_FIELD(pps, num_slice_groups_minus1);
    COPY_FIELD(pps, slice_group_map_type);
    COPY_FIELD(pps, slice_group_change_rate_minus1);
    COPY_FIELD(pps, pic_init_qp_minus26);
    COPY_FIELD(pps, pic_init_qs_minus26);
    COPY_FIELD(pps, chroma_qp_index_offset);
    COPY_FIELD(pps, second_chroma_qp_index_offset);

    pic_param->seq_fields.value                                         = 0; /* reset all bits */
    pic_param->seq_fields.bits.residual_colour_transform_flag           = sps->separate_colour_plane_flag;
    pic_param->seq_fields.bits.MinLumaBiPredSize8x8                     = sps->level_idc >= 31; /* A.3.3.2 */

    COPY_BFM(seq_fields, sps, chroma_format_idc);
    COPY_BFM(seq_fields, sps, gaps_in_frame_num_value_allowed_flag);
    COPY_BFM(seq_fields, sps, frame_mbs_only_flag); 
    COPY_BFM(seq_fields, sps, mb_adaptive_frame_field_flag); 
    COPY_BFM(seq_fields, sps, direct_8x8_inference_flag); 
    COPY_BFM(seq_fields, sps, log2_max_frame_num_minus4);
    COPY_BFM(seq_fields, sps, pic_order_cnt_type);
    COPY_BFM(seq_fields, sps, log2_max_pic_order_cnt_lsb_minus4);
    COPY_BFM(seq_fields, sps, delta_pic_order_always_zero_flag);

    pic_param->pic_fields.value                                         = 0; /* reset all bits */
    pic_param->pic_fields.bits.field_pic_flag                           = slice_hdr->field_pic_flag;
    pic_param->pic_fields.bits.reference_pic_flag                       = GST_VAAPI_PICTURE_IS_REFERENCE(picture);

    COPY_BFM(pic_fields, pps, entropy_coding_mode_flag);
    COPY_BFM(pic_fields, pps, weighted_pred_flag);
    COPY_BFM(pic_fields, pps, weighted_bipred_idc);
    COPY_BFM(pic_fields, pps, transform_8x8_mode_flag);
    COPY_BFM(pic_fields, pps, constrained_intra_pred_flag);
    COPY_BFM(pic_fields, pps, pic_order_present_flag);
    COPY_BFM(pic_fields, pps, deblocking_filter_control_present_flag);
    COPY_BFM(pic_fields, pps, redundant_pic_cnt_present_flag);
    return TRUE;
}

/* Detection of the first VCL NAL unit of a primary coded picture (7.4.1.2.4) */
static gboolean
is_new_picture(
    GstVaapiDecoderH264 *decoder,
    GstH264NalUnit      *nalu,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    GstVaapiSliceH264 *slice;
    GstH264SliceHdr *prev_slice_hdr;

    if (!priv->current_picture)
        return TRUE;

    slice = gst_vaapi_picture_h264_get_last_slice(priv->current_picture);
    if (!slice)
        return FALSE;
    prev_slice_hdr = &slice->slice_hdr;

#define CHECK_EXPR(expr, field_name) do {              \
        if (!(expr)) {                                 \
            GST_DEBUG(field_name " differs in value"); \
            return TRUE;                               \
        }                                              \
    } while (0)

#define CHECK_VALUE(new_slice_hdr, old_slice_hdr, field) \
    CHECK_EXPR(((new_slice_hdr)->field == (old_slice_hdr)->field), #field)

    /* frame_num differs in value, regardless of inferred values to 0 */
    CHECK_VALUE(slice_hdr, prev_slice_hdr, frame_num);

    /* pic_parameter_set_id differs in value */
    CHECK_VALUE(slice_hdr, prev_slice_hdr, pps);

    /* field_pic_flag differs in value */
    CHECK_VALUE(slice_hdr, prev_slice_hdr, field_pic_flag);

    /* bottom_field_flag is present in both and differs in value */
    if (slice_hdr->field_pic_flag && prev_slice_hdr->field_pic_flag)
        CHECK_VALUE(slice_hdr, prev_slice_hdr, bottom_field_flag);

    /* nal_ref_idc differs in value with one of the nal_ref_idc values is 0 */
    CHECK_EXPR(((GST_VAAPI_PICTURE_IS_REFERENCE(priv->current_picture) ^
                 (nalu->ref_idc != 0)) == 0), "nal_ref_idc");

    /* POC type is 0 for both and either pic_order_cnt_lsb differs in
       value or delta_pic_order_cnt_bottom differs in value */
    if (sps->pic_order_cnt_type == 0) {
        CHECK_VALUE(slice_hdr, prev_slice_hdr, pic_order_cnt_lsb);
        if (pps->pic_order_present_flag && !slice_hdr->field_pic_flag)
            CHECK_VALUE(slice_hdr, prev_slice_hdr, delta_pic_order_cnt_bottom);
    }

    /* POC type is 1 for both and either delta_pic_order_cnt[0]
       differs in value or delta_pic_order_cnt[1] differs in value */
    else if (sps->pic_order_cnt_type == 1) {
        CHECK_VALUE(slice_hdr, prev_slice_hdr, delta_pic_order_cnt[0]);
        CHECK_VALUE(slice_hdr, prev_slice_hdr, delta_pic_order_cnt[1]);
    }

    /* IdrPicFlag differs in value */
    CHECK_EXPR(((GST_VAAPI_PICTURE_IS_IDR(priv->current_picture) ^
                 (nalu->type == GST_H264_NAL_SLICE_IDR)) == 0), "IdrPicFlag");

    /* IdrPicFlag is equal to 1 for both and idr_pic_id differs in value */
    if (GST_VAAPI_PICTURE_IS_IDR(priv->current_picture))
        CHECK_VALUE(slice_hdr, prev_slice_hdr, idr_pic_id);

#undef CHECK_EXPR
#undef CHECK_VALUE
    return FALSE;
}

static GstVaapiDecoderStatus
decode_picture(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu, GstH264SliceHdr *slice_hdr)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 *picture;
    GstVaapiDecoderStatus status;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;

    status = ensure_context(decoder, sps);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    picture = gst_vaapi_picture_h264_new(decoder);
    if (!picture) {
        GST_ERROR("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    priv->current_picture = picture;

    picture->pps = pps;

    status = ensure_quant_matrix(decoder, picture);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_ERROR("failed to reset quantizer matrix");
        return status;
    }

    if (!init_picture(decoder, picture, slice_hdr, nalu))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    if (!fill_picture(decoder, picture, slice_hdr, nalu))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline guint
get_slice_data_bit_offset(GstH264SliceHdr *slice_hdr, GstH264NalUnit *nalu)
{
    guint epb_count;

    epb_count = slice_hdr->n_emulation_prevention_bytes;
    return 8 /* nal_unit_type */ + slice_hdr->header_size - epb_count * 8;
}

static gboolean
fill_pred_weight_table(GstVaapiDecoderH264 *decoder, GstVaapiSliceH264 *slice)
{
    GstH264SliceHdr * const slice_hdr = &slice->slice_hdr;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    GstH264PredWeightTable * const w = &slice_hdr->pred_weight_table;
    VASliceParameterBufferH264 * const slice_param = slice->base.param;
    guint num_weight_tables = 0;
    gint i, j;

    if (pps->weighted_pred_flag &&
        (GST_H264_IS_P_SLICE(slice_hdr) || GST_H264_IS_SP_SLICE(slice_hdr)))
        num_weight_tables = 1;
    else if (pps->weighted_bipred_idc == 1 && GST_H264_IS_B_SLICE(slice_hdr))
        num_weight_tables = 2;
    else
        num_weight_tables = 0;

    slice_param->luma_log2_weight_denom   = w->luma_log2_weight_denom;
    slice_param->chroma_log2_weight_denom = w->chroma_log2_weight_denom;
    slice_param->luma_weight_l0_flag      = 0;
    slice_param->chroma_weight_l0_flag    = 0;
    slice_param->luma_weight_l1_flag      = 0;
    slice_param->chroma_weight_l1_flag    = 0;

    if (num_weight_tables < 1)
        return TRUE;

    slice_param->luma_weight_l0_flag = 1;
    for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
        slice_param->luma_weight_l0[i] = w->luma_weight_l0[i];
        slice_param->luma_offset_l0[i] = w->luma_offset_l0[i];
    }

    slice_param->chroma_weight_l0_flag = sps->chroma_array_type != 0;
    if (slice_param->chroma_weight_l0_flag) {
        for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
            for (j = 0; j < 2; j++) {
                slice_param->chroma_weight_l0[i][j] = w->chroma_weight_l0[i][j];
                slice_param->chroma_offset_l0[i][j] = w->chroma_offset_l0[i][j];
            }
        }
    }

    if (num_weight_tables < 2)
        return TRUE;

    slice_param->luma_weight_l1_flag = 1;
    for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
        slice_param->luma_weight_l1[i] = w->luma_weight_l1[i];
        slice_param->luma_offset_l1[i] = w->luma_offset_l1[i];
    }

    slice_param->chroma_weight_l1_flag = sps->chroma_array_type != 0;
    if (slice_param->chroma_weight_l1_flag) {
        for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
            for (j = 0; j < 2; j++) {
                slice_param->chroma_weight_l1[i][j] = w->chroma_weight_l1[i][j];
                slice_param->chroma_offset_l1[i][j] = w->chroma_offset_l1[i][j];
            }
        }
    }
    return TRUE;
}

static gboolean
fill_RefPicList(GstVaapiDecoderH264 *decoder, GstVaapiSliceH264 *slice)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SliceHdr * const slice_hdr = &slice->slice_hdr;
    VASliceParameterBufferH264 * const slice_param = slice->base.param;
    guint i, num_ref_lists = 0;

    slice_param->num_ref_idx_l0_active_minus1 = 0;
    slice_param->num_ref_idx_l1_active_minus1 = 0;

    if (GST_H264_IS_B_SLICE(slice_hdr))
        num_ref_lists = 2;
    else if (GST_H264_IS_I_SLICE(slice_hdr))
        num_ref_lists = 0;
    else
        num_ref_lists = 1;

    if (num_ref_lists < 1)
        return TRUE;

    slice_param->num_ref_idx_l0_active_minus1 =
        slice_hdr->num_ref_idx_l0_active_minus1;

    for (i = 0; i < priv->RefPicList0_count && priv->RefPicList0[i]; i++)
        vaapi_fill_picture(&slice_param->RefPicList0[i], priv->RefPicList0[i]);
    for (; i <= slice_param->num_ref_idx_l0_active_minus1; i++)
        vaapi_init_picture(&slice_param->RefPicList0[i]);

    if (num_ref_lists < 2)
        return TRUE;

    slice_param->num_ref_idx_l1_active_minus1 =
        slice_hdr->num_ref_idx_l1_active_minus1;

    for (i = 0; i < priv->RefPicList1_count && priv->RefPicList1[i]; i++)
        vaapi_fill_picture(&slice_param->RefPicList1[i], priv->RefPicList1[i]);
    for (; i <= slice_param->num_ref_idx_l1_active_minus1; i++)
        vaapi_init_picture(&slice_param->RefPicList1[i]);
    return TRUE;
}

static gboolean
fill_slice(
    GstVaapiDecoderH264 *decoder,
    GstVaapiSliceH264   *slice,
    GstH264NalUnit      *nalu
)
{
    GstH264SliceHdr * const slice_hdr = &slice->slice_hdr;
    VASliceParameterBufferH264 * const slice_param = slice->base.param;

    /* Fill in VASliceParameterBufferH264 */
    slice_param->slice_data_bit_offset          = get_slice_data_bit_offset(slice_hdr, nalu);
    slice_param->first_mb_in_slice              = slice_hdr->first_mb_in_slice;
    slice_param->slice_type                     = slice_hdr->type % 5;
    slice_param->direct_spatial_mv_pred_flag    = slice_hdr->direct_spatial_mv_pred_flag;
    slice_param->cabac_init_idc                 = slice_hdr->cabac_init_idc;
    slice_param->slice_qp_delta                 = slice_hdr->slice_qp_delta;
    slice_param->disable_deblocking_filter_idc  = slice_hdr->disable_deblocking_filter_idc;
    slice_param->slice_alpha_c0_offset_div2     = slice_hdr->slice_alpha_c0_offset_div2;
    slice_param->slice_beta_offset_div2         = slice_hdr->slice_beta_offset_div2;

    if (!fill_RefPicList(decoder, slice))
        return FALSE;
    if (!fill_pred_weight_table(decoder, slice))
        return FALSE;
    return TRUE;
}

static GstVaapiDecoderStatus
decode_slice(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstVaapiPictureH264 *picture;
    GstVaapiSliceH264 *slice = NULL;
    GstH264SliceHdr *slice_hdr;
    GstH264ParserResult result;

    GST_DEBUG("slice (%u bytes)", nalu->size);
    slice = gst_vaapi_slice_h264_new(
        decoder,
        nalu->data + nalu->offset,
        nalu->size
    );
    if (!slice) {
        GST_ERROR("failed to allocate slice");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }

    slice_hdr = &slice->slice_hdr;
    memset(slice_hdr, 0, sizeof(*slice_hdr));
    result = gst_h264_parser_parse_slice_hdr(priv->parser, nalu, slice_hdr, TRUE, TRUE);
    if (result != GST_H264_PARSER_OK) {
        status = get_status(result);
        goto error;
    }

    if (is_new_picture(decoder, nalu, slice_hdr)) {
	priv->ready_to_dec = TRUE;
        status = decode_picture(decoder, nalu, slice_hdr);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto error;
    } 
    picture = priv->current_picture;

    if (!fill_slice(decoder, slice, nalu)) {
        status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        goto error;
    }
    gst_vaapi_picture_add_slice(
        GST_VAAPI_PICTURE_CAST(picture),
        GST_VAAPI_SLICE_CAST(slice)
    );
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

error:
    if (slice)
        gst_mini_object_unref(GST_MINI_OBJECT(slice));
    return status;
}

static inline gint
scan_for_start_code(GstAdapter *adapter, guint ofs, guint size, guint32 *scp)
{
    return (gint)gst_adapter_masked_scan_uint32_peek(adapter,
                                                     0xffffff00, 0x00000100,
                                                     ofs, size,
                                                     scp);
}

static GstVaapiDecoderStatus
decode_nalu(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderStatus status;

    switch (nalu->type) {
    case GST_H264_NAL_SLICE_IDR:
        /* fall-through. IDR specifics are handled in init_picture() */
    case GST_H264_NAL_SLICE:
        status = decode_slice(decoder, nalu);
        break;
    case GST_H264_NAL_SPS:
        status = decode_sps(decoder, nalu);
        break;
    case GST_H264_NAL_PPS:
        status = decode_pps(decoder, nalu);
        break;
    case GST_H264_NAL_SEI:
        status = decode_sei(decoder, nalu);
        break;
    case GST_H264_NAL_SEQ_END:
        status = decode_sequence_end(decoder);
        break;
  case GST_H264_NAL_AU_DELIMITER:
        /* skip all Access Unit NALs */
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
    case GST_H264_NAL_FILLER_DATA:
        /* skip all Filler Data NALs */
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
    default:
        GST_WARNING("unsupported NAL unit type %d", nalu->type);
        status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        break;
    }
    return status;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h264_parse(
    GstVaapiDecoder *base, 
    GstAdapter *adapter, 
    guint *toadd,
    gboolean *have_frame)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(base);
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    GstH264ParserResult result;
    GstH264NalUnit nalu;
    guchar *data;
    gint size = 0;
    gint ofs = 0;

    priv->adapter = adapter;
    size = gst_adapter_available (adapter);
    data = (guint8 *)gst_adapter_map (adapter,size);

    if (priv->is_avc) {
	if (size < priv->nal_length_size)
            goto need_data;

        result = gst_h264_parser_identify_nalu_avc(
                priv->parser,
                data, ofs, size, priv->nal_length_size,
                &nalu
        );
    } else {
        if (size < 4)
	    goto need_data;
        result = gst_h264_parser_identify_nalu(
            priv->parser,
            data, ofs, size,
            &nalu
            );
    }
   
    status = get_status(result);

    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        goto beach;
    else
        status = decode_nalu(decoder, &nalu);

    if (nalu.type == GST_H264_NAL_SEQ_END)
	*have_frame = TRUE;

    if (status == GST_VAAPI_DECODER_STATUS_SUCCESS) {

 	*toadd = nalu.offset + nalu.size;

        if (priv->ready_to_dec && 
	   (nalu.type == GST_H264_NAL_SLICE_IDR ||
	   nalu.type == GST_H264_NAL_SLICE)) {

    	    priv->ready_to_dec = FALSE;
            *have_frame = TRUE;
            goto beach;
        }
    } 
beach:
    return status;
need_data:
    GST_INFO("Need more data to continue the parsing");
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
}

static gboolean 
reset_context(GstVaapiDecoderH264 *decoder, GstBufferPool *pool)
{
    GstVaapiDecoderH264Private * priv = decoder->priv;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    /*Fixme: Do we actually need to check the sps parameters here?*/
    status = ensure_context(decoder, &priv->last_sps);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_DEBUG("failed to reset context");
        return FALSE;
    }
        
    if (priv->reset_context) {
        GstVaapiContextInfo info;

        info.profile    = priv->profile;
        info.entrypoint = priv->entrypoint;
        info.width      = priv->width;
        info.height     = priv->height;
        info.ref_frames = get_max_dec_frame_buffering(&priv->last_sps);
	info.pool	= GST_VAAPI_SURFACE_POOL(pool);
        
	if (!gst_vaapi_decoder_ensure_context(GST_VAAPI_DECODER(decoder), &info))
            return FALSE;
        priv->has_context = TRUE;
	priv->reset_context = FALSE;
        /* Reset DPB */
 	dpb_reset(decoder, &priv->last_sps);	
    }
    return TRUE;
}
 
gboolean
gst_vaapi_decoder_h264_decide_allocation(
    GstVaapiDecoder *dec,
    GstBufferPool *pool)
{
    GstVaapiDecoderH264 *decoder = GST_VAAPI_DECODER_H264(dec);
    GstVaapiDecoderH264Private * priv = decoder->priv;
    gboolean res;

    res = reset_context(decoder, pool);

    if (!res) {
        GST_ERROR("failed to reset VAContext..");
        return FALSE;
    }
    return TRUE;

}

static GstVaapiDecoderStatus
decode_codec_data(GstVaapiDecoderH264 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstH264NalUnit nalu;
    GstH264ParserResult result;
    guchar *buf;
    guint buf_size;
    guint i, ofs, num_sps, num_pps;
    GstMapInfo map_info;

    gst_buffer_map(buffer, &map_info, GST_MAP_READ);
    buf = map_info.data;
    buf_size = map_info.size;
    
    if (!buf || buf_size == 0)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (buf_size < 8)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    if (buf[0] != 1) {
        GST_ERROR("failed to decode codec-data, not in avcC format");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    priv->nal_length_size = (buf[4] & 0x03) + 1;

    num_sps = buf[5] & 0x1f;
    ofs = 6;

    for (i = 0; i < num_sps; i++) {
        result = gst_h264_parser_identify_nalu_avc(
            priv->parser,
            buf, ofs, buf_size, 2,
            &nalu
        );
        if (result != GST_H264_PARSER_OK)
            return get_status(result);

        status = decode_sps(decoder, &nalu);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
        ofs = nalu.offset + nalu.size;
    }

    num_pps = buf[ofs];
    ofs++;

    for (i = 0; i < num_pps; i++) {
        result = gst_h264_parser_identify_nalu_avc(
            priv->parser,
            buf, ofs, buf_size, 2,
            &nalu
        );
        if (result != GST_H264_PARSER_OK)
            return get_status(result);

        status = decode_pps(decoder, &nalu);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
        ofs = nalu.offset + nalu.size;
    }

    priv->is_avc = TRUE;
    return status;
}

static gboolean
gst_vaapi_picture_h264_allocate_surface(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private *priv = decoder->priv;
    GstVaapiPictureH264 *picture = priv->current_picture;
    GstVaapiPicture *base_picture = &picture->base;
    VAPictureParameterBufferH264 * const pic_param = base_picture->param;

    if (!gst_vaapi_picture_allocate_surface(GST_VAAPI_PICTURE_CAST(base_picture))) {
            GST_ERROR("failed to allocate surface for current pic");
            return FALSE;
    }
    vaapi_fill_picture(&pic_param->CurrPic, picture); 

    return TRUE;   
}

GstVaapiDecoderStatus
decode_buffer_h264(GstVaapiDecoderH264 *decoder, GstBuffer *buffer, GstVideoCodecFrame *frame)
{
    GstVaapiDecoderH264Private *priv = decoder->priv;
    GstVaapiPicture *picture = GST_VAAPI_PICTURE_CAST(priv->current_picture);
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    if (priv->current_picture) {
	if (!gst_vaapi_picture_h264_allocate_surface(decoder)) {
            GST_ERROR("failed to allocate surface for current pic");
            return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        }
        picture->frame_id       = frame->system_frame_number;
        /*decode pic*/
        status = decode_current_picture(decoder);
    }
    return status;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_h264_decode(GstVaapiDecoder *base, GstVideoCodecFrame *frame)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(base);
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstBuffer *codec_data;

    g_return_val_if_fail(priv->is_constructed,
                         GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED);

    status = decode_buffer_h264(decoder, frame->input_buffer, frame);
    return status;
}

gboolean
gst_vaapi_decoder_h264_reset(GstVaapiDecoder *bdec)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(bdec);
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    priv->adapter = NULL;

    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    dpb_clear(decoder);

    return TRUE;
}

static void
gst_vaapi_decoder_h264_finalize(GObject *object)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(object);

    gst_vaapi_decoder_h264_destroy(decoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_h264_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_h264_constructed(GObject *object)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(object);
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GObjectClass *parent_class;
    GstBuffer *codec_data;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    parent_class = G_OBJECT_CLASS(gst_vaapi_decoder_h264_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->is_constructed = gst_vaapi_decoder_h264_create(decoder);
    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_h264_open(decoder);
        g_return_if_fail(priv->is_opened);
        codec_data = GST_VAAPI_DECODER_CODEC_DATA(decoder);
        if (codec_data) {
            status = decode_codec_data(decoder, codec_data);
            g_return_if_fail(status ==  GST_VAAPI_DECODER_STATUS_SUCCESS);
        }
    }
}

static void
gst_vaapi_decoder_h264_class_init(GstVaapiDecoderH264Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderH264Private));

    object_class->finalize      = gst_vaapi_decoder_h264_finalize;
    object_class->constructed   = gst_vaapi_decoder_h264_constructed;

    decoder_class->parse             = gst_vaapi_decoder_h264_parse;
    decoder_class->decide_allocation = gst_vaapi_decoder_h264_decide_allocation;
    decoder_class->decode            = gst_vaapi_decoder_h264_decode;
    decoder_class->reset             = gst_vaapi_decoder_h264_reset;
}

static void
gst_vaapi_decoder_h264_init(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private *priv;

    priv                        = GST_VAAPI_DECODER_H264_GET_PRIVATE(decoder);
    decoder->priv               = priv;
    priv->parser                = NULL;
    priv->current_picture       = NULL;
    priv->dpb_count             = 0;
    priv->dpb_size              = 0;
    priv->profile               = GST_VAAPI_PROFILE_UNKNOWN;
    priv->entrypoint            = GST_VAAPI_ENTRYPOINT_VLD;
    priv->chroma_type           = GST_VAAPI_CHROMA_TYPE_YUV420;
    priv->short_ref_count       = 0;
    priv->long_ref_count        = 0;
    priv->RefPicList0_count     = 0;
    priv->RefPicList1_count     = 0;
    priv->nal_length_size       = 0;
    priv->width                 = 0;
    priv->height                = 0;
    priv->adapter               = NULL;
    priv->field_poc[0]          = 0;
    priv->field_poc[1]          = 0;
    priv->poc_msb               = 0;
    priv->poc_lsb               = 0;
    priv->prev_poc_msb          = 0;
    priv->prev_poc_lsb          = 0;
    priv->frame_num_offset      = 0;
    priv->frame_num             = 0;
    priv->prev_frame_num        = 0;
    priv->prev_pic_has_mmco5    = FALSE;
    priv->prev_pic_structure    = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    priv->is_constructed        = FALSE;
    priv->is_opened             = FALSE;
    priv->is_avc                = FALSE;
    priv->has_context           = FALSE;
    priv->ready_to_dec          = FALSE;
    priv->reset_context		= FALSE;

    memset(priv->dpb, 0, sizeof(priv->dpb));
    memset(priv->short_ref, 0, sizeof(priv->short_ref));
    memset(priv->long_ref, 0, sizeof(priv->long_ref));
    memset(priv->RefPicList0, 0, sizeof(priv->RefPicList0));
    memset(priv->RefPicList1, 0, sizeof(priv->RefPicList1));
}

/**
 * gst_vaapi_decoder_h264_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for MPEG-2 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_h264_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiDecoderH264 *decoder;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    decoder = g_object_new(
        GST_VAAPI_TYPE_DECODER_H264,
        "display",      display,
        "caps",         caps,
        NULL
    );
    if (!decoder->priv->is_constructed) {
        g_object_unref(decoder);
        return NULL;
    }
    return GST_VAAPI_DECODER_CAST(decoder);
}
