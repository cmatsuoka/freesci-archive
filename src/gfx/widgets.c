/***************************************************************************
 widgets.c Copyright (C) 2000 Christoph Reichenbach


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

#include <gfx_widgets.h>

#ifdef GFXW_DEBUG_WIDGETS
gfxw_widget_t *debug_widgets[GFXW_DEBUG_WIDGETS];
int debug_widget_pos = 0;
#define inline
static void
_gfxw_debug_add_widget(gfxw_widget_t *widget)
{
	if (debug_widget_pos == GFXW_DEBUG_WIDGETS) {
		GFXERROR("WIDGET DEBUG: Allocated the maximum of %d widgets- Aborting!\n", GFXW_DEBUG_WIDGETS);
		BREAKPOINT();
	}
	debug_widgets[debug_widget_pos++] = widget;
	GFXDEBUG("Added widget: %d active\n", debug_widget_pos);
}

static void
_gfxw_debug_remove_widget(gfxw_widget_t *widget) {
	int i;
	int found = 0;
	for (i = 0; i < debug_widget_pos; i++) {
		if (debug_widgets[i] == widget) {
			memcpy(debug_widgets + i, debug_widgets + i + 1,
			       (sizeof (gfxw_widget_t *)) * (debug_widget_pos - i - 1));
			debug_widgets[debug_widget_pos--] = NULL;
			found++;
		}
	}

	if (found > 1) {
		GFXERROR("While removing widget: Found it %d times!\n", found);
		BREAKPOINT();
	}

	if (found == 0) {
		GFXERROR("Failed to remove widget!\n");
		BREAKPOINT();
	}
	GFXDEBUG("Removed widget: %d active now\n", debug_widget_pos);
}
#else /* !GFXW_DEBUG_WIDGETS */
#define _gfxw_debug_add_widget(a)
#define _gfxw_debug_remove_widget(a)
#endif


static inline void
indent(int indentation)
{
	int i;
	for (i = 0; i < indentation; i++)
		sciprintf("    ");
}

static void
_gfxw_print_widget(gfxw_widget_t *widget, int indentation)
{
	int i;
	char flags_list[] = "VOCDT";
	gfxw_view_t *view = (gfxw_view_t *) widget;
	gfxw_dyn_view_t *dyn_view = (gfxw_dyn_view_t *) widget;
	gfxw_text_t *text = (gfxw_text_t *) widget;
	gfxw_list_t *list = (gfxw_list_t *) widget;
	gfxw_port_t *port = (gfxw_port_t *) widget;
	gfxw_primitive_t *primitive = (gfxw_primitive_t *) widget;

	indent(indentation);

	if (widget->magic == GFXW_MAGIC_VALID)
		sciprintf("v ");
	else if (widget->magic == GFXW_MAGIC_INVALID)
		sciprintf("INVALID ");

	if (widget->ID != GFXW_NO_ID)
		sciprintf("#%08x ", widget->ID);

	sciprintf("[(%d,%d)(%dx%d)]", widget->bounds.x, widget->bounds.y, widget->bounds.xl, widget->bounds.yl);

	for (i = 0; i < strlen(flags_list); i++)
		if (widget->flags & (1 << i))
			sciprintf("%c", flags_list[i]);

	sciprintf(" ");
}

static
_gfxwop_print_empty(gfxw_widget_t *widget, int indentation)
{
	_gfxw_print_widget(widget, indentation);
	sciprintf("<untyped #%d>", widget->type);
}


static gfxw_widget_t *
_gfxw_new_widget(int size, int type)
{
	gfxw_widget_t *widget = malloc(size);
	widget->magic = GFXW_MAGIC_VALID;
	widget->parent = NULL;
	widget->visual = NULL;
	widget->next = NULL;
	widget->type = type;
	widget->bounds = gfx_rect(0, 0, 0, 0);
	widget->flags = GFXW_FLAG_DIRTY;
	widget->ID = GFXW_NO_ID;

	widget->draw = NULL;
	widget->free = NULL;
	widget->tag = NULL;
	widget->print = _gfxwop_print_empty;
	widget->compare_to = widget->equals = widget->superarea_of = NULL;

	_gfxw_debug_add_widget(widget);

	return widget;
}


static inline int
verify_widget(gfxw_widget_t *widget)
{
	if (!widget) {
		GFXERROR("Attempt to use NULL widget\n");
#ifdef GFXW_DEBUG_WIDGETS
		BREAKPOINT();
#endif /* GFXW_DEBUG_WIDGETS */
		return 1;
	} else if (widget->magic != GFXW_MAGIC_VALID) {
		if (widget->magic = GFXW_MAGIC_INVALID) {
			GFXERROR("Attempt to use invalidated widget\n");
		} else {
			GFXERROR("Attempt to use non-widget\n");
		}
#ifdef GFXW_DEBUG_WIDGETS
		BREAKPOINT();
#endif /* GFXW_DEBUG_WIDGETS */
		return 1;
	}
	return 0;
}

#define VERIFY_WIDGET(w) \
  if (verify_widget((gfxw_widget_t *)(w))) { GFXERROR("Error occured while validating widget\n"); }

static void
_gfxw_unallocate_widget(gfx_state_t *state, gfxw_widget_t *widget)
{
	if (GFXW_IS_TEXT(widget)) {
		gfxw_text_t *text = (gfxw_text_t *) widget;

		if (text->text_handle) {
			if (!state) {
				GFXERROR("Attempt to free text without supplying mode to free it from!\n");
				BREAKPOINT();
			} else {
				gfxop_free_text(state, text->text_handle);
				text->text_handle = NULL;
			}
		}
	}

	widget->magic = GFXW_MAGIC_INVALID;
	free(widget);
	_gfxw_debug_remove_widget(widget);
}

static inline void
tabulate(int c)
{
	while (c--)
		sciprintf(" ");
}

static rect_t no_rect = {-1, -1, -1, -1}; /* Never matched by gfx_rects_overlap() */
static rect_t fullscreen_rect = {0, 0, 320, 200};

static inline rect_t
gfxw_rects_merge(rect_t a, rect_t b)
{
	if (a.xl == -1) /* no_rect */
		return b;
	if (b.xl == -1) /* no_rect */
		return a;

	return gfx_rects_merge(a, b);
}

static inline int
gfxw_rect_isin(rect_t a, rect_t b)
     /* Returns whether forall x. x in a -> x in b */
{
	return b.x >= a.x
		&& b.y >= a.y
		&& (b.x + b.xl) <= (a.x + a.xl)
		&& (b.y + b.yl) <= (a.y + a.yl);
}

#define GFX_ASSERT(__x) \
  { \
	  int retval = (__x); \
	  if (retval == GFX_ERROR) { \
		  GFXERROR("Error occured while drawing widget!\n"); \
		  return 1; \
	  } else if (retval == GFX_FATAL) { \
		  GFXERROR("Fatal error occured while drawing widget!\nGraphics state invalid; aborting program..."); \
		  exit(1); \
	  } \
  }


/**********************************/
/*********** Widgets **************/
/**********************************/

/* Base class operations and common stuff */

/* Assertion for drawing */
#define DRAW_ASSERT(widget, exp_type) \
  if (!(widget)) { \
	sciprintf("L%d: NULL widget!\n", __LINE__); \
	return 1; \
  } \
  if (!(widget)->print) { \
	  sciprintf("L%d: Widget of type %d does not have print function!\n", __LINE__, \
		    (widget)->type); \
  } \
  if ((widget)->type != (exp_type)) { \
	  sciprintf("L%d: Error in widget: Expected type " # exp_type "(%d) but got %d\n", \
		    __LINE__, exp_type, (widget)->type); \
	  sciprintf("Erroneous widget: "); \
	  widget->print(widget, 4); \
	  sciprintf("\n"); \
	  return 1; \
  } \
  if (!(widget->type == GFXW_VISUAL || widget->visual)) { \
	  sciprintf("L%d: Error while drawing widget: Widget has no visual\n", __LINE__); \
	  sciprintf("Erroneous widget: "); \
	  widget->print(widget, 1); \
	  sciprintf("\n"); \
	  return 1; \
  }


static inline int
_color_equals(gfx_color_t a, gfx_color_t b)
{
	if (a.mask != b.mask)
		return 0;

	if (a.mask & GFX_MASK_VISUAL) {
		if (a.visual.r != b.visual.r
		    || a.visual.g != b.visual.g
		    || a.visual.b != b.visual.b
		    || a.alpha != b.alpha)
			return 0;
	}

	if (a.mask & GFX_MASK_PRIORITY)
		if (a.priority != b.priority)
			return 0;

	if (a.mask & GFX_MASK_CONTROL)
		if (a.control != b.control)
			return 0;

	return 1;
}

static int
_gfxwop_basic_set_visual(gfxw_widget_t *widget, gfxw_visual_t *visual)
{
	widget->visual = visual;

	if (widget->parent)
		widget->parent->add_dirty_rel(widget->parent, widget->bounds, 1);

	return 0;
}


static inline void
_gfxw_set_ops(gfxw_widget_t *widget, gfxw_point_op *draw, gfxw_op *free, gfxw_op *tag, gfxw_op_int *print,
	      gfxw_bin_op *compare_to, gfxw_bin_op *equals, gfxw_bin_op *superarea_of)
{
	widget->draw = draw;
	widget->free = free;
	widget->tag = tag;
	widget->print = print;
	widget->compare_to = compare_to;
	widget->equals = equals;
	widget->superarea_of = superarea_of;

	widget->set_visual = _gfxwop_basic_set_visual;
}

static void
_gfxw_remove_widget(gfxw_container_t *container, gfxw_widget_t *widget)
{
	gfxw_widget_t **seekerp = &(container->contents);

	if (GFXW_IS_LIST(widget) && GFXW_IS_PORT(container)) {
	        gfxw_port_t *port = (gfxw_port_t *) container;
		if (port->decorations == (gfxw_list_t *) widget)
			port->decorations = NULL;
		return;
	}

	while (*seekerp && *seekerp != widget)
		seekerp = &((*seekerp)->next);

	if (!*seekerp) {
		GFXERROR("Internal error: Attempt to remove widget from container it was not contained in!\n");
		sciprintf("Widget:");
		widget->print(GFXW(widget), 1);
		sciprintf("Container:");
		widget->print(GFXW(container), 1);
		return;
	}

	if (container->nextpp == &(widget->next))
		container->nextpp = seekerp;

	*seekerp = widget->next; /* Remove it */
}

static int
_gfxwop_basic_free(gfxw_widget_t *widget)
{
	gfx_state_t *state = (widget->visual)? widget->visual->gfx_state : NULL;
	if (widget->parent) {
		if (GFXW_IS_CONTAINER(widget))
			widget->parent->add_dirty_abs(widget->parent, widget->bounds, 1);
		else
			widget->parent->add_dirty_rel(widget->parent, widget->bounds, 1);

		_gfxw_remove_widget(widget->parent, widget);
	}
	
	_gfxw_unallocate_widget(state, widget);

	return 0;
}


static int
_gfxwop_basic_tag(gfxw_widget_t *widget)
{
	widget->flags |= GFXW_FLAG_TAGGED;

	return 0;
}


static int
_gfxwop_basic_compare_to(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	return -1;
}


static int
_gfxwop_basic_equals(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	return 0;
}

static int
_gfxwop_basic_superarea_of(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	return (widget == other);
}

/*-------------*/
/**** Boxes ****/
/*-------------*/

static inline rect_t
_move_rect(rect_t rect, point_t point)
{
	return gfx_rect(rect.x + point.x, rect.y + point.y, rect.xl, rect.yl);
}

static inline point_t
_move_point(rect_t rect, point_t point)
{
	return gfx_point(rect.x + point.x, rect.y + point.y);
}

static int
_gfxwop_box_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_box_t *box = (gfxw_box_t *) widget;
	DRAW_ASSERT(widget, GFXW_BOX);
	GFX_ASSERT(gfxop_draw_box(box->visual->gfx_state, _move_rect(box->bounds, pos), box->color1,
				  box->color2, box->shade_type));

	return 0;
}

static int
_gfxwop_box_print(gfxw_widget_t *widget, int indentation)
{
	_gfxw_print_widget(widget, indentation);
	sciprintf("BOX");
	return 0;
}

static int
_gfxwop_box_superarea_of(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	gfxw_box_t *box = (gfxw_box_t *) widget;

	if ((box->color1.mask & (GFX_MASK_VISUAL | GFX_MASK_CONTROL)) != (GFX_MASK_VISUAL | GFX_MASK_CONTROL))
		return 0;

	if (box->color1.alpha)
		return 0;

	if (box->shade_type != GFX_BOX_SHADE_FLAT && box->color2.alpha)
		return 0;

	if (!gfx_rect_subset(other->bounds, box->bounds))
		return 0;

	return 1;
}

static int
_gfxwop_box_equals(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	gfxw_box_t *wbox = (gfxw_box_t *) widget, *obox;
	if (other->type != GFXW_BOX)
		return 0;

	obox = (gfxw_box_t *) other;

	if (!gfx_rect_equals(wbox->bounds, obox->bounds))
		return 0;

	if (!_color_equals(wbox->color1, obox->color1))
		return 0;

	if (wbox->shade_type != obox->shade_type)
		return 0;

	if (wbox->shade_type != GFX_BOX_SHADE_FLAT
	    && _color_equals(wbox->color2, obox->color2))
		return 0;

	return 1;
}

gfxw_box_t *
gfxw_new_box(gfx_state_t *state, rect_t area, gfx_color_t color1, gfx_color_t color2, gfx_box_shade_t shade_type)
{
	gfxw_box_t *widget = (gfxw_box_t *) _gfxw_new_widget(sizeof(gfxw_box_t), GFXW_BOX);

	widget->bounds = area;
	widget->color1 = color1;
	widget->color2 = color2;
	widget->shade_type = shade_type;

	widget->flags |= GFXW_FLAG_VISIBLE;

	if (color1.mask & GFX_MASK_VISUAL
	    && (state && (state->driver->mode->palette))
		|| (!color1.alpha && !color2.alpha))
		widget->flags |= GFXW_FLAG_OPAQUE;

	_gfxw_set_ops(GFXW(widget), _gfxwop_box_draw,
		      _gfxwop_basic_free,
		      _gfxwop_basic_tag,
		      _gfxwop_box_print,
		      _gfxwop_basic_compare_to,
		      _gfxwop_box_equals,
		      _gfxwop_box_superarea_of);

	return widget;
}


static inline gfxw_primitive_t *
_gfxw_new_primitive(rect_t area, gfx_color_t color, gfx_line_mode_t mode, gfx_line_style_t style, int type)
{
	gfxw_primitive_t *widget = (gfxw_primitive_t *) _gfxw_new_widget(sizeof(gfxw_primitive_t), type);
	widget->bounds = area;
	widget->color = color;
	widget->line_mode = mode;
	widget->line_style = style;

	widget->flags |= GFXW_FLAG_VISIBLE;
	return widget;
}

/*------------------*/
/**** Rectangles ****/
/*------------------*/

static int
_gfxwop_primitive_equals(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	gfxw_primitive_t *wprim = (gfxw_primitive_t *) widget, *oprim;
	if (widget->type != other->type)
		return 0;

	oprim = (gfxw_primitive_t *) other;

	if (!gfx_rect_equals(wprim->bounds, oprim->bounds))
		return 0;

	if (!_color_equals(wprim->color, oprim->color))
		return 0;

	if (wprim->line_mode != oprim->line_mode)
		return 0;

	if (wprim->line_style != oprim->line_style)
		return 0;

	return 1;
}

static int
_gfxwop_rect_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_primitive_t *rect = (gfxw_primitive_t *) widget;
	DRAW_ASSERT(widget, GFXW_RECT);

	GFX_ASSERT(gfxop_draw_rectangle(rect->visual->gfx_state,
					gfx_rect(rect->bounds.x + pos.x, rect->bounds.y + pos.y,
						 rect->bounds.xl - 1, rect->bounds.yl - 1),
					rect->color, rect->line_mode, rect->line_style));

	return 0;
}

static int
_gfxwop_rect_print(gfxw_widget_t *rect, int indentation)
{
	_gfxw_print_widget(GFXW(rect), indentation);
	sciprintf("RECT");
	return 0;
}

gfxw_primitive_t *
gfxw_new_rect(rect_t rect, gfx_color_t color, gfx_line_mode_t line_mode, gfx_line_style_t line_style)
{
	gfxw_primitive_t *prim = _gfxw_new_primitive(rect, color, line_mode, line_style, GFXW_RECT);
	prim->bounds.xl++;
	prim->bounds.yl++; /* Since it is actually one pixel bigger in each direction */

	_gfxw_set_ops(GFXW(prim), _gfxwop_rect_draw,
		      _gfxwop_basic_free,
		      _gfxwop_basic_tag,
		      _gfxwop_rect_print,
		      _gfxwop_basic_compare_to,
		      _gfxwop_primitive_equals,
		      _gfxwop_basic_superarea_of);
	return prim;
}


/*-------------*/
/**** Lines ****/
/*-------------*/

static int
_gfxwop_line_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_primitive_t *line = (gfxw_primitive_t *) widget;
	DRAW_ASSERT(widget, GFXW_LINE);

	GFX_ASSERT(gfxop_draw_line(line->visual->gfx_state, _move_rect(line->bounds, pos),
				   line->color, line->line_mode, line->line_style));

	return 0;
}

static int
_gfxwop_line_print(gfxw_widget_t *widget, int indentation)
{
	_gfxw_print_widget(widget, indentation);
	sciprintf("LINE");
	return 0;
}

gfxw_primitive_t *
gfxw_new_line(rect_t line, gfx_color_t color, gfx_line_mode_t line_mode, gfx_line_style_t line_style)
{
	gfxw_primitive_t *prim = _gfxw_new_primitive(line, color, line_mode, line_style, GFXW_LINE);

	_gfxw_set_ops(GFXW(prim), _gfxwop_line_draw,
		      _gfxwop_basic_free,
		      _gfxwop_basic_tag,
		      _gfxwop_line_print,
		      _gfxwop_basic_compare_to,
		      _gfxwop_primitive_equals,
		      _gfxwop_basic_superarea_of);

	return prim;
}

/*------------------------------*/
/**** Views and static views ****/
/*------------------------------*/


gfxw_view_t *
_gfxw_new_simple_view(gfx_state_t *state, point_t pos, int view, int loop, int cel, int priority, int control,
		      gfx_alignment_t halign, gfx_alignment_t valign, int size, int type)
{
	gfxw_view_t *widget;
	int width, height;
	point_t offset;

	if (!state) {
		GFXERROR("Attempt to create view widget with NULL state!\n");
		return NULL;
	}

	if (gfxop_get_cel_parameters(state, view, loop, cel, &width, &height, &offset)) {
		GFXERROR("Attempt to retreive cel parameters for (%d/%d/%d) failed (Maybe the values weren't checked beforehand?)\n",
			 view, cel, loop);
		return NULL;
	}

	widget = (gfxw_view_t *) _gfxw_new_widget(size, type);

	widget->pos = pos;
	widget->color.mask =
		(priority < 0)? 0 : GFX_MASK_PRIORITY
		| (control < 0)? 0 : GFX_MASK_CONTROL;
	widget->color.priority = priority;
	widget->color.control = control;
	widget->view = view;
	widget->loop = loop;
	widget->cel = cel;

	if (halign == ALIGN_CENTER)
	  widget->pos.x -= width >> 1;
	else if (halign == ALIGN_RIGHT)
	  widget->pos.x -= width;

	if (valign == ALIGN_CENTER)
          widget->pos.y -= height >> 1;
	else if (valign == ALIGN_BOTTOM)
	  widget->pos.y -= height;

	widget->bounds = gfx_rect(widget->pos.x - offset.x, widget->pos.y - offset.y, width, height);

	widget->flags |= GFXW_FLAG_VISIBLE;

	return widget;
}

static int
_gfxwop_view_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_view_t *view = (gfxw_view_t *) widget;
	DRAW_ASSERT(widget, GFXW_VIEW);

	GFX_ASSERT(gfxop_draw_cel(view->visual->gfx_state, view->view, view->loop,
				  view->cel, gfx_point(view->pos.x + pos.x, view->pos.y + pos.y),
				  view->color));

	return 0;
}

static int
_gfxwop_static_view_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_view_t *view = (gfxw_view_t *) widget;
	DRAW_ASSERT(widget, GFXW_VIEW);

	GFX_ASSERT(gfxop_draw_cel_static(view->visual->gfx_state, view->view, view->loop,
					 view->cel, _move_point(view->bounds, pos),
					 view->color));

	return 0;
}

static int
__gfxwop_view_print(gfxw_widget_t *widget, char *name, int indentation)
{
	gfxw_view_t *view = (gfxw_view_t *) widget;
	_gfxw_print_widget(widget, indentation);

	sciprintf(name);
	sciprintf("(%d/%d/%d)@(%d,%d)[p:%d,c:%d]", view->view, view->loop, view->cel, view->pos.x, view->pos.y,
		  (view->color.mask & GFX_MASK_PRIORITY)? view->color.priority : -1,
		  (view->color.mask & GFX_MASK_CONTROL)? view->color.control : -1);

	return 0;
}

static int
_gfxwop_view_print(gfxw_widget_t *widget, int indentation)
{
	return __gfxwop_view_print(widget, "VIEW", indentation);
}

static int
_gfxwop_static_view_print(gfxw_widget_t *widget, int indentation)
{
	return __gfxwop_view_print(widget, "PICVIEW", indentation);
}

gfxw_view_t *
gfxw_new_view(gfx_state_t *state, point_t pos, int view_nr, int loop, int cel, int priority, int control,
	      gfx_alignment_t halign, gfx_alignment_t valign, int flags)
{
	gfxw_view_t *view;

	if (flags & GFXW_VIEW_FLAG_DONT_MODIFY_OFFSET) {
		int foo;
		point_t offset;
		gfxop_get_cel_parameters(state, view_nr, loop, cel, &foo, &foo, &offset);
		pos.x += offset.x;
		pos.y += offset.y;
	}

	view = _gfxw_new_simple_view(state, pos, view_nr, loop, cel, priority, control, halign, valign, 
				     sizeof(gfxw_view_t), (flags & GFXW_VIEW_FLAG_STATIC) ? GFXW_STATIC_VIEW : GFXW_VIEW);

	_gfxw_set_ops(GFXW(view), (flags & GFXW_VIEW_FLAG_STATIC) ? _gfxwop_static_view_draw : _gfxwop_view_draw,
		      _gfxwop_basic_free,
		      _gfxwop_basic_tag,
		      (flags & GFXW_VIEW_FLAG_STATIC) ? _gfxwop_static_view_print : _gfxwop_view_print,
		      _gfxwop_basic_compare_to,
		      _gfxwop_basic_equals,
		      _gfxwop_basic_superarea_of);

	return view;
}

/*---------------------*/
/**** Dynamic Views ****/
/*---------------------*/

static int
_gfxwop_dyn_view_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_view_t *view = (gfxw_view_t *) widget;
	DRAW_ASSERT(widget, GFXW_DYN_VIEW);

	GFX_ASSERT(gfxop_draw_cel(view->visual->gfx_state, view->view, view->loop,
				  view->cel, _move_point(view->bounds, pos),
				  view->color));

	return 0;
}

static int
_gfxwop_dyn_view_print(gfxw_widget_t *widget, int indentation)
{
	return __gfxwop_view_print(widget, "DYNVIEW", indentation);
}

static int
_gfxwop_dyn_view_equals(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	gfxw_dyn_view_t *wview = (gfxw_dyn_view_t *) widget, *oview;
	if (other->type != GFXW_DYN_VIEW)
		return 0;

	oview = (gfxw_dyn_view_t *) other;

	if (wview->pos.x != oview->pos.x
	    || wview->pos.y != oview->pos.y
	    || wview->z != oview->z)
		return 0;

	if (wview->view != oview->view
	    || wview->loop != oview->loop
	    || wview->cel != oview->cel)
		return 0;

	if (!_color_equals(wview->color, oview->color))
		return 0;

	return 1;
}

static int
_gfxwop_dyn_view_compare_to(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	gfxw_dyn_view_t *wview = (gfxw_dyn_view_t *) widget, *oview;
	if (other->type != GFXW_DYN_VIEW)
		return 1;

	oview = (gfxw_dyn_view_t *) other;

	if (wview->bounds.y < oview->bounds.y)
		return -1;
	return (wview->z - oview->z);
}


gfxw_dyn_view_t *
gfxw_new_dyn_view(gfx_state_t *state, point_t pos, int z, int view, int loop, int cel, int priority, int control,
		  gfx_alignment_t halign, gfx_alignment_t valign)
{
	gfxw_dyn_view_t *widget =
		(gfxw_dyn_view_t *) _gfxw_new_simple_view(state, pos, view, loop, cel, priority, halign, valign,
							  control, sizeof(gfxw_dyn_view_t),
							  GFXW_DYN_VIEW);
	if (!widget) {
		GFXERROR("Invalid view widget (%d/%d/%d)!\n", view, loop, cel);
		return NULL;
	}

	widget->pos.y += z;
	widget->z = z;

	_gfxw_set_ops(GFXW(widget), _gfxwop_dyn_view_draw,
		      _gfxwop_basic_free,
		      _gfxwop_basic_tag,
		      _gfxwop_dyn_view_print,
		      _gfxwop_dyn_view_compare_to,
		      _gfxwop_dyn_view_equals,
		      _gfxwop_basic_superarea_of);

	return widget;
}

/*------------*/
/**** Text ****/
/*------------*/

static int
_gfxwop_text_free(gfxw_widget_t *widget)
{
	gfxw_text_t *text = (gfxw_text_t *) widget;
	free(text->text);
	return _gfxwop_basic_free(widget);
}

static int
_gfxwop_text_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_text_t *text = (gfxw_text_t *) widget;
	DRAW_ASSERT(widget, GFXW_TEXT);

	GFX_ASSERT(gfxop_draw_text(text->visual->gfx_state, text->text_handle, _move_rect(text->bounds, pos)));

	return 0;
}

static int
_gfxwop_text_alloc_and_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_text_t *text = (gfxw_text_t *) widget;
	DRAW_ASSERT(widget, GFXW_TEXT);

	text->text_handle =
		gfxop_new_text(widget->visual->gfx_state, text->font_nr, text->text, text->bounds.xl,
			       text->halign, text->valign, text->color1,
			       text->color2, text->bgcolor, text->text_flags);

	text->draw = _gfxwop_text_draw;

	return _gfxwop_text_draw(widget, pos);
}


static int
_gfxwop_text_print(gfxw_widget_t *widget, int indentation)
{
	_gfxw_print_widget(widget, indentation);
	sciprintf("TEXT:'%s'", ((gfxw_text_t *)widget)->text);
	return 0;
}


static int
_gfxwop_text_equals(gfxw_widget_t *widget, gfxw_widget_t *other)
{
	gfxw_text_t *wtext = (gfxw_text_t *) widget, *otext;
	if (other->type != GFXW_TEXT)
		return 0;

	otext = (gfxw_text_t *) other;

	if (!gfx_rect_equals(wtext->bounds, otext->bounds))
		return 0;

	if (wtext->halign != otext->halign
	    || wtext->valign != otext->valign)
		return 0;

	if (wtext->text_flags != otext->text_flags)
		return 0;

	if (!(_color_equals(wtext->color1, otext->color1)
	      && _color_equals(wtext->color2, otext->color2)
	      && _color_equals(wtext->bgcolor, otext->bgcolor)))
		return 0;

	if (strcmp(wtext->text, otext->text))
		return 0;

	return 1;
}


gfxw_text_t *
gfxw_new_text(gfx_state_t *state, rect_t area, int font, char *text, gfx_alignment_t halign,
	      gfx_alignment_t valign, gfx_color_t color1, gfx_color_t color2,
	      gfx_color_t bgcolor, int text_flags)
{
	gfxw_text_t *widget = (gfxw_text_t *)
		_gfxw_new_widget(sizeof(gfxw_text_t), GFXW_TEXT);

	widget->bounds = area;

	widget->font_nr = font;
	widget->text = malloc(strlen(text) + 1);
	widget->halign = halign;
	widget->valign = valign;
	widget->color1 = color1;
	widget->color2 = color2;
	widget->bgcolor = bgcolor;
	widget->text_flags = text_flags;
	widget->text_handle = NULL;

	strcpy(widget->text, text);

	gfxop_get_text_params(state, font, text, area.xl, &(widget->width), &(widget->height));

	widget->flags |= GFXW_FLAG_VISIBLE;

	_gfxw_set_ops(GFXW(widget), _gfxwop_text_alloc_and_draw,
		      _gfxwop_text_free,
		      _gfxwop_basic_tag,
		      _gfxwop_text_print,
		      _gfxwop_basic_compare_to,
		      _gfxwop_text_equals,
		      _gfxwop_basic_superarea_of);

	return widget;
}




/***********************/
/*-- Container types --*/
/***********************/

static int
_gfxwop_container_add_dirty_rel(gfxw_container_t *cont, rect_t rect, int propagate)
{
	return cont->add_dirty_abs(cont, _move_rect(rect, gfx_point(cont->zone.x, cont->zone.y)), propagate);
}

static inline void
_gfxw_set_container_ops(gfxw_container_t *container, gfxw_point_op *draw, gfxw_op *free, gfxw_op *tag,
			gfxw_op_int *print, gfxw_bin_op *compare_to, gfxw_bin_op *equals,
			gfxw_bin_op *superarea_of, gfxw_visual_op *set_visual,
			gfxw_unary_container_op *free_tagged, gfxw_unary_container_op *free_contents,
			gfxw_rect_op *add_dirty, gfxw_container_op *add)
{
	_gfxw_set_ops(GFXW(container),
		      draw,
		      free,
		      tag,
		      print, 
		      compare_to,
		      equals,
		      superarea_of);

	container->free_tagged = free_tagged;
	container->free_contents = free_contents;
	container->add_dirty_abs = add_dirty;
	container->add_dirty_rel = _gfxwop_container_add_dirty_rel;
	container->add = add;
	container->set_visual = set_visual;
}

static int
__gfxwop_container_print_contents(char *name, gfxw_widget_t *widget, int indentation)
{
	gfxw_widget_t *seeker = widget;

	indent(indentation);

	sciprintf("--%s:\n", name);

	while (seeker) {
		seeker->print(seeker, indentation + 1);
		sciprintf("\n");
		seeker = seeker->next;
	}
}

static int
__gfxwop_container_print(gfxw_widget_t *widget, int indentation)
{
	gfxw_widget_t *seeker;
	gfx_dirty_rect_t *dirty;
	gfxw_container_t *container = (gfxw_container_t *) widget;
	if (!GFXW_IS_CONTAINER(widget)) {
		GFXERROR("__gfxwop_container_print() called on type %d widget\n", widget->type);
		return 1;
	}

	sciprintf(" viszone=((%d,%d),(%dx%d))\n", container->zone.x, container->zone.y,
		  container->zone.xl, container->zone.yl);

	indent(indentation);
	sciprintf("--dirty:\n");

	dirty = container->dirty;
	while (dirty) {
		indent(indentation + 1);
		sciprintf("dirty(%d,%d, (%dx%d))\n",
			dirty->rect.x, dirty->rect.y, dirty->rect.xl, dirty->rect.yl);
		dirty = dirty->next;
	}

	__gfxwop_container_print_contents("contents", container->contents, indentation);
}



gfxw_container_t *
_gfxw_new_container_widget(rect_t area, int size, int type)
{
	gfxw_container_t *widget = (gfxw_container_t *)
		_gfxw_new_widget(size, type);

	widget->bounds = widget->zone = area;
	widget->contents = NULL;
	widget->nextpp = &(widget->contents);
	widget->dirty = NULL;

	widget->flags |= GFXW_FLAG_VISIBLE | GFXW_FLAG_CONTAINER;

	return widget;
}


static void
recursively_free_dirty_rects(gfx_dirty_rect_t *dirty)
{
	if (dirty) {
		recursively_free_dirty_rects(dirty->next);
		free(dirty);
	}
}


int ti = 0;

static int
_gfxwop_container_draw_contents(gfxw_widget_t *widget, gfxw_widget_t *contents)
{
	gfxw_container_t *container = (gfxw_container_t *) widget;
	gfx_dirty_rect_t *dirty = container->dirty;
	gfx_state_t *gfx_state = (widget->visual)? widget->visual->gfx_state : ((gfxw_visual_t *) widget)->gfx_state;
	int draw_ports;

	if (!contents)
		return 0;

	while (dirty) {
		gfxw_widget_t *seeker = contents;

		while (seeker) {
			if (gfx_rects_overlap(seeker->bounds, dirty->rect)) {
				if (GFXW_IS_CONTAINER(seeker)) /* Propagate dirty rectangles /upwards/ */
					((gfxw_container_t *)seeker)->add_dirty_abs((gfxw_container_t *)seeker, dirty->rect, 0);

				seeker->flags |= GFXW_FLAG_DIRTY;
			}

			seeker = seeker->next;
		}

		dirty = dirty->next;
	}


	/* The draw loop is executed twice: Once for normal data, and once for ports. */
	for (draw_ports = 0; draw_ports < 2; draw_ports++) {

		dirty = container->dirty;

		while (dirty) {
			gfxw_widget_t *seeker = contents;

			while (seeker && (draw_ports || !GFXW_IS_PORT(seeker))) {
				if (seeker->flags & GFXW_FLAG_DIRTY) {
					GFX_ASSERT(gfxop_set_clip_zone(gfx_state, dirty->rect));
				/* Clip zone must be reset after each element in case it's a container.
				** Doing this is relatively cheap, though. */
					seeker->draw(seeker, gfx_point(container->zone.x, container->zone.y));

					if (!dirty->next)
						seeker->flags &= ~GFXW_FLAG_DIRTY;
				}

				seeker = seeker->next;
			}
			dirty = dirty->next;
		}
	}
	/* Remember that the dirty rects should be freed afterwards! */

	return 0;
}

static int
_gfxwop_container_free(gfxw_widget_t *widget)
{
	gfxw_container_t *container = (gfxw_container_t *) widget;
	gfxw_widget_t *seeker = container->contents;

	while (seeker) {
		gfxw_widget_t *next = seeker->next;
		seeker->free(seeker);
		seeker = next;
	}

	return _gfxwop_basic_free(widget);
}

static int
_gfxwop_container_tag(gfxw_widget_t *widget)
{
	gfxw_container_t *container = (gfxw_container_t *) widget;
	gfxw_widget_t *seeker = container->contents;

	while (seeker) {
		seeker->tag(seeker);
		seeker = seeker->next;
	}

}


static int
__gfxwop_container_set_visual_contents(gfxw_widget_t *contents, gfxw_visual_t *visual)
{
	while (contents) {
		contents->set_visual(contents, visual);

		contents = contents->next;
	}
	return 0;
}

static int
_gfxwop_container_set_visual(gfxw_widget_t *widget, gfxw_visual_t *visual)
{
	gfxw_container_t *container = (gfxw_container_t *) widget;

	container->visual = visual;
	if (widget->parent)
	    widget->parent->add_dirty_abs(widget->parent, widget->bounds, 1);

	return __gfxwop_container_set_visual_contents(container->contents, visual);
}

static int
_gfxwop_container_free_tagged(gfxw_container_t *container)
{
	gfxw_widget_t *seeker = container->contents;

	while (seeker) {
		gfxw_widget_t *next = seeker->next;

		if (seeker->flags & GFXW_FLAG_TAGGED)
			seeker->free(seeker);

		seeker = next;
	}
	return 0;
}

static int
_gfxwop_container_free_contents(gfxw_container_t *container)
{
	gfxw_widget_t *seeker = container->contents;

	while (seeker) {
		gfxw_widget_t *next = seeker->next;
		seeker->free(seeker);
		seeker = next;
	}
	return 0;
}

static void
_gfxw_dirtify_container(gfxw_container_t *container, gfxw_widget_t *widget)
{
	if (GFXW_IS_CONTAINER(widget))
		container->add_dirty_abs(GFXWC(container), widget->bounds, 1);
	else
		container->add_dirty_rel(GFXWC(container), widget->bounds, 1);
}

static int
_gfxw_container_id_equals(gfxw_container_t *container, gfxw_widget_t *widget)
{
	gfxw_widget_t **seekerp = &(container->contents);

	if (GFXW_IS_PORT(widget))
		return 0; /* Don't match ports */

	if (widget->ID == GFXW_NO_ID)
		return 0;

	while (*seekerp && (*seekerp)->ID != widget->ID)
		seekerp = &((*seekerp)->next);

	if (!*seekerp)
		return 0;

	if ((*seekerp)->equals(*seekerp, widget)) {
		widget->free(widget);
		(*seekerp)->flags &= ~GFXW_FLAG_DIRTY;
		return 1;
	} else {
		widget->next = (*seekerp)->next;
		(*seekerp)->free(*seekerp);
		*seekerp = widget;
		_gfxw_dirtify_container(container, widget);
		return 1;
	}
}


static int
_gfxwop_container_add_dirty(gfxw_container_t *container, rect_t dirty, int propagate)
{
	container->dirty = gfxdr_add_dirty(container->dirty, dirty, GFXW_DIRTY_STRATEGY);
	return 0;
}

static int
_gfxwop_container_add(gfxw_container_t *container, gfxw_widget_t *widget)
{
	if (widget->parent) {
		GFXERROR("_gfxwop_container_add(): Attempt to give second parent node to widget!\nWidget:");
		widget->print(GFXW(widget), 3);
		sciprintf("\nContainer:");
		container->print(GFXW(container), 3);

		return 1;
	}

	if (GFXW_IS_VISUAL(container))
		widget->set_visual(widget, (gfxw_visual_t *) container);
	else
		if (container->visual)
			widget->set_visual(widget, container->visual);
			

	if (_gfxw_container_id_equals(container, widget))
		return 0;

	_gfxw_dirtify_container(container, widget);

	*(container->nextpp) = widget;
	container->nextpp = &(widget->next);
	widget->parent = GFXWC(container);

	return 0;
}

/*------------------------------*/
/**** Lists and sorted lists ****/
/*------------------------------*/

static int
_gfxwop_list_draw(gfxw_widget_t *list, point_t pos)
{
	DRAW_ASSERT(list, GFXW_LIST);

	_gfxwop_container_draw_contents(list, ((gfxw_list_t *)list)->contents);
	recursively_free_dirty_rects(GFXWC(list)->dirty);
	GFXWC(list)->dirty = NULL;
	list->flags &= ~GFXW_FLAG_DIRTY;
	return 0;
}

static int
_gfxwop_sorted_list_draw(gfxw_widget_t *list, point_t pos)
{
	DRAW_ASSERT(list, GFXW_SORTED_LIST);

	_gfxwop_container_draw_contents(list, ((gfxw_list_t *)list)->contents);
	recursively_free_dirty_rects(GFXWC(list)->dirty);
	GFXWC(list)->dirty = NULL;
	return 0;
}

static inline int
__gfxwop_list_print(gfxw_widget_t *list, char *name, int indentation)
{
	_gfxw_print_widget(list, indentation);
	sciprintf(name);
	return __gfxwop_container_print(list, indentation);
}

static int
_gfxwop_list_print(gfxw_widget_t *list, int indentation)
{
	return __gfxwop_list_print(list, "LIST", indentation);
}

static int
_gfxwop_sorted_list_print(gfxw_widget_t *list, int indentation)
{
	return __gfxwop_list_print(list, "SORTED_LIST", indentation);
}

/* --- */
#if 0
struct gfxw_widget_list {
	gfxw_widget_t *widget;
	struct gfxw_widget_list *next;
};

static struct gfxw_widtet_list *
_gfxw_make_widget_list_recursive(gfxw_widget_t *widget)
{
	gfxw_widget_list *node;

	if (!widget)
		return NULL;

	node = malloc(sizeof(struct gfxw_widget_list));
	node->widget = widget;
	node->next = _gfxw_make_widget_list_recursive(widget->next);

	return node;
}

static struct gfxw_widget_list *
_gfxw_make_widget_list(gfxw_container_t *container)
{
	return _gfxw_make_widget_list_recursive(container->contents);
}
#endif
/* --- */


static int
_gfxwop_list_equals(gfxw_widget_t *widget, gfxw_widget_t *other)
     /* Requires identical order of list elements. */
{
	gfxw_list_t *wlist, *olist;
	gfxw_widget_t *wseeker, *oseeker;

	if (widget->type != other->type)
		return 0;

	if (!GFXW_IS_LIST(widget)) {
		GFXWARN("_gfxwop_list_equals(): Method called on non-list!\n");
		widget->print(widget, 0);
		sciprintf("\n");
		return 0;
	}

	wlist = (gfxw_list_t *) widget;
	olist = (gfxw_list_t *) other;

	widget = wlist->contents;
	other = olist->contents;

	while (widget && other) {

		if (!widget->equals(widget, other))
			return 0;

		widget = widget->next;
		other = other->next;
	}

	return (!widget && !other); /* True if both are finished now */
}

static int
_gfxwop_list_add_dirty(gfxw_container_t *container, rect_t dirty, int propagate)
{
	/* Lists add dirty boxes to both themselves and their parenting port/visual */

	if (propagate)
		if (container->parent)
			container->parent->add_dirty_abs(container->parent, dirty, 1);

	return _gfxwop_container_add_dirty(container, dirty, propagate);
}

static int
_gfxwop_sorted_list_add(gfxw_container_t *container, gfxw_widget_t *widget)
     /* O(n) */
{
	gfxw_widget_t **seekerp = &(container->contents);

	if (widget->next) {
		GFXERROR("_gfxwop_sorted_list_add(): Attempt to add widget to two lists!\nWidget:");
		widget->print(GFXW(widget), 3);
		sciprintf("\nList:");
		container->print(GFXW(container), 3);

		return 1;
	}

	if (_gfxw_container_id_equals(container, widget))
		return 0;

	while (*seekerp && (widget->compare_to(widget, *seekerp) < 0))
		seekerp = &((*seekerp)->next);

	widget->next = *seekerp;
	*seekerp = widget;

	return 0;
}

gfxw_list_t *
gfxw_new_list(rect_t area, int sorted)
{
	gfxw_list_t *list = (gfxw_list_t *) _gfxw_new_container_widget(area, sizeof(gfxw_list_t),
								       sorted? GFXW_SORTED_LIST : GFXW_LIST);

	_gfxw_set_container_ops((gfxw_container_t *) list,
				sorted? _gfxwop_sorted_list_draw : _gfxwop_list_draw,
				_gfxwop_container_free,
				_gfxwop_container_tag,
				sorted? _gfxwop_sorted_list_print : _gfxwop_list_print,
				_gfxwop_basic_compare_to,
				_gfxwop_list_equals,
				_gfxwop_basic_superarea_of,
				_gfxwop_container_set_visual,
				_gfxwop_container_free_tagged,
				_gfxwop_container_free_contents,
				_gfxwop_list_add_dirty,
				sorted? _gfxwop_sorted_list_add : _gfxwop_container_add);
}


/*---------------*/
/**** Visuals ****/
/*---------------*/

static int
_gfxwop_visual_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_visual_t *visual = (gfxw_visual_t *) widget;
	gfx_dirty_rect_t *dirty = visual->dirty;
	DRAW_ASSERT(widget, GFXW_VISUAL);

	while (dirty) {
		int err = gfxop_clear_box(visual->gfx_state, dirty->rect);

		if (err) {
			GFXERROR("Error while clearing dirty rect (%d,%d,(%dx%d))\n", dirty->rect.x,
				 dirty->rect.y, dirty->rect.xl, dirty->rect.yl);
			if (err = GFX_FATAL)
				return err;
		}

		dirty = dirty->next;
	}

	_gfxwop_container_draw_contents(widget, visual->contents);

	recursively_free_dirty_rects(visual->dirty);
	visual->dirty = NULL;
	widget->flags &= ~GFXW_FLAG_DIRTY;

	return 0;
}

static int
_gfxwop_visual_free(gfxw_widget_t *widget)
{
	gfxw_visual_t *visual = (gfxw_visual_t *) widget;

	if (!GFXW_IS_VISUAL(visual)) {
		GFXERROR("_gfxwop_visual_free() called on non-visual!Widget was: ");
		widget->print(widget, 3);
		return 1;
	}

	free(visual->port_refs);

	return _gfxwop_container_free(widget);
}

static int
_gfxwop_visual_print(gfxw_widget_t *widget, int indentation)
{
	int i;
	int comma = 0;
	gfxw_visual_t *visual = (gfxw_visual_t *) widget;

	if (!GFXW_IS_VISUAL(visual)) {
		GFXERROR("_gfxwop_visual_free() called on non-visual!Widget was: ");
		widget->print(widget, 3);
		return 1;
	}

	_gfxw_print_widget(widget, indentation);
	sciprintf("VISUAL; ports={");
	for (i = 0; i < visual->port_refs_nr; i++)
		if (visual->port_refs[i]) {
			if (comma)
				sciprintf(",");
			else
				comma = 1;

			sciprintf("%d", i);
		}
	sciprintf("}\n");

	return __gfxwop_container_print(widget, indentation);
}

static int
_gfxwop_visual_set_visual(gfxw_widget_t *self, gfxw_visual_t *visual)
{
	if (self != GFXW(visual)) {
		GFXWARN("Attempt to set a visual's parent visual to something else!\n");
	} else {
		GFXWARN("Attempt to set a visual's parent visual!\n");
	}
	return 1;
}

gfxw_visual_t *
gfxw_new_visual(gfx_state_t *state, int font)
{
	gfxw_visual_t *visual = (gfxw_visual_t *) _gfxw_new_container_widget(gfx_rect(0, 0, 320, 200), sizeof(gfxw_visual_t), GFXW_VISUAL);

	visual->font_nr = font;
	visual->gfx_state = state;

	visual->port_refs = calloc(sizeof(gfxw_port_t), visual->port_refs_nr = 16);

	_gfxw_set_container_ops((gfxw_container_t *) visual,
				_gfxwop_visual_draw,
				_gfxwop_visual_free,
				_gfxwop_container_tag,
				_gfxwop_visual_print,
				_gfxwop_basic_compare_to,
				_gfxwop_basic_equals,
				_gfxwop_basic_superarea_of,
				_gfxwop_visual_set_visual,
				_gfxwop_container_free_tagged,
				_gfxwop_container_free_contents,
				_gfxwop_container_add_dirty,
				_gfxwop_container_add);

	return visual;
}


static int
_visual_find_free_ID(gfxw_visual_t *visual)
{
	int id = 0;
	int newports = 16;

	while (visual->port_refs[id] && id < visual->port_refs_nr)
		id++;

	if (id == visual->port_refs_nr) {/* Out of ports? */
		visual->port_refs = realloc(visual->port_refs, visual->port_refs_nr += newports);
		memset(visual->port_refs + id, 0, newports * sizeof(gfxw_port_t *)); /* Clear new port refs */
	}

	return id;
}

_gfxwop_add_dirty_rects(gfxw_container_t *dest, gfx_dirty_rect_t *src)
{
	if (src) {
		dest->dirty = gfxdr_add_dirty(dest->dirty, src->rect, GFXW_DIRTY_STRATEGY);
		_gfxwop_add_dirty_rects(dest, src->next);
	}
}

/*-------------*/
/**** Ports ****/
/*-------------*/

static int
_gfxwop_port_draw(gfxw_widget_t *widget, point_t pos)
{
	gfxw_port_t *port = (gfxw_port_t *) widget;
	DRAW_ASSERT(widget, GFXW_PORT);

	if (port->decorations) {
		_gfxwop_add_dirty_rects(GFXWC(port->decorations), port->dirty);
		if (port->decorations->draw(GFXW(port->decorations), gfxw_point_zero)) {
			port->decorations->dirty = NULL;
			return 1;
		}
		port->decorations->dirty = NULL;
	}

	_gfxwop_container_draw_contents(widget, port->contents);

	recursively_free_dirty_rects(port->dirty);
	port->dirty = NULL;
	widget->flags &= ~GFXW_FLAG_DIRTY;
	return 0;
}

static int
_gfxwop_port_free(gfxw_widget_t *widget)
{
	gfxw_port_t *port = (gfxw_port_t *) widget;

	if (port->decorations)
		port->decorations->free(GFXW(port->decorations));

	return _gfxwop_container_free(widget);
}

static int
_gfxwop_port_print(gfxw_widget_t *widget, int indentation)
{
	gfxw_port_t *port = (gfxw_port_t *) widget;

	_gfxw_print_widget(widget, indentation);
	sciprintf("PORT");
	sciprintf(" font=%d drawpos=(%d,%d)", port->font_nr, port->draw_pos.x, port->draw_pos.y);
	if (port->gray_text)
		sciprintf(" (gray)");
	__gfxwop_container_print(GFXW(port), indentation);
	return __gfxwop_container_print_contents("decorations", GFXW(port->decorations), indentation);

}

static int
_gfxwop_port_superarea_of(gfxw_widget_t *self, gfxw_widget_t *other)
{
	gfxw_port_t *port = (gfxw_port_t *) self;

	if (!port->port_bg)
		return _gfxwop_basic_superarea_of(self, other);

	return port->port_bg->superarea_of(port->port_bg, other);
}

static int
_gfxwop_port_set_visual(gfxw_widget_t *widget, gfxw_visual_t *visual)
{
	gfxw_list_t *decorations = ((gfxw_port_t *) widget)->decorations;
	widget->visual = visual;

	if (decorations)
		if (decorations->set_visual(GFXW(decorations), visual)) {
			GFXWARN("Setting the visual for decorations failed for port ");
			widget->print(widget, 1);
			return 1;
		}

	return _gfxwop_container_set_visual(widget, visual);
}

static int
_gfxwop_port_add_dirty(gfxw_container_t *widget, rect_t dirty, int propagate)
{
	gfxw_port_t *self = (gfxw_port_t *) widget;
	_gfxwop_container_add_dirty(widget, dirty, propagate);

	if (self->port_bg) {
		gfxw_widget_t foo;

		foo.bounds = dirty; /* Yeah, sub-elegant, I know */
		if (self->port_bg->superarea_of(self->port_bg, &foo))
			return 0;
	} /* else propagate to the parent, since we're not 'catching' the dirty rect */

	if (propagate)
		if (self->parent)
			return self->parent->add_dirty_abs(self->parent, dirty, 1);

	return 0;
}

gfxw_port_t *
gfxw_new_port(gfxw_visual_t *visual, gfxw_port_t *predecessor, rect_t area, gfx_color_t fgcolor, gfx_color_t bgcolor)
{
	gfxw_port_t *widget = (gfxw_port_t *)
		_gfxw_new_container_widget(area, sizeof(gfxw_port_t), GFXW_PORT);

	VERIFY_WIDGET(visual);

	widget->port_bg = NULL;
	widget->parent = NULL;
	widget->decorations = NULL;
	widget->draw_pos = gfx_point(0, 0);
	widget->gray_text = 0;
	widget->color = fgcolor;
	widget->bgcolor = bgcolor;
	widget->font_nr = visual->font_nr;
	widget->ID = _visual_find_free_ID(visual);
	visual->port_refs[widget->ID] = widget;

	_gfxw_set_container_ops((gfxw_container_t *) widget,
				_gfxwop_port_draw,
				_gfxwop_port_free,
				_gfxwop_container_tag,
				_gfxwop_port_print,
				_gfxwop_basic_compare_to,
				_gfxwop_basic_equals,
				_gfxwop_port_superarea_of,
				_gfxwop_port_set_visual,
				_gfxwop_container_free_tagged,
				_gfxwop_container_free_contents,
				_gfxwop_port_add_dirty,
				_gfxwop_container_add);

	return widget;
}


gfxw_port_t *
gfxw_remove_port(gfxw_visual_t *visual, gfxw_port_t *port)
{
	int ID;
	gfxw_port_t *parent;
	VERIFY_WIDGET(visual);
	VERIFY_WIDGET(port);

	if (!visual->contents) {
		GFXWARN("Attempt to remove port from empty visual\n");
		return NULL;
	}

	ID = port->ID;

	parent = (gfxw_port_t *) port->parent;
	port->free(GFXW(port));

	while (parent && !GFXW_IS_PORT(parent))
		parent = (gfxw_port_t *) parent->parent; /* Ascend through ancestors */

	if (ID < 0 || ID >= visual->port_refs_nr) {
		GFXWARN("Attempt to free port #%d; allowed: [0..%d]!\n", ID, visual->port_refs_nr);
		return parent;
	}

	if (visual->port_refs[ID] != port) {
		GFXWARN("While freeing port %d: Port is at %p, but port list indicates %p!\n",
			ID, port, visual->port_refs[ID]);
	} else visual->port_refs[ID] = NULL;

	return parent;
}


gfxw_port_t *
gfxw_find_port(gfxw_visual_t *visual, int ID)
{
	if (ID < 0 || ID >= visual->port_refs_nr)
		return NULL;

	return visual->port_refs[ID];
}


/*** - other functions - ***/

gfxw_widget_t *
gfxw_set_id(gfxw_widget_t *widget, int ID)
{
	widget->ID = ID;
	return widget;

}