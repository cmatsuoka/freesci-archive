/***************************************************************************
 view_0.c Copyright (C) 2000 Christoph Reichenbach


 This program may be modified and copied freely according to the terms of
 the GNU general public license (GPL), as long as the above copyright
 notice and the licensing information contained herein are preserved.

 Please refer to www.gnu.org for licensing details.

 This work is provided AS IS, without warranty of any kind, expressed or
 implied, including but not limited to the warranties of merchantibility,
 noninfringement, and fitness for a specific purpose. The author will not
 be held liable for any damage caused by this work or derivatives of it.

 By using this source code, you agree to the licensing terms as stated
 above.


 Please contact the maintainer for bug reports or inquiries.

 Current Maintainer:

    Christoph Reichenbach (CR) <jameson@linuxgames.com>

***************************************************************************/


#include <gfx_system.h>
#include <gfx_resource.h>
#include <gfx_tools.h>





static gfx_pixmap_t *
gfxr_draw_cel0(int id, int loop, int cel, byte *resource, int size, gfxr_view_t *view, int mirrored)
{
	int xl = get_int_16(resource);
	int yl = get_int_16(resource + 2);
	int xhot = ((signed char *) resource)[4];
	int yhot = ((signed char *) resource)[5];
	int color_key = resource[6];
	int pos = 7;
	int writepos = 0;
	int pixmap_size = xl * yl;
	gfx_pixmap_t *retval = gfx_pixmap_alloc_index_data(gfx_new_pixmap(xl, yl, id, loop, cel));
	byte *dest = retval->index_data;

	retval->xoffset = -xhot;
	retval->yoffset = -yhot;
	retval->colors = view->colors;
	retval->colors_nr = view->colors_nr;
	retval->flags |= GFX_PIXMAP_FLAG_EXTERNAL_PALETTE;

	if (xl <= 0 || yl <= 0) {
		gfx_free_pixmap(NULL, retval);
		GFXERROR("View %02x:(%d/%d) has invalid xl=%d or yl=%d\n", id, loop, cel, xl, yl);
		return NULL;
	}

	while (writepos < pixmap_size && pos < size) {
		int op = resource[pos++];
		int count = op >> 4;
		int color = op & 0xf;

		if (color == color_key)
			color = GFX_COLOR_INDEX_TRANSPARENT;

		if (writepos + count > pixmap_size) {
			GFXERROR("View %02x:(%d/%d) writes RLE data over its designated end at rel. offset 0x%04x\n", id, loop, cel, pos);
			return NULL;
		}

		memset(dest + writepos, color, count);
		writepos += count;
	}
	return retval;
}

static int
gfxr_draw_loop0(gfxr_loop_t *dest, int id, int loop, byte *resource, int offset, int size, gfxr_view_t *view, int mirrored)
{
	int i;
	int cels_nr = get_int_16(resource + offset);

	if (get_uint_16(resource + offset + 2)) {
		GFXWARN("View %02x:(%d): Gray magic %04x in loop, expected white\n", id, loop, get_uint_16(resource + offset + 2));
	}

	if (cels_nr * 2 + 4 + offset > size) {
		GFXERROR("View %02x:(%d): Offset array for %d cels extends beyond resource space\n", id, loop, cels_nr);
		dest->cels_nr = 0; /* Mark as "delete no cels" */
		dest->cels = NULL;
		return 1;
	}

	dest->cels = malloc(sizeof(gfx_pixmap_t *) * cels_nr);

	for (i = 0; i < cels_nr; i++) {
		int cel_offset = get_uint_16(resource + offset + 4 + (i << 1));
		gfx_pixmap_t *cel = NULL;

		if (cel_offset >= size) {
			GFXERROR("View %02x:(%d/%d) supposed to be at illegal offset 0x%04x\n", id, loop, i, cel_offset);
			cel = NULL;
		} else
			cel = gfxr_draw_cel0(id, loop, i, resource + cel_offset, size - cel_offset, view, mirrored);
		

		if (!cel) {
			dest->cels_nr = i;
			return 1;
		}

		dest->cels[i] = cel;
	}

	dest->cels_nr = cels_nr;

	return 0;
}


#define V0_LOOPS_NR_OFFSET 0
#define V0_FIRST_LOOP_OFFSET 8
#define V0_MIRROR_LIST_OFFSET 2

gfxr_view_t *
gfxr_draw_view0(int id, byte *resource, int size)
{
	int i;
	gfxr_view_t *view;
	int mirror_bitpos = 1;
	int mirror_bytepos = V0_MIRROR_LIST_OFFSET;

	if (size < V0_FIRST_LOOP_OFFSET + 8) {
		GFXERROR("Attempt to draw empty view %04x\n", id);
		return NULL;
	}

	view = malloc(sizeof(gfxr_view_t));
	view->ID = id;

	view->loops_nr = resource[V0_LOOPS_NR_OFFSET];

	/* Set palette */
	view->colors_nr = GFX_SCI0_IMAGE_COLORS_NR;
	view->flags = GFX_PIXMAP_FLAG_EXTERNAL_PALETTE;
	view->colors = gfx_sci0_image_colors;

	if (view->loops_nr * 2 + V0_FIRST_LOOP_OFFSET > size) {
		GFXERROR("View %04x: Not enough space in resource to accomodate for the claimed %d loops\n", id, view->loops_nr);
		free(view);
		return NULL;
	}

	view->loops = malloc(sizeof (gfxr_loop_t) * view->loops_nr);

	for (i = 0; i < view->loops_nr; i++) {
		int error_token = 0;
		int loop_offset = get_uint_16(resource + V0_FIRST_LOOP_OFFSET + (i << 1));
		int mirrored = resource[mirror_bytepos] & mirror_bitpos;

		if ((mirror_bitpos <<= 1) == 0x100) {
			mirror_bytepos++;
			mirror_bitpos = 1;
		}

		if (loop_offset >= size) {
			GFXERROR("View %04x:(%d) supposed to be at illegal offset 0x%04x\n", id, i, loop_offset);
			error_token = 1;
		}

		if (error_token || gfxr_draw_loop0(view->loops + i, id, i, resource, loop_offset, size, view, mirrored)) {
			/* An error occured */
			view->loops_nr = i;
			gfxr_free_view(NULL, view);
			return NULL;
		}
	}

	return view;
}






