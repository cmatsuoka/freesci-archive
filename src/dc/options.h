/***************************************************************************
 options.h Copyright (C) 2003 Walter van Niftrik


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

#ifndef __OPTIONS_H
#define __OPTIONS_H

struct dc_option_t {
	char *name;	/* Option name */
	char *values;	/* Option values, each followed by `\0' */
	char def;	/* Default option */
};

#define NUM_DC_OPTIONS 1

struct dc_option_t dc_options[NUM_DC_OPTIONS] = {
/* 0 */		{ "Graphics Mode", "Frame Buffer\0PVR\0", 0 },
};

#endif  /* __OPTIONS_H */