/***************************************************************************
 kernel.c Copyright (C) 1999 Christoph Reichenbach


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

    Christoph Reichenbach (CJR) [jameson@linuxgames.com]

***************************************************************************/

#include <engine.h>


sci_kernel_function_t kfunct_mappers[] = {
  {"Load", kLoad},
  {"UnLoad", kUnLoad},
  {"GameIsRestarting", kGameIsRestarting },
  {"NewList", kNewList },
  {"GetSaveDir", kGetSaveDir },
  {"GetCWD", kGetCWD },
  {"SetCursor", kSetCursor },
  {"FindKey", kFindKey },
  {"NewNode", kNewNode },
  {"AddToFront", kAddToFront },
  {"AddToEnd", kAddToEnd },
  {"Show", kShow },
  {"PicNotValid", kPicNotValid },
  {"Random", kRandom },
  {"Abs", kAbs },
  {"Sqrt", kSqrt },
  {"OnControl", kOnControl },
  {"HaveMouse", kHaveMouse },
  {"GetAngle", kGetAngle },
  {"GetDistance", kGetDistance },
  {"LastNode", kLastNode },
  {"FirstNode", kFirstNode },
  {"NextNode", kNextNode },
  {"PrevNode", kPrevNode },
  {"NodeValue", kNodeValue },
  {"Clone", kClone },
  {"DisposeClone", kDisposeClone },
  {"ScriptID", kScriptID },
  {"MemoryInfo", kMemoryInfo },
  {"DrawPic", kDrawPic },
  {"DisposeList", kDisposeList },
  {"DisposeScript", kDisposeScript },
  {"GetPort", kGetPort },
  {"SetPort", kSetPort },
  {"NewWindow", kNewWindow },
  {"DisposeWindow", kDisposeWindow },
  {"IsObject", kIsObject },
  {"Format", kFormat },
  {"DrawStatus", kDrawStatus },
  {"DrawMenuBar", kDrawMenuBar },
  {"AddMenu", kAddMenu },
  {"SetMenu", kSetMenu },
  {"AddToPic", kAddToPic },
  {"CelWide", kCelWide },
  {"CelHigh", kCelHigh },
  {"Display", kDisplay },
  {"Animate", kAnimate },
  {"GetTime", kGetTime },
  {"DeleteKey", kDeleteKey },
  {"StrLen", kStrLen },
  {"GetFarText", kGetFarText },
  {"StrEnd", kStrEnd },
  {"StrCat", kStrCat },
  {"StrCmp", kStrCmp },
  {"StrCpy", kStrCpy },
  {"StrAt", kStrAt },
  {"ReadNumber", kReadNumber },
  {"DrawControl", kDrawControl },
  {"NumCels", kNumCels },
  {"NumLoops", kNumLoops },
  {"TextSize", kTextSize },
  {"InitBresen", kInitBresen },
  {"DoBresen", kDoBresen },
  {"CanBeHere", kCanBeHere },
  {"DrawCel", kDrawCel },
  {"DirLoop", kDirLoop },
  {"CoordPri", kCoordPri },
  {"PriCoord", kPriCoord },
  {"ValidPath", kValidPath },
  {"RespondsTo", kRespondsTo },
  {"FOpen", kFOpen },
  {"FPuts", kFPuts },
  {"FGets", kFGets },
  {"FClose", kFClose },
  {"TimesSin", kTimesSin },
  {"SinMult", kTimesSin },
  {"TimesCos", kTimesCos },
  {"CosMult", kTimesCos },
  {"MapKeyToDir", kMapKeyToDir },
  {"GlobalToLocal", kGlobalToLocal },
  {"LocalToGlobal", kLocalToGlobal },
  {"Wait", kWait },
  {"CosDiv", kCosDiv },
  {"SinDiv", kSinDiv },
  {"BaseSetter", kBaseSetter },
  {"Parse", kParse },
  {"ShakeScreen", kShakeScreen },
#ifdef _WIN32
  {"DeviceInfo", kDeviceInfo_Win32},
#else /* !_WIN32 */
  {"DeviceInfo", kDeviceInfo_Unix},
#endif
  {"HiliteControl", kHiliteControl},
  {"GetMenu", kGetMenu},
  {"MenuSelect", kMenuSelect},
  {"GetEvent", kGetEvent },
  {"CheckFreeSpace", kCheckFreeSpace },
  {"DoSound", kDoSound },
  {"SetSynonyms", kSetSynonyms },
  {"FlushResources", kFlushResources },
  {"SetDebug", kSetDebug },
  {"GetSaveFiles", kGetSaveFiles },
  {"CheckSaveGame", kCheckSaveGame },
  {"SaveGame", kSaveGame },
  {"RestoreGame", kRestoreGame },
  {"SetJump", kSetJump },
  {"EditControl", kEditControl },
  {"EmptyList", kEmptyList },
  {"AddAfter", kAppendAfter },

  /* Experimental functions */
  {"RestartGame", kRestartGame },
  {"SetNowSeen", kSetNowSeen },
  {"Said", kSaid },
  {"Graph", kGraph },

  /* Special and NOP stuff */
  {"DoAvoider", kNOP },
  {SCRIPT_UNKNOWN_FUNCTION_STRING, k_Unknown },
  {0,0} /* Terminator */
};



/******************** Kernel Oops ********************/

int
kernel_oops(state_t *s, char *file, int line, char *reason)
{
  sciprintf("Kernel Oops in file %s, line %d: %s\n", file, line, reason);
  fprintf(stderr,"Kernel Oops in file %s, line %d: %s\n", file, line, reason);
  script_debug_flag = script_error_flag = 1;
  return 0;
}


/* Allocates a set amount of memory for a specified use and returns a handle to it. */
int
kalloc(state_t *s, int type, int space)
{
  int seeker = 0;

  while ((seeker < MAX_HUNK_BLOCKS) && (s->hunk[seeker].size))
    seeker++;

  if (seeker == MAX_HUNK_BLOCKS)
    KERNEL_OOPS("Out of hunk handles! Try increasing MAX_HUNK_BLOCKS in engine.h");
  else {
    s->hunk[seeker].data = g_malloc(s->hunk[seeker].size = space);
    s->hunk[seeker].type = type;
  }

  SCIkdebug(SCIkMEM, "Allocated %d at hunk %04x\n", space, seeker | (sci_memory << 11));

  return (seeker | (sci_memory << 11));
}


/* Returns a pointer to the memory indicated by the specified handle */
byte *
kmem(state_t *s, int handle)
{
  if ((handle >> 11) != sci_memory) {
    SCIkwarn(SCIkERROR, "Error: kmem() without a handle (%04x)\n", handle);
    return 0;
  }

  handle &= 0x7ff;

  if ((handle < 0) || (handle >= MAX_HUNK_BLOCKS)) {
    SCIkwarn(SCIkERROR, "Error: kmem() with invalid handle\n");
    return 0;
  }

  return s->hunk[handle & 0x7ff].data;
}

/* Frees the specified handle. Returns 0 on success, 1 otherwise. */
int
kfree(state_t *s, int handle)
{
  if ((handle >> 11) != sci_memory) {
    SCIkwarn(SCIkERROR, "Error: Attempt to kfree() non-handle\n");
    return 1;
  }

  SCIkdebug(SCIkMEM, "Freeing hunk %04x\n", handle);

  handle &= 0x7ff;

  if ((handle < 0) || (handle >= MAX_HUNK_BLOCKS)) {
    SCIkwarn(SCIkERROR, "Error: Attempt to kfree() with invalid handle\n");
    return 1;
  }

  if (s->hunk[handle].size == 0) {
    SCIkwarn(SCIkERROR, "Error: Attempt to kfree() non-allocated memory\n");
    return 1;
  }

  g_free(s->hunk[handle].data);
  s->hunk[handle].size = 0;

  return 0;
}


/*****************************************/
/************* Kernel functions **********/
/*****************************************/

void
kRestartGame(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  s->restarting_flags |= SCI_GAME_IS_RESTARTING_NOW;
  s->restarting_flags &= ~SCI_GAME_WAS_RESTARTED_AT_LEAST_ONCE; /* This appears to help */
  script_abort_flag = 1; /* Force vm to abort ASAP */
}


/* kGameIsRestarting():
** Returns the restarting_flag in acc
*/
void
kGameIsRestarting(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  CHECK_THIS_KERNEL_FUNCTION;
  s->acc = (s->restarting_flags & SCI_GAME_WAS_RESTARTED);

  if (argc) {/* Only happens during replay */
    if (!PARAM(0)) /* Set restarting flag */
      s->restarting_flags &= ~SCI_GAME_WAS_RESTARTED;
  }
}

void
kHaveMouse(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  CHECK_THIS_KERNEL_FUNCTION;

  s->acc = s->have_mouse_flag;
}



void
kMemoryInfo(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  switch (PARAM(0)) {
  case 0: s->acc = heap_meminfo(s->_heap); break;
  case 1: s->acc = heap_largest(s->_heap); break;
  default: SCIkwarn(SCIkWARNING, "Unknown MemoryInfo operation: %04x\n", PARAM(0));
  }
}


void
k_Unknown(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  switch (funct_nr) {
  case 0x70: kGraph(s, funct_nr, argc, argp); break;
  default: {
    CHECK_THIS_KERNEL_FUNCTION;
    SCIkdebug(SCIkSTUB, "Unhandled Unknown function %04x\n", funct_nr);
  }
  }
}


void
kFlushResources(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  /* Nothing to do */
}

void
kSetDebug(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  sciprintf("Debug mode activated\n");

  script_debug_flag = 1; /* Enter debug mode */
  _debug_seeking = _debug_step_running = 0;
}

void
kGetTime(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  struct tm* loc_time;
  GTimeVal time_prec;
  time_t the_time;

  if (argc) { /* Get seconds since last am/pm switch */
    the_time = time(NULL);
    loc_time = localtime(&the_time);
    s->acc = loc_time->tm_sec + loc_time->tm_min * 60 + (loc_time->tm_hour % 12) * 3600;
  } else { /* Get time since game started */
    g_get_current_time (&time_prec);
    s-> acc = ((time_prec.tv_usec - s->game_start_time.tv_usec) * 60 / 1000000) +
      (time_prec.tv_sec - s->game_start_time.tv_sec) * 60;
  }
}


void
kstub(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  int i;

  SCIkwarn(SCIkWARNING, "Unimpl'd syscall: %s[%x](", s->kernel_names[funct_nr], funct_nr);

  for (i = 0; i < argc; i++) {
    sciprintf("%04x", 0xffff & PARAM(i));
    if (i+1 < argc) sciprintf(", ");
  }
  sciprintf(")\n");
}


void
kNOP(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
  CHECK_THIS_KERNEL_FUNCTION;
  SCIkwarn(SCIkWARNING, "Warning: Kernel function 0x%02x invoked: NOP\n");
}


void
script_map_kernel(state_t *s)
{
  int functnr;
  int mapped = 0;

  s->kfunct_table = g_malloc(sizeof(kfunct *) * (s->kernel_names_nr + 1));

  for (functnr = 0; functnr < s->kernel_names_nr; functnr++) {
    int seeker, found = -1;

    for (seeker = 0; (found == -1) && kfunct_mappers[seeker].functname; seeker++)
      if (strcmp(kfunct_mappers[seeker].functname, s->kernel_names[functnr]) == 0) {
	found = seeker; /* Found a kernel function with the same name! */
	mapped++;
      }

    if (found == -1) {

      sciprintf("Warning: Kernel function %s[%x] unmapped\n", s->kernel_names[functnr], functnr);
      s->kfunct_table[functnr] = kstub;

    } else s->kfunct_table[functnr] = kfunct_mappers[found].kernel_function;

  } /* for all functions requesting to be mapped */

  sciprintf("Mapped %d of %d kernel functions.\n", mapped, s->kernel_names_nr);

}