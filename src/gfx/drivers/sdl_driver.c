/***************************************************************************
 sdl_driver.h Copyright (C) 2001 Solomon Peachy


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

     Solomon Peachy <pizza@shaftnet.org>

***************************************************************************/

#include <gfx_driver.h>
#ifdef 0
/* HAVE_SDL */
#include <gfx_tools.h>

#include <SDL/SDL.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>

#define SCI_XLIB_PIXMAP_HANDLE_NORMAL 0
#define SCI_XLIB_PIXMAP_HANDLE_GRABBED 1

#define SCI_XLIB_SWAP_CTRL_CAPS (1 << 0)

int string_truep(char *value); 
byte *xlib_create_cursor_data(gfx_driver_t *drv, gfx_pixmap_t *pointer, int mode);
unsigned long xlib_map_color(gfx_driver_t *drv, gfx_color_t color);
unsigned long xlib_map_pixmap_color(gfx_driver_t *drv, gfx_pixmap_color_t pc);


/* XXXX clean out */
struct _sdl_state {
  
        int used_bytespp;

  gfx_pixmap_t *priority[2];
  SDL_Color colors[256];
  SDL_Surface *visual[3];
  SDL_Surface *primary;
  int buckystate;
  int flags;
  byte *pointer_data[2];
};

#define S ((struct _sdl_state *)(drv->state))

#define XASS(foo) { int val = foo; if (!val) sdlerror(drv, __LINE__); }
#define XFACT drv->mode->xfact
#define YFACT drv->mode->yfact

#define DEBUGB if (drv->debug_flags & GFX_DEBUG_BASIC && ((debugline = __LINE__))) sdlprintf
#define DEBUGU if (drv->debug_flags & GFX_DEBUG_UPDATES && ((debugline = __LINE__))) sdlprintf
#define DEBUGPXM if (drv->debug_flags & GFX_DEBUG_PIXMAPS && ((debugline = __LINE__))) sdlprintf
#define DEBUGPTR if (drv->debug_flags & GFX_DEBUG_POINTER && ((debugline = __LINE__))) sdlprintf
#define ERROR if ((debugline = __LINE__)) sdlprintf

static int debugline = 0;

static void
sdlprintf(char *fmt, ...)
{
  va_list argp;
  fprintf(stderr,"GFX-SDL %d:", debugline);
  va_start(argp, fmt);
  vfprintf(stderr, fmt, argp);
  va_end(argp);
}

static void
sdlerror(gfx_driver_t *drv, int line)
{
  sdlprintf("Error in line %d\n", line);
}

static int
sdl_set_parameter(struct _gfx_driver *drv, char *attribute, char *value)
{
  if (strcmp(attribute, "swap_ctrl_caps") ||
      strcmp(attribute, "swap_caps_ctrl")) {
    if (string_truep(value))
      S->flags |= SCI_XLIB_SWAP_CTRL_CAPS;
    else
      S->flags &= ~SCI_XLIB_SWAP_CTRL_CAPS;
    
    return GFX_OK;
  }
  
  ERROR("Attempt to set sdl parameter \"%s\" to \"%s\"\n", attribute, value);
  return GFX_ERROR;
}

static int
sdl_init_specific(struct _gfx_driver *drv, int xfact, int yfact, int bytespp)
{
  int red_shift, green_shift, blue_shift, alpha_shift;
  int xsize = xfact * 320;
  int ysize = yfact * 200;
  XSizeHints *size_hints;
  
  int i;
  
  if (!S)
    S = malloc(sizeof(struct _sdl_state));
  
  S->flags = 0;
  
  if (xfact < 1 || yfact < 1 || bytespp < 1 || bytespp > 4) {
    ERROR("Internal error: Attempt to open window w/ scale factors (%d,%d) and bpp=%d!\n",
	  xfact, yfact, bytespp);
  }

  S->primary = NULL;
  S->primary = SDL_SetVideoMode(xsize, ysize, bytespp << 3, SDL_HWSURFACE);

  if (!S->primary) {
    ERROR("Could not set up a primary SDL surface!\n");
    return GFX_FATAL;
  }

  if (S->primary->format->BytesPerPixel != bytespp) {
    ERROR("Could not set up a primary SDL surface of depth %d bpp!\n",bytespp);
    SDL_FreeSurface(S->primary);
    S->primary = NULL;
    return GFX_FATAL;
  }
  
  /* clear palette */
  for (i = 0; i < 256; i++) {
    S->colors[i].r = 0;
    S->colors[i].g = 0;
    S->colors[i].b = 0;
  }
  if (bytespp == 1) 
    SDL_SetColors(S->primary, S->colors, 0, 256);

  /* create an input event mask */
  SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
  SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
  SDL_EventState(SDL_VIDEORESIZE, SDL_IGNORE);

  SDL_WM_SetCaption("FreeSCI", "freesci");

  SDL_ShowCursor(SDL_DISABLE);
  S->pointer_data[0] = NULL;
  S->pointer_data[1] = NULL;

  S->buckystate = 0;
  S->used_bytespp = bytespp;

  if (bytespp == 1)
    red_shift = green_shift = blue_shift = alpha_shift = 0;
  else {
    red_shift = S->primary->format->Rshift;
    green_shift = S->primary->format->Gshift;
    blue_shift = S->primary->format->Bshift;
    alpha_shift = S->primary->format->Ashift;
  }

  for (i = 0; i < 2; i++) {
    S->priority[i] = gfx_pixmap_alloc_index_data(gfx_new_pixmap(xsize, ysize, GFX_RESID_NONE, -i, -777));
    if (!S->priority[i]) {
      ERROR("Out of memory: Could not allocate priority maps! (%dx%d)\n",
	    xsize, ysize);
      return GFX_FATAL;
    }
  }

  /* create the visual buffers */
  for (i = 0; i < 3; i++) {  /* XXX SDL_SRCALPHA ??? */
    S->visual[i] = SDL_CreateRGBSurface(SDL_HWSURFACE, xsize, ysize, 
					bytespp << 3, 
					S->primary->format->Rmask, 
					S->primary->format->Gmask,
					S->primary->format->Bmask, 
					S->primary->format->Amask);
    
    SDL_FillRect(S->primary, NULL, SDL_MapRGB(S->primary->format, 0,0,0));
  }
  
  drv->mode = gfx_new_mode(xfact, yfact, bytespp,
			   S->primary->format->Rmask, 
			   S->primary->format->Gmask,
			   S->primary->format->Bmask, 
			   S->primary->format->Amask,
			   red_shift, green_shift, blue_shift, alpha_shift,
			   (bytespp == 1)? 256 : 0);
  
  return GFX_OK;
}



static int
sdl_init(struct _gfx_driver *drv)
{
  int i;
  
  if (SDL_Init(SDL_INIT_VIDEO)) {
    DEBUGB("Failed to init SDL");
    return GFX_FATAL;
  }
  
  for (i = 4; i > 0; i--) {
    /* 320x200 * (2,2) scaling) */
    if (SDL_VideoModeOK(640,400, i << 3, SDL_HWSURFACE | SDL_SWSURFACE))
      if (! sdl_init_specific(drv, 2, 2, i))
	return GFX_OK;
  }
  DEBUGB("Failed to find visual!\n");
  
  /* XXX need to add mouse motion ignoring */
  return GFX_FATAL;
}

static void
sdl_exit(struct _gfx_driver *drv)
{
  int i;
  if (S) {
    for (i = 0; i < 2; i++) {
      gfx_free_pixmap(drv, S->priority[i]);
      S->priority[i] = NULL;
    }
    /* XXXX write me */
    
  }
  SDL_Quit();
}


  /*** Drawing operations ***/

static int
xlib_draw_line(struct _gfx_driver *drv, rect_t line, gfx_color_t color,
               gfx_line_mode_t line_mode, gfx_line_style_t line_style)
{
	int linewidth = (line_mode == GFX_LINE_MODE_FINE)? 1:
		(drv->mode->xfact + drv->mode->yfact) >> 1;

	if (color.mask & GFX_MASK_VISUAL) {
		S->gc_values.foreground = xlib_map_color(drv, color);
		S->gc_values.line_width = linewidth;
		S->gc_values.line_style = (line_style == GFX_LINE_STYLE_NORMAL)?
			LineSolid : LineOnOffDash;
		S->gc_values.cap_style = CapProjecting;

		XChangeGC(S->display, S->gc, GCLineWidth | GCLineStyle | GCForeground | GCCapStyle, &(S->gc_values));

		XASS(XDrawLine(S->display, S->visual[1], S->gc, line.x, line.y,
			       line.x + line.xl, line.y + line.yl));
	}

	if (color.mask & GFX_MASK_PRIORITY) {
		int xc, yc;
		rect_t newline;

		newline.xl = line.xl;
		newline.yl = line.yl;

		linewidth--;
		for (xc = -linewidth; xc++; xc <= linewidth)
			for (yc = -linewidth; yc++; yc <= linewidth) {
				newline.x = line.x + xc;
				newline.y = line.y + yc;
				gfx_draw_line_pixmap_i(S->priority[0], newline, color.priority);
			}
	}

	return GFX_OK;
}

static int
xlib_draw_filled_rect(struct _gfx_driver *drv, rect_t rect,
                      gfx_color_t color1, gfx_color_t color2,
                      gfx_rectangle_fill_t shade_mode)
{

	if (color1.mask & GFX_MASK_VISUAL) {
		S->gc_values.foreground = xlib_map_color(drv, color1);
		XChangeGC(S->display, S->gc, GCForeground, &(S->gc_values));
		XASS(XFillRectangle(S->display, S->visual[1], S->gc, rect.x, rect.y,
				    rect.xl, rect.yl));
	}

	if (color1.mask & GFX_MASK_PRIORITY)
		gfx_draw_box_pixmap_i(S->priority[0], rect, color1.priority);

	return GFX_OK;
}

  /*** Pixmap operations ***/

static int
xlib_register_pixmap(struct _gfx_driver *drv, gfx_pixmap_t *pxm)
{
	if (pxm->internal.info) {
		ERROR("Attempt to register pixmap twice!\n");
		return GFX_ERROR;
	}
	pxm->internal.info = XCreateImage(S->display, DefaultVisual(S->display, DefaultScreen(S->display)),
					    S->used_bytespp << 3, ZPixmap, 0, (char *) pxm->data, pxm->xl,
					    pxm->yl, 8, 0);

	DEBUGPXM("Registered pixmap %d/%d/%d at %p (%dx%d)\n", pxm->ID, pxm->loop, pxm->cel,
		 pxm->internal.info, pxm->xl, pxm->yl);
	return GFX_OK;
}

static int
xlib_unregister_pixmap(struct _gfx_driver *drv, gfx_pixmap_t *pxm)
{
	DEBUGPXM("Freeing pixmap %d/%d/%d at %p\n", pxm->ID, pxm->loop, pxm->cel,
		 pxm->internal.info);

	if (!pxm->internal.info) {
		ERROR("Attempt to unregister pixmap twice!\n");
		return GFX_ERROR;
	}

	XDestroyImage((XImage *) pxm->internal.info);
	pxm->internal.info = NULL;
	pxm->data = NULL; /* Freed by XDestroyImage */
	return GFX_OK;
}

static int
xlib_draw_pixmap(struct _gfx_driver *drv, gfx_pixmap_t *pxm, int priority,
                 rect_t src, rect_t dest, gfx_buffer_t buffer)
{
	int bufnr = (buffer == GFX_BUFFER_STATIC)? 2:1;
	int pribufnr = bufnr -1;
	XImage *tempimg;

	if (dest.xl != src.xl || dest.yl != src.yl) {
		ERROR("Attempt to scale pixmap (%dx%d)->(%dx%d): Not supported\n",
		      src.xl, src.yl, dest.xl, dest.yl);
		return GFX_ERROR;
	}
	fflush(stdout);
	if (pxm->internal.handle == SCI_XLIB_PIXMAP_HANDLE_GRABBED) {
		XPutImage(S->display, S->visual[bufnr], S->gc, (XImage *) pxm->internal.info,
			  src.x, src.y, dest.x, dest.y, dest.xl, dest.yl);
		return GFX_OK;
	}
	fflush(stdout);
	tempimg = XGetImage(S->display, S->visual[bufnr], dest.x, dest.y,
			    dest.xl, dest.yl, 0xffffffff, ZPixmap);

	if (!tempimg) {
		ERROR("Failed to grab X image!\n");
		return GFX_ERROR;
	}

	gfx_crossblit_pixmap(drv->mode, pxm, priority, src, dest,
			     (byte *) tempimg->data, tempimg->bytes_per_line,
			     S->priority[pribufnr]->index_data,
			     S->priority[pribufnr]->index_xl, 1,
                             GFX_CROSSBLIT_FLAG_DATA_IS_HOMED);

	XPutImage(S->display, S->visual[bufnr], S->gc, tempimg,
		  0, 0, dest.x, dest.y, dest.xl, dest.yl);

	XDestroyImage(tempimg);
	return GFX_OK;
}

static int
xlib_grab_pixmap(struct _gfx_driver *drv, rect_t src, gfx_pixmap_t *pxm,
                 gfx_map_mask_t map)
{

	if (src.x < 0 || src.y < 0) {
		ERROR("Attempt to grab pixmap from invalid coordinates (%d,%d)\n", src.x, src.y);
		return GFX_ERROR;
	}

	if (!pxm->data) {
		ERROR("Attempt to grab pixmap to unallocated memory\n");
		return GFX_ERROR;
	}

	switch (map) {

	case GFX_MASK_VISUAL:
		pxm->xl = src.xl;
		pxm->yl = src.yl;
		pxm->internal.info = XGetImage(S->display, S->visual[1], src.x, src.y,
					       src.xl, src.yl, 0xffffffff, ZPixmap);
		pxm->internal.handle = SCI_XLIB_PIXMAP_HANDLE_GRABBED;
		pxm->flags |= GFX_PIXMAP_FLAG_INSTALLED | GFX_PIXMAP_FLAG_EXTERNAL_PALETTE | GFX_PIXMAP_FLAG_PALETTE_SET;
		free(pxm->data);
		pxm->data = (byte *) ((XImage *)(pxm->internal.info))->data;
		break;

	case GFX_MASK_PRIORITY:
		ERROR("FIXME: priority map grab not implemented yet!\n");
		break;

	default:
		ERROR("Attempt to grab pixmap from invalid map 0x%02x\n", map);
		return GFX_ERROR;
	}

	return GFX_OK;
}


  /*** Buffer operations ***/

static int
xlib_update(struct _gfx_driver *drv, rect_t src, point_t dest, gfx_buffer_t buffer)
{
	int data_source = (buffer == GFX_BUFFER_BACK)? 2 : 1;
	int data_dest = data_source - 1;


	if (src.x != dest.x || src.y != dest.y) {
		DEBUGU("Updating %d (%d,%d)(%dx%d) to (%d,%d)\n", buffer, src.x, src.y,
		       src.xl, src.yl, dest.x, dest.y);
	} else {
		DEBUGU("Updating %d (%d,%d)(%dx%d)\n", buffer, src.x, src.y, src.xl, src.yl);
	}

	XCopyArea(S->display, S->visual[data_source], S->visual[data_dest], S->gc,
		  src.x, src.y, src.xl, src.yl, dest.x, dest.y);

	if (buffer == GFX_BUFFER_BACK && (src.x == dest.x) && (src.y == dest.y))
		gfx_copy_pixmap_box_i(S->priority[0], S->priority[1], src);
	else {
		gfx_color_t col;
		col.mask = GFX_MASK_VISUAL;
		col.visual.r = 0xff;
		col.visual.g = 0;
		col.visual.b = 0;

		/*src.xl = 640;
		src.yl = 400;
		src.x = src.y = dest.x = dest.y = 0;*/
		XCopyArea(S->display, S->visual[0], S->window, S->gc,
			  dest.x, dest.y, src.xl, src.yl, dest.x, dest.y);
	}

	return GFX_OK;
}

static int
xlib_set_static_buffer(struct _gfx_driver *drv, gfx_pixmap_t *pic, gfx_pixmap_t *priority)
{

	if (!pic->internal.info) {
		ERROR("Attempt to set static buffer with unregisterd pixmap!\n");
		return GFX_ERROR;
	}
	XPutImage(S->display, S->visual[2], S->gc, (XImage *) pic->internal.info,
		  0, 0, 0, 0, 320 * XFACT, 200 * YFACT);
	gfx_copy_pixmap_box_i(S->priority[1], priority, gfx_rect(0, 0, 320*XFACT, 200*YFACT));

	return GFX_OK;
}

  /*** Palette operations ***/

static int
sdl_set_palette(struct _gfx_driver *drv, int index, byte red, byte green, byte blue)
{
  S->colors[index].r = red;
  S->colors[index].g = green;
  S->colors[index].b = blue;

  SDL_SetColors(S->primary, S->colors, 0, 256);
  return GFX_OK;
}


  /*** Mouse pointer operations ***/

static SDL_Cursor
*sdl_create_cursor_data(gfx_driver_t *drv, gfx_pixmap_t *pointer)
{
  char *visual_data, *mask_data;

  S->pointer_data[0] = visual_data = xlib_create_cursor_data(drv, pointer, 1);
  S->pointer_data[1] = mask_data = xlib_create_cursor_data(drv, pointer, 0);

  return SDL_CreateCursor(visual_data, mask_data, 
			  pointer->xl *XFACT, pointer->yl * YFACT,
			  pointer->xoffset *XFACT, pointer->yoffset * YFACT);
  
}

static int sdl_set_pointer (struct _gfx_driver *drv, gfx_pixmap_t *pointer)
{
  SDL_Cursor *cursor;
  int i;
  
  if (pointer == NULL)
    SDL_ShowCursor(SDL_DISABLE);
  else {
    for (i = 0; i < 2; i++)
      if (S->pointer_data[i]) {
	free(S->pointer_data[i]);
	S->pointer_data[i] = NULL;
      }
    SDL_FreeCursor(SDL_GetCursor());
    SDL_SetCursor(sdl_create_cursor_data(drv, pointer));
  }  

  return 0;
}

  /*** Event management ***/

int
sdl_unmap_key(gfx_driver_t *drv, SDL_keysym keysym)
{
  SDLKey skey = keysym.sym;
  int rkey = keysym.unicode;

if (S->flags & SCI_XLIB_SWAP_CTRL_CAPS) {
  switch (skey) {
  case SDLK_LCTRL: skey = SDLK_CAPSLOCK; break;
  case SDLK_CAPSLOCK: skey = SDLK_LCTRL; break;
  }
}

  switch (skey) {
  case SDLK_LCTRL:
  case SDLK_RCTRL: S->buckystate &= ~SCI_EVM_CTRL; return 0;
  case SDLK_LALT:
  case SDLK_RALT: S->buckystate &= ~SCI_EVM_ALT; return 0;
  case SDLK_LSHIFT: S->buckystate &= ~SCI_EVM_LSHIFT; return 0;
  case SDLK_RSHIFT: S->buckystate &= ~SCI_EVM_RSHIFT; return 0;
  }

  return 0;
}


int
sdl_map_key(gfx_driver_t *drv, SDL_keysym keysym)
{
  SDLKey skey = keysym.sym;
  int rkey = keysym.unicode;
   
  if ((skey >= SDLK_a) && (skey <= SDLK_z)) {
    if ((keysym.mod & KMOD_SHIFT) || (keysym.mod & KMOD_CAPS)) 
      return (rkey + 32);
      else
	return rkey;
  }

  if ((skey >= SDLK_0) && (skey <= SDLK_9))
    return rkey;
  
  if (S->flags & SCI_XLIB_SWAP_CTRL_CAPS) {
    switch (skey) {
    case SDLK_LCTRL: skey = SDLK_CAPSLOCK; break;
    case SDLK_CAPSLOCK: skey = SDLK_LCTRL; break;
    }
  }

  switch (skey) {
    /* XXXX catch KMOD_NUM for KP0-9 */
  case SDLK_BACKSPACE: return SCI_K_BACKSPACE;
  case SDLK_TAB: return 9;
  case SDLK_ESCAPE: return SCI_K_ESC;
  case SDLK_RETURN:
  case SDLK_KP_ENTER: return SCI_K_ENTER;
  case SDLK_KP_PERIOD: return SCI_K_DELETE;
  case SDLK_KP0:
  case SDLK_INSERT: return SCI_K_INSERT;
  case SDLK_KP1:
  case SDLK_END: return SCI_K_END;
  case SDLK_KP2:
  case SDLK_DOWN: return SCI_K_DOWN;
  case SDLK_KP3:
  case SDLK_PAGEDOWN: return SCI_K_PGDOWN;
  case SDLK_KP4:
  case SDLK_LEFT: return SCI_K_LEFT;
  case SDLK_KP5: return SCI_K_CENTER;
  case SDLK_KP6:
  case SDLK_RIGHT: return SCI_K_RIGHT;
  case SDLK_KP7:
  case SDLK_HOME: return SCI_K_HOME;
  case SDLK_KP8:
  case SDLK_UP: return SCI_K_UP;
  case SDLK_KP9:
  case SDLK_PAGEUP: return SCI_K_PGUP;
    
  case SDLK_F1: return SCI_K_F1;
  case SDLK_F2: return SCI_K_F2;
  case SDLK_F3: return SCI_K_F3;
  case SDLK_F4: return SCI_K_F4;
  case SDLK_F5: return SCI_K_F5;
  case SDLK_F6: return SCI_K_F6;
  case SDLK_F7: return SCI_K_F7;
  case SDLK_F8: return SCI_K_F8;
  case SDLK_F9: return SCI_K_F9;
  case SDLK_F10: return SCI_K_F10;

  case SDLK_LCTRL:
  case SDLK_RCTRL: S->buckystate |= SCI_EVM_CTRL; return 0;
  case SDLK_LALT:
  case SDLK_RALT: S->buckystate |= SCI_EVM_ALT; return 0;
  case SDLK_CAPSLOCK: S->buckystate ^= SCI_EVM_CAPSLOCK; return 0;
  case SDLK_SCROLLOCK: S->buckystate ^= SCI_EVM_SCRLOCK; return 0;
  case SDLK_NUMLOCK: S->buckystate ^= SCI_EVM_NUMLOCK; return 0;
  case SDLK_LSHIFT: S->buckystate |= SCI_EVM_LSHIFT; return 0;
  case SDLK_RSHIFT: S->buckystate |= SCI_EVM_RSHIFT; return 0;
    
  case SDLK_PLUS:
  case SDLK_KP_PLUS: return '+';
  case SDLK_SLASH:
  case SDLK_KP_DIVIDE: return '/';
  case SDLK_MINUS:
  case SDLK_KP_MINUS: return '-';
  case SDLK_ASTERISK:
  case SDLK_KP_MULTIPLY: return '*';
  case SDLK_EQUALS:
  case SDLK_KP_EQUALS: return '=';
    
  case SDLK_COMMA:
  case SDLK_PERIOD:
  case SDLK_BACKSLASH:
  case SDLK_SEMICOLON:
  case SDLK_QUOTE:
  case SDLK_LEFTBRACKET:
  case SDLK_RIGHTBRACKET:
  case SDLK_BACKQUOTE:
  case SDLK_LESS:
  case SDLK_GREATER:
  case SDLK_SPACE:   return rkey;
    
  }

  sciprintf("Unknown SDL keysym: %04x (%d) \n", skey, rkey);
  return 0;
}


void
sdl_fetch_event(gfx_driver_t *drv, long wait_usec, sci_event_t *sci_event)
{
  SDL_Event event;
  int x_button_xlate[] = {0, 1, 3, 2, 4, 5};
  struct timeval ctime, timeout_time, sleep_time;
  int usecs_to_sleep;
  
  gettimeofday(&timeout_time, NULL);
  timeout_time.tv_usec += wait_usec;
  
  /* Calculate wait time */
  timeout_time.tv_sec += (timeout_time.tv_usec / 1000000);
  timeout_time.tv_usec %= 1000000;
  
  do {
    int redraw_pointer_request = 0;

    while (SDL_PollEvent(&event)) {

      switch (event.type) {
      case SDL_KEYDOWN:
	sci_event->type = SCI_EVT_KEYBOARD;
	sci_event->buckybits = S->buckystate;
	sci_event->data = sdl_map_key(drv, event.key.keysym);
	if (sci_event->data)
	  return;
	break;
      case SDL_KEYUP:
	sdl_unmap_key(drv, event.key.keysym);
	break;
      case SDL_MOUSEBUTTONDOWN:
	sci_event->type = SCI_EVT_MOUSE_PRESS;
	sci_event->buckybits = S->buckystate;
	sci_event->data = event.button.button - 1;
	drv->pointer_x = event.button.x;
	drv->pointer_y = event.button.y;
	return;
      case SDL_MOUSEBUTTONUP:
	sci_event->type = SCI_EVT_MOUSE_RELEASE;
	sci_event->buckybits = S->buckystate;
	sci_event->data = event.button.button - 1;
	drv->pointer_x = event.button.x;
	drv->pointer_y = event.button.y;
	return;
      case SDL_MOUSEMOTION:
	drv->pointer_x = event.motion.x;
	drv->pointer_y = event.motion.y;
	break;
      default:
	ERROR("Received unhandled SDL event %04x\n", event.type);
      }
    }

    gettimeofday(&ctime, NULL);
    
    usecs_to_sleep = (timeout_time.tv_sec > ctime.tv_sec)? 1000000 : 0;
    usecs_to_sleep += timeout_time.tv_usec - ctime.tv_usec;
    if (ctime.tv_sec > timeout_time.tv_sec) usecs_to_sleep = -1;
    
    
    if (usecs_to_sleep > 0) {
      
      if (usecs_to_sleep > 10000)
	usecs_to_sleep = 10000; /* Sleep for a maximum of 10 ms */
      
      sleep_time.tv_usec = usecs_to_sleep;
      sleep_time.tv_sec = 0;
      
      select(0, NULL, NULL, NULL, &sleep_time); /* Sleep. */
    }
    
  } while (usecs_to_sleep >= 0);
  
  if (sci_event)
    sci_event->type = SCI_EVT_NONE; /* No event. */
}

static sci_event_t
sdl_get_event(struct _gfx_driver *drv)
{
	sci_event_t input;

	/* XXXX write me! */
	sdl_fetch_event(drv, 0, &input);
	return input;
}


static int
sdl_usec_sleep(struct _gfx_driver *drv, int usecs)
{
  struct timeval ctime;
  if (usecs > 10000)
    usecs = 10000;
  
  ctime.tv_sec = 0;
  ctime.tv_usec = usecs;
  
  select(0, NULL, NULL, NULL, &ctime); /* Sleep. */

  return GFX_OK;
}

gfx_driver_t
gfx_driver_sdl = {
	"sdl",
	"0.1",
	NULL,
	0, 0,
	GFX_CAPABILITY_STIPPLED_LINES | GFX_CAPABILITY_MOUSE_SUPPORT | GFX_CAPABILITY_MOUSE_POINTER |
	GFX_CAPABILITY_PIXMAP_REGISTRY | GFX_CAPABILITY_PIXMAP_GRABBING,
	0/*GFX_DEBUG_POINTER | GFX_DEBUG_UPDATES | GFX_DEBUG_PIXMAPS | GFX_DEBUG_BASIC*/,
	sdl_set_parameter,
	sdl_init_specific,
	sdl_init,
	sdl_exit,
	xlib_draw_line,
	xlib_draw_filled_rect,
	xlib_register_pixmap,
	xlib_unregister_pixmap,
	xlib_draw_pixmap,
	xlib_grab_pixmap,
	xlib_update,
	xlib_set_static_buffer,
	sdl_set_pointer,
	sdl_set_palette,
	sdl_get_event,
	sdl_usec_sleep,
	NULL
};

#endif /* HAVE_SDL */