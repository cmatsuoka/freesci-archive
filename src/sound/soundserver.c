/***************************************************************************
 soundserver.c Copyright (C) 1999 Christoph Reichenbach, TU Darmstadt
               Copyright (C) 2001,2002 Alexander R Angas

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

#include <sciresource.h>
#include <stdarg.h>
#include <engine.h>
#include <sound.h>
#include <scitypes.h>
#include <soundserver.h>
#include <midi_device.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOUNDCARD_H
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <sys/ioctl.h>
#  include <sys/soundcard.h>
#endif

sound_server_t *global_sound_server = NULL;

int soundserver_dead;	/* if sound server is dead */

int do_sound(sound_server_state_t *sss, int buff_ss)
{
	static midi_op_t cached_cmd;
	/* cached MIDI command stored while waiting for ticks to == 0 */

	/* if sound suspended do nothing */
	if (sss->suspended)
		return 0;

	/* find the active song */
	sss->current_song = song_lib_find_active(sss->songlib, sss->current_song);

	if (sss->current_song && (sss->current_song->status == SOUND_STATUS_PLAYING))	/* have an active song */
	{
		midi_op_t this_cmd;		/* current command (may come from cache) */

		/* check if song has faded out */
		if (sss->current_song->fading == 0)
		{
#ifdef DEBUG_SOUND_SERVER
		fprintf(debug_stream, "Song %04x faded out\n", sss->current_song->handle);
#endif
			/* reset song position */
			sss->current_song->resetflag = 1;

			/* stop this song */
			stop_handle((word)sss->current_song->handle, sss);
			global_sound_server->queue_event(
				sss->current_song->handle, SOUND_SIGNAL_LOOP, -1);

		} else if (sss->current_song->fading > 0) {
			/* fade by one more tick */
			sss->current_song->fading = sss->current_song->fading - 1;
		}

		/* see if there is a MIDI command waiting to be played */
		if (cached_cmd.midi_cmd != 0)
		{
			if (cached_cmd.delta_time > 0)
			{
				cached_cmd.delta_time--;	/* maybe next time around! */
				return 1;

			} else {
				/* the tick is now so build midi cmd from cache */
#ifdef DEBUG_SOUND_SERVER
				this_cmd.pos_in_song = cached_cmd.pos_in_song;
#endif
				this_cmd.delta_time = 0;
				this_cmd.midi_cmd = cached_cmd.midi_cmd;
				this_cmd.param1 = cached_cmd.param1;
				this_cmd.param2 = cached_cmd.param2;
				cached_cmd.midi_cmd = 0;
			}

		} else if (sss->current_song->data)	{
			/* no waiting MIDI command so if this song has data, process it */

			static unsigned char last_cmd;
			unsigned char msg_type;	/* type of command */

			/* retrieve MIDI command */
			this_cmd.delta_time = 0;

			if (sss->current_song->pos >= sss->current_song->size) {
				fprintf(debug_stream, "Sound server: Error: Reached end of song while decoding prefix:\n");
				dump_song(sss->current_song);
				fprintf(debug_stream, "Suspending sound server\n");
				suspend_all(sss);
				return 0;
			}

			while (sss->current_song->data[(sss->current_song->pos)] == SCI_MIDI_TIME_EXPANSION_PREFIX)
			{
				this_cmd.delta_time += SCI_MIDI_TIME_EXPANSION_LENGTH;
				(sss->current_song->pos)++;
			}
			this_cmd.delta_time += sss->current_song->data[sss->current_song->pos];
#if (DEBUG_SOUND_SERVER == 2)
			fprintf(stdout, "delta: %u ", this_cmd.delta_time);
#endif
			(sss->current_song->pos)++;
#ifdef DEBUG_SOUND_SERVER
			this_cmd.pos_in_song = sss->current_song->pos;
#endif
			this_cmd.midi_cmd = sss->current_song->data[sss->current_song->pos];

			if ((this_cmd.midi_cmd >> 4) >= '\x8')	/* this is a command */
			{
				last_cmd = this_cmd.midi_cmd;	/* cache this command */
				(sss->current_song->pos)++;

			} else {	/* this is data */
				/* running status mode - use the last command cached */
				this_cmd.midi_cmd = last_cmd;
			}
#if (DEBUG_SOUND_SERVER == 2)
			fprintf(stdout, " cmd: %02x ", this_cmd.midi_cmd);
#endif

			/* work out the parameters for all types of commands */
			msg_type = (unsigned char)(this_cmd.midi_cmd >> 4);
			if (MIDI_PARAMETERS_TWO(msg_type))
			{
				/* two parameters */
				this_cmd.param1 = sss->current_song->data[sss->current_song->pos];
				this_cmd.param2 = sss->current_song->data[(sss->current_song->pos) + 1];
				(sss->current_song->pos) += 2;	/* lined up for next time around */
#if (DEBUG_SOUND_SERVER == 2)
				fprintf(stdout, " p1: %02x ", this_cmd.param1);
				fprintf(stdout, " p2: %02x ", this_cmd.param2);
#endif

				if (sss->reverse_stereo &&
					(msg_type == MIDI_MSG_TYPE_CONTROLLER_CHANGE) &&
					(this_cmd.param1 == MIDI_CONTROLLER_PAN_COARSE))
					this_cmd.param2 = 0x7f - this_cmd.param2;	/* reverse stereo */

			} else if (MIDI_PARAMETERS_ONE(msg_type)) {
				/* one parameter */
				this_cmd.param1 = sss->current_song->data[sss->current_song->pos];
				this_cmd.param2 = 0;
#if (DEBUG_SOUND_SERVER == 2)
				fprintf(stdout, " p1: %02x ", this_cmd.param1);
#endif
				(sss->current_song->pos)++;

			} else if (msg_type == MIDI_MSG_TYPE_SYSTEM) {
#if (DEBUG_SOUND_SERVER == 2)
				fprintf(stdout, " SYSTEM ");
#endif
				if (this_cmd.midi_cmd <= 0xF6)
					/* all system common messages cancel running status */
					last_cmd = 0;

				/* varying parameters - yay! */

				/* this command is ignored */
				if (this_cmd.midi_cmd == MIDI_SYSTEM_SYSEX)	/* 0xF0 */
				{
					while ((sss->current_song->data[(sss->current_song->pos) + 1] >= 0x80) &&
						   (sss->current_song->data[(sss->current_song->pos) + 1] <= MIDI_SYSTEM_SYSEX_END))
						  (sss->current_song->pos)++;	/* go past any parameters */

				}
				else
				{
					(sss->current_song->pos)++;

					/* MIDI_SYSTEM_SYSEX_END (0xF7) - ignore if appears from
					** nowhere (no parameters)
					**
					**
					** SCI_MIDI_END_OF_TRACK (0xFC) - no parameters
					*/
				}

			} else if (this_cmd.midi_cmd == 0) {
				fprintf(debug_stream, "WARNING: do_sound() ignoring NULL MIDI command\n");
			} else {
				fprintf(debug_stream, "ERROR: do_sound() MIDI command 0x%02X is not recognised\n", this_cmd.midi_cmd);
				exit(-1);
			}

			/* now we have a MIDI command, but is it time for it to be played? */
			if (buff_ss)
			{
				/* yes it is */
			} else {
				if (this_cmd.delta_time > 0)
				{
					/* not yet, make cache */
#ifdef DEBUG_SOUND_SERVER
					cached_cmd.pos_in_song = this_cmd.pos_in_song;
#endif
					cached_cmd.delta_time = this_cmd.delta_time;
					cached_cmd.midi_cmd = this_cmd.midi_cmd;
					cached_cmd.param1 = this_cmd.param1;
					cached_cmd.param2 = this_cmd.param2;
#if (DEBUG_SOUND_SERVER == 2)
					fprintf(stdout, " [cached] ");
#endif
					return 1;
				}
			}

		} else {
			fprintf(debug_stream, "FIXME: do_sound() has handle with no data!\n");
		}

		/* handle 0xF commands */
		if (this_cmd.midi_cmd == SCI_MIDI_END_OF_TRACK)
		{
			if (buff_ss)
				return 0;
			if ((--(sss->current_song->loops) != 0) && sss->current_song->loopmark)
			{
#ifdef DEBUG_SOUND_SERVER
					fprintf(debug_stream, "Looping back from %d to %d on handle %04x\n",
					        sss->current_song->pos, sss->current_song->loopmark, sss->current_song->handle);
#endif
				sss->current_song->pos = sss->current_song->loopmark;
				global_sound_server->queue_event(
					sss->current_song->handle, SOUND_SIGNAL_LOOP, sss->current_song->loops);
			} else {
#ifdef DEBUG_SOUND_SERVER
					fprintf(debug_stream, "Reached end of track for handle %04x\n",
					        sss->current_song->handle);
#endif
				sss->current_song->resetflag = 1;	/* reset song position */
				stop_handle((word)sss->current_song->handle, sss);
				global_sound_server->queue_event(
					sss->current_song->handle, SOUND_SIGNAL_LOOP, -1);
			}
		}
		else
		{
			/* unless the channel is muted, send MIDI command */
			if (!( ((this_cmd.midi_cmd & 0xf0) == MIDI_NOTE_ON) &&
				   sss->mute_channel[this_cmd.midi_cmd & 0xf] ))
			{
				sci_midi_command(debug_stream,
					sss->current_song,
					this_cmd.midi_cmd,
					(unsigned char)this_cmd.param1,
					(unsigned char)this_cmd.param2,
					this_cmd.delta_time,
					&(sss->sound_cue),
					sss->master_volume);
#if (DEBUG_SOUND_SERVER == 2)
					fprintf(stdout, " (playing %02x %02x %02x)", this_cmd.midi_cmd, this_cmd.param1, this_cmd.param2);
#endif
			}
			return 1;
		}
#if (DEBUG_SOUND_SERVER == 2)
		fprintf(stdout, "\n");
#endif
	}
	return 1;
}

#ifdef DEBUG_SOUND_SERVER
void
print_channels_any(int mapped, sound_server_state_t *ss_state)
{
	int i;

	for (i = 0; i < 16; i++)
		if (channel_instrument[i] >= 0) {
			fprintf(debug_stream, "#%d%s:", i+1, ss_state->mute_channel[i]? " [MUTE]":"");
			if (mapped)
				midi_mt32gm_print_instrument(debug_stream, channel_instrument[i]);
			else
				fprintf(debug_stream, " Instrument %d\n", channel_instrument[i]);
		}
}

void
print_song_info(word song_handle, sound_server_state_t *ss_state)
{
	song_t *a_song = song_lib_find(ss_state->songlib, song_handle);

	fprintf(debug_stream, "Info for song handle: ");
	if (a_song)
	{
		fprintf(debug_stream, "%04x\n", a_song->handle);
		fprintf(debug_stream, " size: %i, priority: %i, status: %i\n",
			a_song->size, a_song->priority, a_song->status);
		fprintf(debug_stream, " pos: %i, loopmark: %i, loops: %i\n",
			a_song->pos, a_song->loopmark, a_song->loops);
		fprintf(debug_stream, " fading: %i, maxfade: %i\n",
			a_song->fading, a_song->maxfade);
		fprintf(debug_stream, " reverb: %i, resetflag: %i\n",
			a_song->reverb, a_song->resetflag);

	} else {
		fprintf(debug_stream, "No handle given!\n");
	}
}
#endif


static int
_sound_expect_answer(char *timeoutmessage, int def_value)
{
	int *success;
	int retval;
	int size;

	global_sound_server->get_data((byte **)&success, &size);

	retval = *success;
	free(success);
	return retval;

}

static int
_sound_transmit_text_expect_anwer(state_t *s, char *text, int command, char *timeoutmessage)
{
	int count;

	count = strlen(text) + 1;
	global_sound_server->queue_command(0, command, count);
	if (global_sound_server->send_data((unsigned char *)text, count) != count) {
		fprintf(debug_stream, "_sound_transmit_text_expect_anwer():"
			" sound_send_data returned < count\n");
	}

	return _sound_expect_answer(timeoutmessage, 1);
}

int
sound_save_default(state_t *s, char *dir)
{
	return _sound_transmit_text_expect_anwer(s, dir, SOUND_COMMAND_SAVE_STATE,
					   "Sound server timed out while saving\n");
}

int
sound_restore_default(state_t *s, char *dir)
{
	return _sound_transmit_text_expect_anwer(s, dir, SOUND_COMMAND_RESTORE_STATE,
					   "Sound server timed out while restoring\n");
}

void
dump_song_pos(int i, song_t *song)
{
	char marks[2][2] = {" .", "|X"};
	char mark = marks[i == song->loopmark][i == song->pos];

	if ((i & 0xf) == 0)
		fprintf(debug_stream, " %04x:\t", i);
	fprintf(debug_stream, "%c%02x%c", mark, song->data[i], mark);
	if ((i & 0xf) == 0x7)
		fprintf(debug_stream,"--");
	if ((i & 0xf) == 0x3 || (i & 0xf) == 0xb)
		fprintf(debug_stream," ");
	if ((i & 0xf) == 0xf)
		fprintf(debug_stream,"\n");
}

void
dump_song(song_t *song)
{
	char *stati[] = {"stopped", "playing", "suspended", "waiting"};

	fprintf(debug_stream, "song: ");
	if (!song)
		fprintf(debug_stream, "NULL");
	else {
		int i;

		fprintf(debug_stream, "size=%d, pos=0x%x, loopmark=0x%x, loops_left=%d\n",
			song->size, song->pos, song->loopmark, song->loops);
		fprintf(debug_stream, " Current status: %d (%s), Handle is: 0x%04x\n",
			song->status,
			(song->status >= 0 && song->status < 4)? stati[song->status] : "INVALID",
			song->handle);
		fprintf(debug_stream, "Mark descriptions: '.': Pos, '|': Loop mark, 'X': both\n");

		for (i = 0; i < song->size; i++)
			dump_song_pos(i, song);
		fprintf(debug_stream,"\n");
	}
}


void
_restore_midi_state(sound_server_state_t *ss_state)
{
	/* restore some MIDI device state (e.g. current instruments) */
	if (ss_state->current_song) {
		guint8 i;
		midi_allstop();
		for (i = 0; i < MIDI_CHANNELS; i++) {
			if (ss_state->current_song->instruments[i])
				midi_event2((guint8)(0xc0 | i), (guint8)ss_state->current_song->instruments[i], 0);
		}
		midi_reverb((short)ss_state->current_song->reverb);
	}
}


#define SOUNDSERVER_IMAP_CHANGE(ELEMENT, VAL_MIN, VAL_MAX, ELEMENT_NAME) \
		if (value >= (VAL_MIN) && value <= (VAL_MAX)) \
			MIDI_mapping[instr].ELEMENT = value; \
		else \
			fprintf(debug_stream, "Invalid %s: %d\n", ELEMENT_NAME, value) \

void
imap_set(unsigned int action, int instr, int value)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Changing instrument map - instr %i, action %i, value %i\n", instr, action, value);
#endif

	if (instr < 0 || instr >= MIDI_mappings_nr) {
		fprintf(debug_stream, "imap_set(): Attempt to re-map"
			" invalid instrument %d!\n", instr);
		return;
	}

	switch (action) {

	case SOUND_COMMAND_IMAP_SET_INSTRUMENT:
		if ((value >= (0) && value <= (MIDI_mappings_nr - 1))
		    || value == NOMAP
		    || value == RHYTHM)
			MIDI_mapping[instr].gm_instr = (char)value;
		else
			fprintf(debug_stream,
				"imap_set(): Invalid instrument ID: %d\n", value);
		break;


	case SOUND_COMMAND_IMAP_SET_KEYSHIFT:
		SOUNDSERVER_IMAP_CHANGE(keyshift, -128, 127,
					"key shift");
		break;


	case SOUND_COMMAND_IMAP_SET_FINETUNE:
		SOUNDSERVER_IMAP_CHANGE(finetune, -32768, 32767,
					"finetune value");
		break;


	case SOUND_COMMAND_IMAP_SET_BENDER_RANGE:
		SOUNDSERVER_IMAP_CHANGE(bender_range, -128, 127,
					"bender range");
		break;


	case SOUND_COMMAND_IMAP_SET_PERCUSSION:
		SOUNDSERVER_IMAP_CHANGE(gm_rhythmkey, 0, 79,
					"percussion instrument");
		break;


	case SOUND_COMMAND_IMAP_SET_VOLUME:
		SOUNDSERVER_IMAP_CHANGE(volume, 0, 100,
					"instrument volume");
		break;

	default:
		fprintf(debug_stream,
			"imap_set(): Invalid instrument map sound command: %d\n",
			action);
	}
}

void
change_song(song_t *new_song, sound_server_state_t *ss_state)
{
	guint8 i;
#ifdef DEBUG_SOUND_SERVER
	if (new_song)
		fprintf(debug_stream, "Changing to song handle %04x\n", new_song->handle);
	else
		fprintf(debug_stream, "Changing to NULL song handle\n");
#endif

	if (new_song)
	{
		for (i = 0; i < MIDI_CHANNELS; i++) {
			if (new_song->instruments[i])
				midi_event2((guint8)(MIDI_INSTRUMENT_CHANGE | i),
					(guint8)new_song->instruments[i], 0);
		}

		ss_state->current_song = new_song;
		/* ss_state->current_song->pos = 33; */ /* ??? */

	} else {
		ss_state->current_song = NULL;
	}
}

void
init_handle(int priority, word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;
	/* pointer to song for this instruction */

	byte *data;
	/* temporary for storing pointer to new song's data */

	int data_size = 1;
	/* temporary for storing size of data */

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Initialising song with handle %04x and priority %i\n", song_handle, priority);
#endif

	if (!song_handle)
		fprintf(debug_stream, "init_handle(): Warning: Attempt to initialise NULL sound handle\n");

	/* see if it's in the cache */
	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		/* in the cache so remove it */
		int last_mode = song_lib_remove(ss_state->songlib, song_handle, ss_state->resmgr);

		/* if just retrieved the current song, reset the queue for
		 * song_lib_find_active() */
		if (last_mode == SOUND_STATUS_PLAYING)
		{
			global_sound_server->queue_event(song_handle, SOUND_SIGNAL_FINISHED, 0);

			/* play highest priority song */
			change_song(ss_state->songlib[0], ss_state);
		}

		/* usually same case as above but not always */
		if (this_song == ss_state->current_song)
		{
			/* reset the song (just been freed) so we don't try to read from
			 * it later */
			ss_state->current_song = NULL;
		}
	}

	global_sound_server->get_data(&data, &data_size);
	this_song = song_new(song_handle, data, data_size, priority);	/* Create new song */

	/* enable rhythm channel if requested by the hardware */
	if (midi_playrhythm)
		this_song->flags[RHYTHM_CHANNEL] |= midi_playflag;

	song_lib_add(ss_state->songlib, this_song);	/* add to song library */

	ss_state->sound_cue = 127;	/* reset ss_state->sound_cue */

	global_sound_server->queue_event(song_handle, SOUND_SIGNAL_INITIALIZED, 0);
}

void
play_handle(int priority, word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Playing handle %04x with priority %i\n", song_handle, priority);
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		midi_allstop();

#ifdef DEBUG_SOUND_SERVER
		/* play this song and reset instrument mappings */
		memset(channel_instrument, -1, sizeof(channel_instrument));
#endif

		change_song(this_song, ss_state);

		this_song->status = SOUND_STATUS_PLAYING;
		global_sound_server->queue_event(song_handle, SOUND_SIGNAL_PLAYING, 0);

	} else {
		fprintf(debug_stream, "play_handle(): Attempt to play invalid sound handle %04x\n", song_handle);
	}
}

void
loop_handle(int loops, word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Set loops for handle %04x to %i\n", song_handle, loops);
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		this_song->loops = loops;

	} else {
		fprintf(debug_stream, "loop_handle(): Attempt to set loops on invalid sound handle %04x\n", song_handle);
	}
}

void
fade_handle(int ticks, word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Fade handle %04x in %i ticks", song_handle, ticks);
	if (ss_state->current_song)
		fprintf(debug_stream, " (currently playing is %04x)\n", ss_state->current_song->handle);
	else
		fprintf(debug_stream, " (no song currently playing)\n");
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (song_handle == 0x0000)
	{
		/* applies to current song */
		if (ss_state->current_song)
		{
			ss_state->current_song->fading = ticks;
			ss_state->current_song->maxfade = ticks;

#ifdef DEBUG_SOUND_SERVER
		} else {
			fprintf(debug_stream, "Attempt to fade with no currently playing song\n");
#endif
		}

	} else if (this_song) {
		this_song->fading = ticks;
		/* this_song->maxfade = ticks; */ /* ??? */

	} else {
		fprintf(debug_stream, "fade_handle(): Attempt to fade invalid sound handle %04x\n", song_handle);
	}
}

void
stop_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Stopping handle %04x", song_handle);
	if (ss_state->current_song)
		fprintf(debug_stream, " (currently playing is %04x)\n", ss_state->current_song->handle);
	else
		fprintf(debug_stream, " (no song currently playing)\n");
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		midi_allstop();
		this_song->status = SOUND_STATUS_STOPPED;

		if (this_song->resetflag)
		{
			/* reset song position */
			this_song->pos = 33;
			this_song->loopmark = 33;
			this_song->resetflag = 0;	/* ??? */
		}

		/* global_sound_server->queue_event(song_handle, SOUND_SIGNAL_LOOP, -1); */
		global_sound_server->queue_event(song_handle, SOUND_SIGNAL_FINISHED, 0);

	} else {
		fprintf(debug_stream, "stop_handle(): Attempt to stop invalid sound handle %04x\n", song_handle);
	}
}

void
suspend_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t *seeker = *ss_state->songlib;
	/* used to search for suspended and waiting songs */

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Suspending handle %04x", song_handle);
	if (ss_state->current_song)
		fprintf(debug_stream, " (currently playing is %04x)\n", ss_state->current_song->handle);
	else
		fprintf(debug_stream, " (no song currently playing)\n");
#endif

	if (song_handle)
	{
		/* search for suspended handle */
		while (seeker && (seeker->status != SOUND_STATUS_SUSPENDED))
			seeker = seeker->next;

		if (seeker)
		{
			/* found a suspended handle */
			seeker->status = SOUND_STATUS_WAITING;	/* change to paused */
#ifdef DEBUG_SOUND_SERVER
			fprintf(debug_stream, "Unsuspending paused song\n");
#endif

		} else {
			/* search for waiting handles */
			while (seeker && (seeker->status != SOUND_STATUS_WAITING))
			{
				if (seeker->status == SOUND_STATUS_SUSPENDED)
					return;	/* ??? */
				seeker = seeker->next;
			}

			if (seeker)
			{
				/* found waiting song, so suspending */
				seeker->status = SOUND_STATUS_SUSPENDED;
#ifdef DEBUG_SOUND_SERVER
				fprintf(debug_stream, "Suspending paused song\n");
#endif
			}
		}
	}	/* else ignore */
}

void
resume_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Resuming handle %04x", song_handle);
	if (ss_state->current_song)
		fprintf(debug_stream, " (currently playing is %04x)\n", ss_state->current_song->handle);
	else
		fprintf(debug_stream, " (no song currently playing)\n");
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		if (this_song->status == SOUND_STATUS_SUSPENDED)
		{
			/* set this handle as ready and waiting */
			this_song->status = SOUND_STATUS_WAITING;
			global_sound_server->queue_event(song_handle, SOUND_SIGNAL_RESUMED, 0);

		} else {
			fprintf(debug_stream, "Attempt to resume handle %04x not suspended\n",
				song_handle);
		}
#ifdef DEBUG_SOUND_SERVER
	} else {
		fprintf(debug_stream, "resume_handle(): Attempt to resume invalid sound handle %04x\n", song_handle);
#endif
	}
}

void
renice_handle(int priority, word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Renice handle %04x to priority %i", song_handle);
	if (ss_state->current_song)
		fprintf(debug_stream, " (currently playing is %04x)\n", ss_state->current_song->handle);
	else
		fprintf(debug_stream, " (no song currently playing)\n");
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		/* set new priority and resort song cache accordingly */
		this_song->priority = priority;
		song_lib_resort(ss_state->songlib, this_song);

	} else {
		fprintf(debug_stream, "renice_handle(): Attempt to renice invalid sound handle %04x\n", song_handle);
	}
}

void
dispose_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Disposing handle %04x", song_handle);
	if (ss_state->current_song)
		fprintf(debug_stream, " (currently playing is %04x)\n", ss_state->current_song->handle);
	else
		fprintf(debug_stream, " (no song currently playing)\n");
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		int last_mode = song_lib_remove(ss_state->songlib, song_handle, ss_state->resmgr);
		if (last_mode == SOUND_STATUS_PLAYING)
		{
			/* force song detection to start with the highest priority song */
			change_song(ss_state->songlib[0], ss_state);

			global_sound_server->queue_event(song_handle, SOUND_SIGNAL_FINISHED, 0);
		}

	} else {
		fprintf(debug_stream, "dispose_handle(): Attempt to dispose invalid sound handle %04x\n", song_handle);
	}
}

void
set_channel_mute(int channel, unsigned char mute_setting, sound_server_state_t *ss_state)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Setting mute of channel %i to %i\n", channel, mute_setting);
#endif

	if ( (channel >= 0) && (channel < 16) )
	{
		ss_state->mute_channel[channel] = mute_setting;

	} else {
		fprintf(debug_stream, "set_channel_mute(): Attempt to mute invalid sound channel %i\n", channel);
	}
}

void
set_master_volume(guint8 new_volume, sound_server_state_t *ss_state)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Setting master volume to %i ", new_volume);
#endif

	ss_state->master_volume = (guint8)((float)new_volume * 100.0 / 15.0);	/* scale to % */

	if (midi_device->volume)
	  midi_volume((guint8)ss_state->master_volume);

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, " (%u%%)\n", ss_state->master_volume);
#endif
}

void
sound_check(int mid_polyphony, sound_server_state_t *ss_state)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Received test signal, responding\n");
#endif

	global_sound_server->send_data((byte *) &mid_polyphony, sizeof(int));
}

void
stop_all(sound_server_state_t *ss_state)
{
	song_t *seeker = *ss_state->songlib;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Stopping all songs\n");
#endif

	/* FOREACH seeker IN ss_state->songlib */
	while (seeker)
	{
		if ( (seeker->status == SOUND_STATUS_WAITING) ||
			 (seeker->status == SOUND_STATUS_PLAYING) )
		{
			seeker->status = SOUND_STATUS_STOPPED;	/* stop song */

			global_sound_server->queue_event(seeker->handle, SOUND_SIGNAL_FINISHED, 0);
		}
		seeker = seeker->next;
	}

	change_song(NULL, ss_state);
	midi_allstop();
}

void
suspend_all(sound_server_state_t *ss_state)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "All sound suspended\n");
#endif
	ss_state->suspended = 1;
}

void
resume_all(sound_server_state_t *ss_state)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "All sound resumed\n");
#endif
	ss_state->suspended = 0;
}

void
set_reverse_stereo(int rs, sound_server_state_t *ss_state)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Set reverse stereo to %i\n", rs);
#endif
	ss_state->reverse_stereo = rs;
}

void
save_sound_state(sound_server_state_t *ss_state)
{
	char *dirname;
	int size;
	int success;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Saving sound state\n");
#endif

	/* Retreive target directory from command stream.
	** Since we might be in a separate process, cwd must be set
	** separately from the main process. That's why we need it here.
	*/
	global_sound_server->get_data((byte **)&dirname, &size);
	success = soundsrv_save_state(debug_stream,
		global_sound_server->flags & SOUNDSERVER_FLAG_SEPARATE_CWD ? dirname : NULL,
		ss_state);

	/* Return soundsrv_save_state()'s return value */
	global_sound_server->send_data((byte *)&success, sizeof(int));
	sci_free(dirname);
}

void
restore_sound_state(sound_server_state_t *ss_state)
{
	char *dirname;
	int len;
	int success;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Restoring sound state\n");
#endif

	change_song(NULL, ss_state);
	global_sound_server->get_data((byte **)&dirname, &len);
	midi_allstop();

	success = soundsrv_restore_state(debug_stream,
		global_sound_server->flags & SOUNDSERVER_FLAG_SEPARATE_CWD ? dirname : NULL,
		ss_state);

	/* Return return value */
	global_sound_server->send_data((byte *)&success, sizeof(int));
	sci_free(dirname);

	_restore_midi_state(ss_state);

}
