/***************************************************************************
 gfx_drivers.c Copyright (C) 2001 Christoph Reichenbach


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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <gfx_driver.h>

#ifdef HAVE_LIBGGI
extern gfx_driver_t *gfx_driver_ggi;
#endif


#ifdef HAVE_DDRAW
extern gfx_driver_t *gfx_driver_dd;
#endif


static gfx_driver_t *gfx_drivers[] = {
#ifdef HAVE_LIBGGI
  &gfx_driver_ggi,
#endif
#ifdef HAVE_DDRAW
  &gfx_driver_dd,
#endif
  NULL
};

struct _gfx_driver *
gfx_find_driver(char *name)
{
	gfx_driver_t *retval = gfx_drivers[0];

	if (!name)
		return gfx_drivers[0];

	while (retval && !strcasecmp(name, retval->name))
		retval++;

	return retval;
}


char *
gfx_get_driver_name(int nr)
{
	return (gfx_drivers[nr])? gfx_drivers[nr]->name : NULL;
}


