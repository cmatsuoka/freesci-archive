/***************************************************************************
 input.c (C) 1999 Christoph Reichenbach, TU Darmstadt


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

    Christoph Reichenbach (CJR) [creichen@rbg.informatik.tu-darmstadt.de]

***************************************************************************/

#include <uinput.h>
#include <engine.h>

long sci_clock_time = SCI_INPUT_DEFAULT_CLOCKTIME;
long sci_redraw_time = SCI_INPUT_DEFAULT_REDRAWTIME;


sci_event_t getEvent (state_t *s)
{
  sci_event_t loop;
  loop.type = SCI_EVT_CLOCK;

  if (s->gfx_driver->GetEvent)
    return s->gfx_driver->GetEvent(s);
  else {
    fprintf(stderr,"SCI Input:Warning: No input handler active!\n");
    return loop; /* Not much of a replacement... */
  }
}

