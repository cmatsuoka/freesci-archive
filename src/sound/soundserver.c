/***************************************************************************
 soundserver.c Copyright (C) 1999 Christoph Reichenbach, TU Darmstadt

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


void
dump_song_pos(int i, song_t *song)
{
	char marks[2][2] = {" .", "|X"};
	char mark = marks[i == song->loopmark][i == song->pos];

	if ((i & 0xf) == 0)
		fprintf(stderr, " %04x:\t", i);
	fprintf(stderr, "%c%02x%c", mark, song->data[i], mark);
	if ((i & 0xf) == 0x7)
		fprintf(stderr,"--");
	if ((i & 0xf) == 0x3 || (i & 0xf) == 0xb)
		fprintf(stderr," ");
	if ((i & 0xf) == 0xf)
		fprintf(stderr,"\n");
}

void
dump_song(song_t *song)
{
	char *stati[] = {"stopped", "playing", "suspended", "waiting"};

	fprintf(stderr, "song: ");
	if (!song)
		fprintf(stderr, "NULL");
	else {
		int i;

		fprintf(stderr, "size=%d, pos=0x%x, loopmark=0x%x, loops_left=%d\n",
			song->size, song->pos, song->loopmark, song->loops);
		fprintf(stderr, " Current status: %d (%s), Handle is: 0x%04x\n",
			song->status,
			(song->status >= 0 && song->status < 4)? stati[song->status] : "INVALID",
			song->handle);
		fprintf(stderr, "Mark descriptions: '.': Pos, '|': Loop mark, 'X': both\n");

		for (i = 0; i < song->size; i++)
			dump_song_pos(i, song);
		fprintf(stderr,"\n");
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

	if (action == SOUND_COMMAND_IMAP_SET_INSTRUMENT) {
			if ((value >= (0) && value <= (MIDI_mappings_nr - 1))
			    || value == NOMAP
			    || value == RHYTHM)
				MIDI_mapping[instr].gm_instr = (char)value;
			else
				fprintf(debug_stream, "Invalid instrument ID: %d\n", value);

	} else if (action == SOUND_COMMAND_IMAP_SET_KEYSHIFT) {
			SOUNDSERVER_IMAP_CHANGE(keyshift, -128, 127,
				"key shift");

	} else if (action == SOUND_COMMAND_IMAP_SET_FINETUNE) {
			SOUNDSERVER_IMAP_CHANGE(finetune, -32768, 32767,
			    "finetune value");

	} else if (action == SOUND_COMMAND_IMAP_SET_BENDER_RANGE) {
			SOUNDSERVER_IMAP_CHANGE(bender_range, -128, 127,
			    "bender range");

	} else if (action == SOUND_COMMAND_IMAP_SET_PERCUSSION) {
			SOUNDSERVER_IMAP_CHANGE(gm_rhythmkey, 0, 79,
			    "percussion instrument");

	} else if (action == SOUND_COMMAND_IMAP_SET_VOLUME) {
			SOUNDSERVER_IMAP_CHANGE(volume, 0, 100,
			    "instrument volume");

	} else {
			fprintf(debug_stream, "imap_set(): Internal error: "
				"Invalid action %d!\n", action);
	}
}

void
change_song(song_t *new_song, sound_server_state_t *ss_state)
{
	if (new_song)
	{
		guint8 i;
		for (i = 0; i < MIDI_CHANNELS; i++) {
			/* lingering notes are usually intended */
			ss_state->playing_notes[i].polyphony = MAX(1, MIN(POLYPHONY(new_song, i),
				MAX_POLYPHONY));
			ss_state->playing_notes[i].playing = 0;

			if (new_song->instruments[i])
				midi_event2((guint8)(0xc0 | i), new_song->instruments[i]);
		}
	}
	ss_state->current_song = new_song;
	ss_state->current_song->pos = 33;
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

	this_song = song_lib_find(ss_state->songlib, song_handle);
	/* see if it's in the cache */

	if (this_song)
	{
		/* in the cache so remove it */
		int last_mode = song_lib_remove(ss_state->songlib, song_handle);

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
	fprintf(debug_stream, "Playing handle %04x and priority %i\n", song_handle, priority);
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		midi_allstop();
		this_song->status = SOUND_STATUS_PLAYING;

		global_sound_server->queue_event(song_handle, SOUND_SIGNAL_PLAYING, 0);

#ifdef DEBUG_SOUND_SERVER
		/* play this song and reset instrument mappings */
		memset(channel_instrument, -1, sizeof(channel_instrument));
#endif

		change_song(this_song, ss_state);

	} else {
		fprintf(debug_stream, "Attempt to play invalid handle %04x\n", song_handle);
	}
}

void
stop_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Stopping handle %04x\n", song_handle);
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
			this_song->resetflag = 0;
		}

		global_sound_server->queue_event(song_handle, SOUND_SIGNAL_LOOP, -1);

		global_sound_server->queue_event(song_handle, SOUND_SIGNAL_FINISHED, 0);

	} else {
		fprintf(debug_stream, "Attempt to stop invalid handle %04x\n", song_handle);
	}
}

void
suspend_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t **seeker = ss_state->songlib;
	/* used to search for suspended and waiting songs */

	if (song_handle)
	{
		/* search for suspended handle */
		while ((*seeker) && ((*seeker)->status != SOUND_STATUS_SUSPENDED))
			(*seeker) = (*seeker)->next;

		if (*seeker)
		{
			/* found a suspended handle */
			(*seeker)->status = SOUND_STATUS_WAITING;	/* change to paused */
#ifdef DEBUG_SOUND_SERVER
			fprintf(debug_stream, "Unsuspending paused song\n");
#endif

		} else {
			/* search for waiting handles */
			while ((*seeker) && ((*seeker)->status != SOUND_STATUS_WAITING))
			{
				if ((*seeker)->status == SOUND_STATUS_SUSPENDED)
					return;	/* ??? */
				(*seeker) = (*seeker)->next;
			}

			if (*seeker)
			{
				/* found waiting song, so suspending */
				(*seeker)->status = SOUND_STATUS_SUSPENDED;
#ifdef DEBUG_SOUND_SERVER
				fprintf(debug_stream, "Suspending paused song\n");
#endif
			}
		}

	} else {
		fprintf(debug_stream, "Attempt to suspend invalid handle %04x\n", song_handle);
	}
}

void
resume_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Resuming handle %04x\n", song_handle);
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		if (this_song->status == SOUND_STATUS_SUSPENDED)
		{
			/* set this handle as ready and waiting */
			this_song->status = SOUND_STATUS_WAITING;

		} else {
			fprintf(debug_stream, "Attempt to resume handle %04x not suspended\n",
				song_handle);
		}
	}
}

void
renice_handle(int priority, word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Renice handle %04x to priority %i\n", song_handle, priority);
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		/* set new priority and resort song cache accordingly */
		this_song->priority = priority;
		song_lib_resort(ss_state->songlib, this_song);

	} else {
		fprintf(debug_stream, "Attempt to renice invalid handle %04x\n", song_handle);
	}
}

void
fade_handle(int ticks, word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Fade handle %04x in %i ticks\n", song_handle, ticks);
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (song_handle == 0x0000)
	{
		/* applies to current song */
		if (ss_state->current_song)
		{
			ss_state->current_song->fading = ticks;
			ss_state->current_song->maxfade = ticks;

		} else {
			fprintf(debug_stream, "Attempt to fade with no currently playing song\n");
		}

	} else if (this_song) {
		this_song->fading = ticks;
		this_song->maxfade = ticks;

	} else {
		fprintf(debug_stream, "Attempt to fade invalid handle %04x\n", song_handle);
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
		fprintf(debug_stream, "Attempt to set loops on invalid handle %04x\n", song_handle);
	}
}

void
dispose_handle(word song_handle, sound_server_state_t *ss_state)
{
	song_t *this_song;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Disposing handle %04x\n", song_handle);
#endif

	this_song = song_lib_find(ss_state->songlib, song_handle);

	if (this_song)
	{
		int last_mode = song_lib_remove(ss_state->songlib, song_handle);
		if (last_mode == SOUND_STATUS_PLAYING)
		{
			/* force song detection to start with the highest priority song */
			change_song(ss_state->songlib[0], ss_state);

			global_sound_server->queue_event(song_handle, SOUND_SIGNAL_FINISHED, 0);
		}

	} else {
		fprintf(debug_stream, "Attempt to dispose invalid handle %04x\n", song_handle);
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
		fprintf(debug_stream, "Attempt to mute invalid channel %i\n", channel);
	}
}

void
set_master_volume(guint8 new_volume, sound_server_state_t *ss_state)
{
#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Setting master volume to %i\n", new_volume);
#endif

	ss_state->master_volume = (guint8)(new_volume * 100 / 15);	/* scale to % */
	midi_volume(ss_state->master_volume);
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
	song_t **seeker = ss_state->songlib;

#ifdef DEBUG_SOUND_SERVER
	fprintf(debug_stream, "Stopping all songs\n");
#endif

	/* FOREACH seeker IN ss_state->songlib */
	while (*seeker)
	{
		if ( ((*seeker)->status == SOUND_STATUS_WAITING) ||
			 ((*seeker)->status == SOUND_STATUS_PLAYING) )
		{
			(*seeker)->status = SOUND_STATUS_STOPPED;	/* stop song */

			global_sound_server->queue_event((*seeker)->handle, SOUND_SIGNAL_FINISHED, 0);
		}
		(*seeker) = (*seeker)->next;
	}

	change_song(NULL, ss_state);
	midi_allstop();
}

void
suspend_all(sound_server_state_t *ss_state)
{
#if 0
	/* store the time suspended at */
	sci_get_current_time((GTimeVal *)&suspend_time);
#endif
	ss_state->suspended = 1;
}

void
resume_all(sound_server_state_t *ss_state)
{
#if 0
	GTimeVal *resume_time;

	memset(&resume_time, 0, sizeof(GTimeVal));
	sci_get_current_time((GTimeVal *)resume_time);

	/* the following calculations ensure that, after resuming, we wait for the
	 * exact (well, mostly) number of microseconds that we were supposed to
	 * wait for before we were suspended */

	/* modify last_played relative to suspend_time */
	last_played_time.tv_sec += resume_time->tv_sec -
		suspend_time.tv_sec - 1;
	last_played_time.tv_usec += resume_time->tv_usec -
		suspend_time.tv_usec + 1000000;

	/* make sure that 0 <= tv_usec <= 999999 */
	last_played_time.tv_sec -= last_played_time.tv_usec / 1000000;
	last_played_time.tv_usec %= 1000000;
#endif

	ss_state->suspended = 0;
}

void
set_reverse_stereo(int rs, sound_server_state_t *ss_state)
{
	ss_state->reverse_stereo = rs;
}
