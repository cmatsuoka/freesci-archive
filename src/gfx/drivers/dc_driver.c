/***************************************************************************
 dc_driver.c Copyright (C) 2002 Walter van Niftrik


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

   Walter van Niftrik <w.f.b.w.v.niftrik@stud.tue.nl>

***************************************************************************/

#include <kos/thread.h>
#include <kos/sem.h>
#include <dc/maple.h>
#include <dc/maple/mouse.h>
#include <dc/maple/keyboard.h>
#include <dc/video.h>

#include <sci_memory.h>
#include <gfx_driver.h>
#include <gfx_tools.h>

/* Event queue struct */

struct dc_event_t {
	sci_event_t event;
	struct dc_event_t *next;
};

struct _dc_state {
	/* 0 = static buffer, 1 = back buffer, 2 = front buffer */
	byte *visual[3];
	byte *priority[2];
	
	/* Pointers to first and last event in the event queue */
	struct dc_event_t *first_event;
	struct dc_event_t *last_event;
	
	/* Semaphores for mouse pointer location and event queue updates */
	semaphore_t *sem_event, *sem_pointer;
	
	/* The dx and dy of the mouse pointer since last get_event() */
	int pointer_dx, pointer_dy;
	
	/* The current bucky state of the keys */
	int buckystate;
	
	/* Thread ID of the input thread */
	tid_t thread;
	
	/* Flag to stop the input thread. (<>0 = run, 0 = stop) */
	int run_thread;
};

#define S ((struct _dc_state *)(drv->state))

#define XFACT drv->mode->xfact
#define YFACT drv->mode->yfact
#define BYTESPP drv->mode->bytespp

#define DC_MOUSE_LEFT (1<<0)
#define DC_MOUSE_RIGHT (1<<1)

#define DC_KEY_CAPSLOCK (1<<0)
#define DC_KEY_NUMLOCK (1<<1)
#define DC_KEY_SCRLOCK (1<<2)
#define DC_KEY_INSERT (1<<3)

static int
dc_add_event(struct _gfx_driver *drv, sci_event_t *event)
/* Adds an event to the end of an event queue
** Parameters: (_gfx_driver *) drv: The driver to use
**             (sci_event_t *) event: The event to add
** Returns   : (int) 1 on success, 0 on error
*/
{
	struct dc_event_t *dc_event;
	if (!(dc_event = sci_malloc(sizeof(dc_event)))) {
		sciprintf("Error: Could not reserve memory for event\n");
		return 0;
	}
	
	dc_event->event = *event;
	dc_event->next = NULL;

	/* Semaphore prevents get_event() from removing the last event in
	** the event queue while a next event is being attached to it.
	*/

	sem_wait(S->sem_event);
	if (!(S->last_event)) {
		/* Event queue is empty */
		S->first_event = dc_event;
		S->last_event = dc_event;
		sem_signal(S->sem_event);
		return 1;
	}

	S->last_event->next = dc_event;
	S->last_event = dc_event;
	sem_signal(S->sem_event);
	return 1;
}

static int
dc_map_key(int *keystate, uint8 key)
/* Converts a kos keycode to a freesci keycode. This function also adjusts
** the caps lock, num lock, scroll lock and insert states in keystate.
** Parameters: (int *) keystate: Pointer to the keystate variable
**             (uint8) key: The kos keycode to convert
** Returns   : (int) Converted freesci keycode on success, 0 on error
*/
{
	if ((key >= KBD_KEY_A) && (key <= KBD_KEY_Z)) {
		return ('a' + (key - KBD_KEY_A));
	}
	
	if ((key >= KBD_KEY_1) && (key <= KBD_KEY_9)) {
		return ('1' + (key - KBD_KEY_1));
	}
	
	switch (key) {
		case KBD_KEY_0:			return '0';
		case KBD_KEY_BACKSPACE:		return SCI_K_BACKSPACE;
		case KBD_KEY_TAB:		return 9;
		case KBD_KEY_ESCAPE:		return SCI_K_ESC;
		case KBD_KEY_ENTER:
		case KBD_KEY_PAD_ENTER:		return SCI_K_ENTER;
		case KBD_KEY_DEL:
		case KBD_KEY_PAD_PERIOD:	return SCI_K_DELETE;
		case KBD_KEY_INSERT:
		case KBD_KEY_PAD_0:		*keystate ^= DC_KEY_INSERT;
						return SCI_K_INSERT;
		case KBD_KEY_END:
		case KBD_KEY_PAD_1:		return SCI_K_END;
		case KBD_KEY_DOWN:
		case KBD_KEY_PAD_2:		return SCI_K_DOWN;
		case KBD_KEY_PGDOWN:
		case KBD_KEY_PAD_3:		return SCI_K_PGDOWN;
		case KBD_KEY_LEFT:
		case KBD_KEY_PAD_4:		return SCI_K_LEFT;
		case KBD_KEY_PAD_5:		return SCI_K_CENTER;
		case KBD_KEY_RIGHT:
		case KBD_KEY_PAD_6:		return SCI_K_RIGHT;
		case KBD_KEY_HOME:
		case KBD_KEY_PAD_7:		return SCI_K_HOME;
		case KBD_KEY_UP:
		case KBD_KEY_PAD_8:		return SCI_K_UP;
		case KBD_KEY_PGUP:
		case KBD_KEY_PAD_9:		return SCI_K_PGUP;
		case KBD_KEY_F1:		return SCI_K_F1;
		case KBD_KEY_F2:		return SCI_K_F2;
		case KBD_KEY_F3:		return SCI_K_F3;
		case KBD_KEY_F4:		return SCI_K_F4;
		case KBD_KEY_F5:		return SCI_K_F5;
		case KBD_KEY_F6:		return SCI_K_F6;
		case KBD_KEY_F7:		return SCI_K_F7;
		case KBD_KEY_F8:		return SCI_K_F8;
		case KBD_KEY_F9:		return SCI_K_F9;
		case KBD_KEY_F10:		return SCI_K_F10;
		case KBD_KEY_PAD_PLUS:		return '+';
		case KBD_KEY_SLASH:
		case KBD_KEY_PAD_DIVIDE:	return '/';
		case KBD_KEY_MINUS:
		case KBD_KEY_PAD_MINUS:		return '-';
		case KBD_KEY_PAD_MULTIPLY:	return '*';
		case KBD_KEY_COMMA:		return ',';
		case KBD_KEY_PERIOD:		return '.';
		case KBD_KEY_BACKSLASH:		return '\\';
		case KBD_KEY_SEMICOLON:		return ';';
		case KBD_KEY_QUOTE:		return '\'';
		case KBD_KEY_LBRACKET:		return '[';
		case KBD_KEY_RBRACKET:		return ']';
		case KBD_KEY_TILDE:		return '`';
		case KBD_KEY_PLUS:		return '=';
		case KBD_KEY_SPACE:		return ' ';
		case KBD_KEY_CAPSLOCK:		*keystate ^= DC_KEY_CAPSLOCK;
						return 0;
		case KBD_KEY_SCRLOCK:		*keystate ^= DC_KEY_SCRLOCK;
						return 0;
		case KBD_KEY_PAD_NUMLOCK:	*keystate ^= DC_KEY_NUMLOCK;
						return 0;
	}

	sciprintf("Warning: Unmapped key: %02x\n", key);
	
	return 0;
}

static void
dc_input_thread(struct _gfx_driver *drv)
/* Thread that checks the dreamcast keyboard and mouse states. It adds
** keypresses and mouseclicks to the end of the event queue. It also updates
** drv->state.buckystate and drv->state.pointer_dx/dy.
** Parameters: (_gfx_driver *) drv: The driver to use
** Returns   : void
*/
{
	/* State of mouse buttons */
	unsigned int mstate = 0;
	/* Last key pressed */
	unsigned int lastkey = KBD_KEY_NONE;
	/* State of caps lock, scroll lock, num lock and insert keys */
	int keystate = DC_KEY_INSERT;

	while (S->run_thread) {
		maple_device_t *kaddr, *maddr;
		mouse_state_t *mouse;
		kbd_state_t *kbd;
		uint8 key;
		int skeys;	
		int bucky = 0;
		sci_event_t event;

		/* Sleep for 10ms */
		thd_sleep(10);

		/* Keyboard handling */
		if ((kaddr = maple_enum_type(0, MAPLE_FUNC_KEYBOARD)) &&
		  (kbd = maple_dev_status(kaddr))) {
			key = kbd->cond.keys[0];
			skeys = kbd->shift_keys;
		
			bucky =	((skeys & (KBD_MOD_LCTRL | KBD_MOD_RCTRL))?
				  SCI_EVM_CTRL : 0) |
				((skeys & (KBD_MOD_LALT | KBD_MOD_RALT))?
				  SCI_EVM_ALT : 0) |
				((skeys & KBD_MOD_LSHIFT)?
				  SCI_EVM_LSHIFT : 0) |
				((skeys & KBD_MOD_RSHIFT)?
				  SCI_EVM_RSHIFT : 0) |
				((keystate & DC_KEY_NUMLOCK)?
				  SCI_EVM_NUMLOCK : 0) |
				((keystate & DC_KEY_SCRLOCK)?
				  SCI_EVM_SCRLOCK : 0) |
				((keystate & DC_KEY_INSERT)?
				  SCI_EVM_INSERT : 0);
		
			/* If a shift key is pressed when caps lock is on, set
			** both shift key states to 0. If no shift keys are
			** pressed when caps lock is on, set both shift key
			** states to 1
			*/

			if (keystate & DC_KEY_CAPSLOCK) {
				if ((bucky & SCI_EVM_LSHIFT) ||
				  (bucky & SCI_EVM_RSHIFT))
					bucky &=
					  ~(SCI_EVM_LSHIFT | SCI_EVM_RSHIFT);
				else bucky |= SCI_EVM_LSHIFT | SCI_EVM_RSHIFT;
			}
		
			S->buckystate = bucky;

			if ((key != lastkey) && (key != KBD_KEY_NONE)) {
				event.type = SCI_EVT_KEYBOARD;
				event.data = dc_map_key(&keystate, key);
				event.buckybits = bucky;
				if (event.data) dc_add_event(drv, &event);
			}		
			lastkey = key;
		}

		/* Mouse handling */
		if ((maddr = maple_enum_type(0, MAPLE_FUNC_MOUSE)) &&
		  (mouse = maple_dev_status(maddr))) {

			/* Enable mouse support */
			drv->capabilities |= GFX_CAPABILITY_MOUSE_SUPPORT;
			
			/* Semaphore prevents get_event() from accessing
			** S->pointer_dx/dy while they are being updated
			*/
			sem_wait(S->sem_pointer);
			S->pointer_dx += mouse->dx;
			S->pointer_dy += mouse->dy;
			sem_signal(S->sem_pointer);

			if ((mouse->buttons & MOUSE_LEFTBUTTON) &&
			  !(mstate & DC_MOUSE_LEFT)) {
				event.type = SCI_EVT_MOUSE_PRESS;
				event.data = 1;
				event.buckybits = bucky;
				dc_add_event(drv, &event);
				mstate |= DC_MOUSE_LEFT;
			}
			if ((mouse->buttons & MOUSE_RIGHTBUTTON) &&
			  !(mstate & DC_MOUSE_RIGHT)) {
				event.type = SCI_EVT_MOUSE_PRESS;
				event.data = 2;
				event.buckybits = bucky;
				dc_add_event(drv, &event);
				mstate |= DC_MOUSE_RIGHT;
			}
			if (!(mouse->buttons & MOUSE_LEFTBUTTON) &&
			  (mstate & DC_MOUSE_LEFT)) {
				event.type = SCI_EVT_MOUSE_RELEASE;
				event.data = 1;
				event.buckybits = bucky;
				dc_add_event(drv, &event);
				mstate &= ~DC_MOUSE_LEFT;
			}
			if (!(mouse->buttons & MOUSE_RIGHTBUTTON) &&
			  (mstate & DC_MOUSE_RIGHT)) {
				event.type = SCI_EVT_MOUSE_RELEASE;
				event.data = 2;
				event.buckybits = bucky;
				dc_add_event(drv, &event);
				mstate &= ~DC_MOUSE_RIGHT;
			}
		}
		else drv->capabilities &= ~GFX_CAPABILITY_MOUSE_SUPPORT;

	}
}

static uint32
dc_get_color(struct _gfx_driver *drv, gfx_color_t col)
/* Converts a color as described in col to it's representation in memory
** Parameters: (_gfx_driver *) drv: The driver to use
**             (gfx_color_t) color: The color to convert
** Returns   : (uint32) the color's representation in memory
*/
{
	uint32 retval;
	uint32 temp;

	retval = 0;

	temp = col.visual.r;
	temp |= temp << 8;
	temp |= temp << 16;
	retval |= (temp >> drv->mode->red_shift) & (drv->mode->red_mask);
	temp = col.visual.g;
	temp |= temp << 8;
	temp |= temp << 16;
	retval |= (temp >> drv->mode->green_shift) & (drv->mode->green_mask);
	temp = col.visual.b;
	temp |= temp << 8;
	temp |= temp << 16;
	retval |= (temp >> drv->mode->blue_shift) & (drv->mode->blue_mask);

	return retval;
}

static void
dc_draw_line_buffer(byte *buf, int line, int bytespp, int x1,
  int y1, int x2, int y2, uint32 color)
/* Draws a line in a buffer
** This function was taken from sdl_driver.c with small modifications
** Parameters: (byte *) buf: The buffer to draw in
**             (int) line: line pitch of buf in bytes
**             (int) bytespp: number of bytes per pixel of buf
**             (int) x1, y1, x2, y2: The line to draw: (x1,y1)-(x2,y2).
**             (uint32) color: The color to draw with
** Returns   : (void)
*/
{
	int pixx, pixy;
	int x,y;
	int dx,dy;
	int sx,sy;
	int swaptmp;
	uint8 *pixel;

	dx = x2 - x1;
	dy = y2 - y1;
	sx = (dx >= 0) ? 1 : -1;
	sy = (dy >= 0) ? 1 : -1;

	dx = sx * dx + 1;
	dy = sy * dy + 1;
	pixx = bytespp;
	pixy = line;
	pixel = ((uint8*) buf) + pixx * x1 + pixy * y1;
	pixx *= sx;
	pixy *= sy;
	if (dx < dy) {
		swaptmp = dx; dx = dy; dy = swaptmp;
		swaptmp = pixx; pixx = pixy; pixy = swaptmp;
	}

	x=0;
	y=0;
	switch(bytespp) {
		case 1:
			for(; x < dx; x++, pixel += pixx) {
				*pixel = color;
				y += dy;
				if (y >= dx) {
					y -= dx; pixel += pixy;
				}
			}
			break;
		case 2:
			for (; x < dx; x++, pixel += pixx) {
				*(uint16*)pixel = color;
				y += dy;
				if (y >= dx) {
					y -= dx;
					pixel += pixy;
				}
			}
			break;
		case 4:
			for(; x < dx; x++, pixel += pixx) {
				*(uint32*)pixel = color;
				y += dy;
				if (y >= dx) {
					y -= dx;
					pixel += pixy;
				}
			}
			break;
	}

}

static void
dc_draw_filled_rect_buffer(byte *buf, int line, int bytespp, rect_t rect,
  uint32 color)
/* Draws a filled rectangle in a buffer
** Parameters: (byte *) buf: The buffer to draw in
**             (int) line: line pitch of buf in bytes
**             (int) bytespp: number of bytes per pixel of buf
**             (rect_t) rect: The rectangle to fill
**             (uint32) color: The fill color
** Returns   : (void)
*/
{
	buf += rect.y*line + rect.x*bytespp;
	int i;
	
	switch (bytespp) {
		case 1:	for (i = 0; i<rect.yl; i++) {
				memset(buf, color, rect.xl);
				buf += line;
			}
			break;
		case 2:	for (i = 0; i<rect.yl; i++) {
				memset2(buf, color, rect.xl*2);
				buf += line;
			}
			break;
		case 4:	for (i = 0; i<rect.yl; i++) {
				memset4(buf, color, rect.xl*4);
				buf += line;
			}
	}
}


static void
dc_copy_rect_buffer(byte *src, byte *dest, int srcline, int destline,
  int bytespp, rect_t sr, point_t dp)
/* Copies a rectangle from one buffer to another
** Parameters: (byte *) src: The source buffer
**             (byte *) dest: The destination buffer
**             (int) srcline: line pitch of src in bytes
**             (int) destline: line pitch of dest in bytes
**             (int) bytespp: number of bytes per pixel of src and dest
**             (rect_t) sr: Rectangle of src to copy
**             (point_t) dp: Left top corner in dest where copy should go
** Returns   : (void)
*/
{
	src += sr.y*srcline + sr.x*bytespp;
	dest += dp.y*destline + dp.x*bytespp;
	int i;
	
	switch (bytespp) {
		case 1:	for (i = 0; i<sr.yl; i++) {
				memcpy(dest, src, sr.xl);
				src += srcline;
				dest += destline;
			}
			break;
		case 2:	for (i = 0; i<sr.yl; i++) {
				memcpy2(dest, src, sr.xl*2);
				src += srcline;
				dest += destline;
			}
			break;
		case 4:	for (i = 0; i<sr.yl; i++) {
				memcpy4(dest, src, sr.xl*4);
				src += srcline;
				dest += destline;
			}
	}
}

static int
dc_set_parameter(struct _gfx_driver *drv, char *attribute, char *value)
{
	printf("Fatal error: Attribute '%s' does not exist\n", attribute);
	return GFX_FATAL;
}


static int
dc_init_specific(struct _gfx_driver *drv, int xfact, int yfact, int bytespp)
{
	kthread_t *thread;

	sciprintf("Initialising video mode\n");

	if (!S)	S = sci_malloc(sizeof(struct _dc_state));
	if (!S) return GFX_FATAL;
	
        if ((xfact != 1 && xfact != 2) || (bytespp != 2 && bytespp != 4) ||
        	xfact != yfact) {
		sciprintf("Error: Buffers with scale factors (%d,%d) and bpp=%d are not supported\n",
		  xfact, yfact, bytespp);
		return GFX_ERROR;
	}

	int i;

	for (i = 0; i < 2; i++) {
		if (!(S->priority[i] = sci_malloc(320*xfact*200*yfact)) ||
		  !(S->visual[i] = sci_malloc(320*xfact*200*yfact* bytespp))) {
			sciprintf("Error: Could not reserve memory for buffer\n");
			return GFX_ERROR;
		}
	}	

	S->visual[2] = (byte *) vram_s;

	memset(S->visual[0], 0, 320*xfact*200*yfact*bytespp);
	memset(S->visual[1], 0, 320*xfact*200*yfact*bytespp);
	memset(S->visual[2], 0, 320*xfact*240*yfact*bytespp);
	memset(S->priority[0], 0, 320*xfact*200*yfact);
	memset(S->priority[1], 0, 320*xfact*200*yfact);

	S->pointer_dx = 0;
	S->pointer_dy = 0;
	
	int rmask = 0, gmask = 0, bmask = 0, rshift = 0, gshift = 0;
	int bshift = 0, vidres = 0, vidcol = 0;
		
	switch(bytespp) {
		case 2:	rmask = 0xF800;
			gmask = 0x7E0;
			bmask = 0x1F;
			rshift = 16;
			gshift = 21;
			bshift = 27;
			vidcol = PM_RGB565;
			break;
		case 4:	rmask = 0xFF0000;
			gmask = 0xFF00;
			bmask = 0xFF;
			rshift = 8;
			gshift = 16;
			bshift = 24;
			vidcol = PM_RGB888;
	}

	switch (xfact) {
		case 1:
			vidres = DM_320x240;
			break;
		case 2:
			vidres = DM_640x480_PAL_IL;
	}
		
	vid_set_mode(vidres, vidcol);

	drv->mode = gfx_new_mode(xfact, yfact, bytespp, rmask, gmask, bmask, 0,
	  rshift, gshift, bshift, 0, 0, 0);

	printf("Video mode initialisation completed succesfully\n");
	
	S->run_thread = 1;

	thread = thd_create((void *) dc_input_thread, drv);

	S->thread = thread->tid;

	S->first_event = NULL;
	S->last_event = NULL;
	
	if (!(S->sem_event = sem_create(1)) ||
	  !(S->sem_pointer = sem_create(1))) {
		printf("Error: Could not reserve memory for semaphore\n");
		return GFX_ERROR;
	};

	return GFX_OK;
}

static int
dc_init(struct _gfx_driver *drv)
{
	if (dc_init_specific(drv, 1, 1, 2) != GFX_OK) return GFX_FATAL;
	
	return GFX_OK;
}

static void
dc_exit(struct _gfx_driver *drv)
{
	if (S) {
		sciprintf("Freeing graphics buffers\n");
		sci_free(S->visual[0]);
		sci_free(S->visual[1]);
		sci_free(S->priority[0]);
		sci_free(S->priority[1]);
		sciprintf("Waiting for input thread to exit... ");
		S->run_thread = 0;
		while (thd_by_tid(S->thread));
		sciprintf("ok\n");
		sciprintf("Freeing semaphores\n");
		sem_destroy(S->sem_event);
		sem_destroy(S->sem_pointer);
	}
	sci_free(S);
}

	/*** Drawing operations ***/
	
static int
dc_draw_line(struct _gfx_driver *drv, rect_t line, gfx_color_t color,
	      gfx_line_mode_t line_mode, gfx_line_style_t line_style)
{
	uint32 scolor;
	int xfact = (line_mode == GFX_LINE_MODE_FINE)? 1: XFACT;
	int yfact = (line_mode == GFX_LINE_MODE_FINE)? 1: YFACT;
	int xsize = XFACT*320;
	int ysize = YFACT*200;
	
	int xc, yc;
	int x1, y1, x2, y2;

	scolor = dc_get_color(drv, color);

	x1 = line.x;
	y1 = line.y;
		
	for (xc = 0; xc < xfact; xc++)
		for (yc = 0; yc < yfact; yc++) {
			x1 = line.x + xc;
			y1 = line.y + yc;
			x2 = x1 + line.xl;
			y2 = y1 + line.yl;
				
			if (x1 < 0) x1 = 0;
			if (x2 < 0) x2 = 0;
			if (y1 < 0) y1 = 0;
			if (y2 < 0) y2 = 0;
				
			if (x1 > xsize) x1 = xsize;
			if (x2 >= xsize) x2 = xsize - 1;
			if (y1 > ysize) y1 = ysize;
			if (y2 >= ysize) y2 = ysize - 1;
				
			if (color.mask & GFX_MASK_VISUAL)
				dc_draw_line_buffer(S->visual[1],
				  XFACT*320*BYTESPP, BYTESPP, x1, y1, x2, y2,
				  dc_get_color(drv, color));

			if (color.mask & GFX_MASK_PRIORITY)
				dc_draw_line_buffer(S->priority[1], XFACT*320,
				  1, x1, y1, x2, y2, color.priority);
		}

	return GFX_OK;
}

static int
dc_draw_filled_rect(struct _gfx_driver *drv, rect_t rect,
  gfx_color_t color1, gfx_color_t color2, gfx_rectangle_fill_t shade_mode)
{
	if (color1.mask & GFX_MASK_VISUAL)
		dc_draw_filled_rect_buffer(S->visual[1], XFACT*320*BYTESPP,
		  BYTESPP, rect, dc_get_color(drv, color1));
	
	if (color1.mask & GFX_MASK_PRIORITY)
		dc_draw_filled_rect_buffer(S->priority[1], XFACT*320, 1, rect,
		  color1.priority);

	return GFX_OK;
}

	/*** Pixmap operations ***/

static int
dc_register_pixmap(struct _gfx_driver *drv, gfx_pixmap_t *pxm)
{
	return GFX_ERROR;
}

static int
dc_unregister_pixmap(struct _gfx_driver *drv, gfx_pixmap_t *pxm)
{
	return GFX_ERROR;
}

static int
dc_draw_pixmap(struct _gfx_driver *drv, gfx_pixmap_t *pxm, int priority,
		rect_t src, rect_t dest, gfx_buffer_t buffer)
{
	int bufnr = (buffer == GFX_BUFFER_STATIC)? 0:1;
	
	return gfx_crossblit_pixmap(drv->mode, pxm, priority, src, dest,
	  S->visual[bufnr], XFACT*BYTESPP*320, S->priority[bufnr], XFACT*320,
	  1, 0);
}	

static int
dc_grab_pixmap(struct _gfx_driver *drv, rect_t src, gfx_pixmap_t *pxm,
		gfx_map_mask_t map)
{
	switch (map) {
		case GFX_MASK_VISUAL:
			dc_copy_rect_buffer(S->visual[1], pxm->data,
			  XFACT*320*BYTESPP, src.xl*BYTESPP, BYTESPP, src,
			  gfx_point(0, 0));
			pxm->xl = src.xl;
			pxm->yl = src.yl;
			return GFX_OK;
		case GFX_MASK_PRIORITY:
			dc_copy_rect_buffer(S->priority[1], pxm->index_data,
			  XFACT*320, src.xl, 1, src, gfx_point(0, 0));
			pxm->index_xl = src.xl;
			pxm->index_yl = src.yl;
			return GFX_OK;
		default:
			sciprintf("Error: attempt to grab pixmap from invalid map");
			return GFX_ERROR;
	}
}


	/*** Buffer operations ***/

static int
dc_update(struct _gfx_driver *drv, rect_t src, point_t dest, gfx_buffer_t buffer)
{
	int tbufnr = (buffer == GFX_BUFFER_BACK)? 1:2;

	dc_copy_rect_buffer(S->visual[tbufnr-1], S->visual[tbufnr],
	  XFACT*320*BYTESPP, XFACT*320*BYTESPP, BYTESPP, src, dest);
		
	if ((tbufnr == 1) && (src.x == dest.x) && (src.y == dest.y)) 
		dc_copy_rect_buffer(S->priority[0], S->priority[1], XFACT*320,
		  XFACT*320, 1, src, dest);
	
	return GFX_OK;
}

static int
dc_set_static_buffer(struct _gfx_driver *drv, gfx_pixmap_t *pic, gfx_pixmap_t *priority)
{
	memcpy4(S->visual[0], pic->data, XFACT*320 * YFACT*200 * BYTESPP);
	memcpy4(S->priority[0], priority->index_data, XFACT*320 * YFACT*200);
	return GFX_OK;
}

	/*** Palette operations ***/

static int
dc_set_palette(struct _gfx_driver *drv, int index, byte red, byte green, byte blue)
{
	return GFX_ERROR;
}


	/*** Mouse pointer operations ***/

static int
dc_set_pointer (struct _gfx_driver *drv, gfx_pixmap_t *pointer)
{
	return GFX_ERROR;
}

	/*** Event management ***/

static sci_event_t
dc_get_event(struct _gfx_driver *drv)
{
	sci_event_t event;
	struct dc_event_t *first;
	sem_wait(S->sem_pointer);
	drv->pointer_x += S->pointer_dx;
	drv->pointer_y += S->pointer_dy;
	S->pointer_dx = 0;
	S->pointer_dy = 0;
	sem_signal(S->sem_pointer);
	if (drv->pointer_x < 0) drv->pointer_x = 0;
	if (drv->pointer_x >= 320*XFACT) drv->pointer_x = 320*XFACT-1;
	if (drv->pointer_y < 0) drv->pointer_y = 0;
	if (drv->pointer_y >= 200*YFACT) drv->pointer_y = 200*YFACT-1;
	sem_wait(S->sem_event);
	first = S->first_event;
	if (first) {
		event = first->event;
		S->first_event = first->next;
		free(first);
		if (S->first_event == NULL) S->last_event = NULL;
		sem_signal(S->sem_event);
		return event;
	}
	sem_signal(S->sem_event);
	event.type = SCI_EVT_NONE;
	event.buckybits = S->buckystate;
	return event;
}


static int
dc_usec_sleep(struct _gfx_driver *drv, long usecs)
{
	/* TODO: wake up on mouse move */
	usleep(usecs);
	return GFX_OK;
}

gfx_driver_t
gfx_driver_dc = {
	"dc",
	"0.1",
	SCI_GFX_DRIVER_MAGIC,
	SCI_GFX_DRIVER_VERSION,
	NULL,
	0,
	0,
	GFX_CAPABILITY_FINE_LINES | GFX_CAPABILITY_PIXMAP_GRABBING,
	0,
	dc_set_parameter,
	dc_init_specific,
	dc_init,
	dc_exit,
	dc_draw_line,
	dc_draw_filled_rect,
	dc_register_pixmap,
	dc_unregister_pixmap,
	dc_draw_pixmap,
	dc_grab_pixmap,
	dc_update,
	dc_set_static_buffer,
	dc_set_pointer,
	dc_set_palette,
	dc_get_event,
	dc_usec_sleep,
	NULL
};