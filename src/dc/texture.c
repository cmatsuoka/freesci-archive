/* GhettoPlay: an S3M browser and playback util
   (c)2000 Dan Potter

   This program is free software; you can redistribute it and/or modify
   it under the terms of the KOS License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   KOS License (README.KOS) for more details.

   You should have received a copy of the KOS License along with this
   program; if not, please visit Cryptic Allusion DCDev at:

     http://dcdev.allusion.net/

*/

#include "gp.h"

/* Creates the utility texture used for the font and mouse cursor. The
   resulting texture will be 256x256. */
#include "mouse1.h"
pvr_ptr_t util_texture;
pvr_poly_hdr_t util_txr_hdr;
void setup_util_texture() {
	uint16	*vram;
	int	x, y;
	pvr_poly_cxt_t	cxt;

	util_texture = pvr_mem_malloc(256*256*2);
	printf("util_texture at %08x\n", util_texture);
	vram = (uint16 *)util_texture;
	
	/* First dump in the mouse cursor */
	for (y=0; y<16; y++) {
		for (x=0; x<10; x++) {
			if (mouse1_xpm[y*10+x] == '.')
				*vram = 0xffff;
			else if (mouse1_xpm[y*10+x] == '+')
				*vram = 0xf000;
			else
				*vram = 0x0000;
			vram++;
		}
		vram += 256 - 10;
	}
	
	/* Now add the rest as ASCII characters */
	vram = (uint16 *)util_texture;
	for (y=0; y<8; y++) {
		for (x=0; x<16; x++) {
			/* Skip the first (it's a mouse pointer) */
			if (x != 0 || y != 0) 
				bfont_draw(vram, 256, 0, y*16+x);
			vram += 16;
		}
		vram += 23*256;
	}

	/* Setup a polygon header for the util texture */
	pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
		256, 256, util_texture, PVR_FILTER_NONE);
	pvr_poly_compile(&util_txr_hdr, &cxt);
}
