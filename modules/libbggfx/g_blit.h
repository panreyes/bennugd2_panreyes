/*
 *  Copyright (C) SplinterGU (Fenix/BennuGD) (Since 2006)
 *  Copyright (C) 2002-2006 Fenix Team (Fenix)
 *  Copyright (C) 1999-2002 José Luis Cebrián Pagüe (Fenix)
 *
 *  This file is part of Bennu Game Development
 *
 *  This software is provided 'as-is', without any express or implied
 *  warranty. In no event will the authors be held liable for any damages
 *  arising from the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 *
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 *
 */

#ifndef __G_BLIT_H
#define __G_BLIT_H

#include <bgddl.h>
#include <g_bitmap.h>
#include <g_regions.h>

/* --------------------------------------------------------------------------- */
/* Flags for gr_blit                                                           */

#define B_HMIRROR       0x0001
#define B_VMIRROR       0x0002
#define B_TRANSLUCENT   0x0004
#define B_ABLEND        0x0010
#define B_SBLEND        0x0020
#define B_NOCOLORKEY    0x0080


#define BLEND_DISABLED  -1
#define BLEND_CUSTOM    -2

/* --------------------------------------------------------------------------- */

#ifdef USE_SDL2
    typedef SDL_BlendMode BLENDMODE;
#endif
#ifdef USE_SDL2_GPU
    typedef GPU_BlendPresetEnum BLENDMODE;
    extern GPU_FilterEnum gr_filter_mode;
#endif

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif

#ifdef __GNUC__
#define __PACKED __attribute__ ((packed))
#pragma pack(1)
#else
#define __PACKED
#define inline __inline
#endif

typedef struct {
    int64_t src_rgb;
    int64_t dst_rgb;
    int64_t src_alpha;
    int64_t dst_alpha;
    int64_t eq_rgb;
    int64_t eq_alpha;
} __PACKED CUSTOM_BLENDMODE;

#ifdef __GNUC__
#pragma pack()
#endif

#ifdef _MSC_VER
#pragma pack(pop)
#endif

extern int gr_create_image_for_graph( GRAPH * gr );

extern int gr_prepare_renderer( GRAPH * dest,
                     REGION * clip,
                     int64_t flags,
                     BLENDMODE * blend_mode );

extern void gr_blit( GRAPH * dest, REGION * clip, double scrx, double scry, int64_t flags, int64_t angle, double scalex, double scaley, double centerx, double centery, GRAPH * gr, BGD_Rect * gr_clip, uint8_t alpha, uint8_t color_r, uint8_t color_g, uint8_t color_b, BLENDMODE blend_mode, CUSTOM_BLENDMODE * custom_blendmode );
extern void gr_get_bbox( REGION * dest, REGION * clip, double x, double y, int64_t flags, int64_t angle, double scalex, double scaley, double centerx, double centery, GRAPH * gr, BGD_Rect * map_clip );

/* --------------------------------------------------------------------------- */

#endif
