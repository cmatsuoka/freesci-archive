/***************************************************************************
 buff_ss_win32.c Copyright (C) 2002 Alexander R Angas

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

    Alexander R Angas (Alex Angas) <wgd@internode.on.net>

 History:

   20020103 - Based on event_ss_win32.c.
                -- Alex Angas

***************************************************************************/

#ifdef _WIN32

#include <sci_memory.h>
#include <soundserver.h>
#include <sound.h>

#include <engine.h>
#include <windows.h>
#include <win32/messages.h>

/* #define SSWIN_DEBUG 1 */

sound_server_t sound_server_win32b;
#define MAIN_CLASS_NAME "FreeSCI Main Receiving from Buffered SS"
#define SOUND_CLASS_NAME "Buffered SS Receiving from FreeSCI Main"
#define WINDOW_SUFFIX " (Window)"

/* Event map */
static unsigned int emap[NUMBER_SOUND_EVENTS];

static HWND main_wnd, sound_wnd;
static HINSTANCE hi_main, hi_sound;
static HANDLE child_thread;
static HANDLE sound_data_event, thread_created_event;

static state_t *gs;

static sci_queue_t data_queue;
static CRITICAL_SECTION dq_cs;	/* synchronisation for data queue */

static sound_event_t *new_command_event;


LRESULT CALLBACK
BuffMainWndProc (HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
	/* does nothing */
#if (SSWIN_DEBUG == 2)
	fprintf(debug_stream, "BuffMainWndProc called for hWnd %i with msg %i (TID: %i)\n", hWnd, nMsg, GetCurrentThreadId());
#endif

	return DefWindowProc (hWnd, nMsg, wParam, lParam);
}

LRESULT CALLBACK
BuffSoundWndProc (HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
	/* does nothing */
#if (SSWIN_DEBUG == 2)
	fprintf(debug_stream, "BuffSoundWndProc called for hWnd %i with msg %i (TID: %i)\n", hWnd, nMsg, GetCurrentThreadId());
#endif

	return DefWindowProc (hWnd, nMsg, wParam, lParam);
}

/* function called when sound server child thread begins */
DWORD WINAPI
win32buff_soundserver_init(LPVOID lpP)
{
	WNDCLASS wc;
	sound_server_state_t sss;
	memset(&sss, 0, sizeof(sound_server_state_t));

#if (SSWIN_DEBUG == 2)
	fprintf(debug_stream, "win32buff_soundserver_init() begins with TID %i\n", GetCurrentThreadId());
#endif

	/*** register window class for messages to sound server ***/
	memset(&wc, 0, sizeof(WNDCLASS));
	wc.lpfnWndProc = BuffSoundWndProc;
	hi_sound = GetModuleHandle(NULL);
	if (hi_sound == NULL)
	{
		fprintf(debug_stream, "win32buff_soundserver_init(): GetModuleHandle() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}
	wc.hInstance = hi_sound;
	wc.lpszClassName = SOUND_CLASS_NAME;

	if (RegisterClass(&wc) == 0)
	{
		fprintf(debug_stream, "win32buff_soundserver_init(): RegisterClass() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}

	/* create our 'window' (not visible of course) */
	sound_wnd = CreateWindow (
		SOUND_CLASS_NAME,
		SOUND_CLASS_NAME WINDOW_SUFFIX,
		( WS_DISABLED | WS_POPUP ),
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		NULL,
		NULL
	);

	if (sound_wnd == NULL)
	{
		fprintf(debug_stream, "win32buff_soundserver_init(): CreateWindow() for sound failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}

#if (SSWIN_DEBUG == 1)
	fprintf(debug_stream, "win32buff_soundserver_init() sound_wnd %i, TID %i\n", sound_wnd, GetCurrentThreadId());
#endif

	/* wait until thread has finished initialising */
	if (WaitForSingleObject(thread_created_event, INFINITE) != WAIT_OBJECT_0)
	{
		fprintf(debug_stream, "sound_win32buff_init(): WaitForSingleObject() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}
	CloseHandle(thread_created_event);

	/*** start the sound server ***/
	sci0_buff_ss(&sss);

	return 0;
}

int
sound_win32buff_init(struct _state *s, int flags)
{
	WNDCLASS wc;
	DWORD dwChildId;	/* child thread ID */

	/* let app know what sound server we are running and yield to the scheduler */
	global_sound_server = &sound_server_win32b;
	debug_stream = stderr;

	gs = s;	/* keep copy of pointer */
	sci_sched_yield();

	/*** register event map and windows messages ***/
	#define DECLARE_MESSAGE(name)  RegisterWindowMessage(UM_##name##_MSG)
	emap[SOUND_COMMAND_INIT_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_INIT_HANDLE);
	emap[SOUND_COMMAND_PLAY_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_PLAY_HANDLE);
	emap[SOUND_COMMAND_LOOP_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_LOOP_HANDLE);
	emap[SOUND_COMMAND_DISPOSE_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_DISPOSE_HANDLE);
	emap[SOUND_COMMAND_SET_MUTE] = DECLARE_MESSAGE(SOUND_COMMAND_SET_MUTE);
	emap[SOUND_COMMAND_GET_MUTE] = DECLARE_MESSAGE(SOUND_COMMAND_GET_MUTE);
	emap[SOUND_COMMAND_STOP_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_STOP_HANDLE);
	emap[SOUND_COMMAND_SUSPEND_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_SUSPEND_HANDLE);
	emap[SOUND_COMMAND_RESUME_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_RESUME_HANDLE);
	emap[SOUND_COMMAND_SET_VOLUME] = DECLARE_MESSAGE(SOUND_COMMAND_SET_VOLUME);
	emap[SOUND_COMMAND_RENICE_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_RENICE_HANDLE);
	emap[SOUND_COMMAND_FADE_HANDLE] = DECLARE_MESSAGE(SOUND_COMMAND_FADE_HANDLE);
	emap[SOUND_COMMAND_TEST] = DECLARE_MESSAGE(SOUND_COMMAND_TEST);
	emap[SOUND_COMMAND_STOP_ALL] = DECLARE_MESSAGE(SOUND_COMMAND_STOP_ALL);
	emap[SOUND_COMMAND_SHUTDOWN] = DECLARE_MESSAGE(SOUND_COMMAND_SHUTDOWN);
	emap[SOUND_COMMAND_SAVE_STATE] = DECLARE_MESSAGE(SOUND_COMMAND_SAVE_STATE);
	emap[SOUND_COMMAND_RESTORE_STATE] = DECLARE_MESSAGE(SOUND_COMMAND_RESTORE_STATE);
	emap[SOUND_COMMAND_SUSPEND_ALL] = DECLARE_MESSAGE(SOUND_COMMAND_SUSPEND_ALL);
	emap[SOUND_COMMAND_RESUME_ALL] = DECLARE_MESSAGE(SOUND_COMMAND_RESUME_ALL);
	emap[SOUND_COMMAND_GET_VOLUME] = DECLARE_MESSAGE(SOUND_COMMAND_GET_VOLUME);
	emap[SOUND_COMMAND_PRINT_SONG_INFO] = DECLARE_MESSAGE(SOUND_COMMAND_PRINT_SONG_INFO);
	emap[SOUND_COMMAND_PRINT_CHANNELS] = DECLARE_MESSAGE(SOUND_COMMAND_PRINT_CHANNELS);
	emap[SOUND_COMMAND_PRINT_MAPPING] = DECLARE_MESSAGE(SOUND_COMMAND_PRINT_MAPPING);
	emap[SOUND_COMMAND_IMAP_SET_INSTRUMENT] = DECLARE_MESSAGE(SOUND_COMMAND_IMAP_SET_INSTRUMENT);
	emap[SOUND_COMMAND_IMAP_SET_KEYSHIFT] = DECLARE_MESSAGE(SOUND_COMMAND_IMAP_SET_KEYSHIFT);
	emap[SOUND_COMMAND_IMAP_SET_FINETUNE] = DECLARE_MESSAGE(SOUND_COMMAND_IMAP_SET_FINETUNE);
	emap[SOUND_COMMAND_IMAP_SET_BENDER_RANGE] = DECLARE_MESSAGE(SOUND_COMMAND_IMAP_SET_BENDER_RANGE);
	emap[SOUND_COMMAND_IMAP_SET_PERCUSSION] = DECLARE_MESSAGE(SOUND_COMMAND_IMAP_SET_PERCUSSION);
	emap[SOUND_COMMAND_IMAP_SET_VOLUME] = DECLARE_MESSAGE(SOUND_COMMAND_IMAP_SET_VOLUME);
	emap[SOUND_COMMAND_MUTE_CHANNEL] = DECLARE_MESSAGE(SOUND_COMMAND_MUTE_CHANNEL);
	emap[SOUND_COMMAND_UNMUTE_CHANNEL] = DECLARE_MESSAGE(SOUND_COMMAND_UNMUTE_CHANNEL);
	emap[SOUND_COMMAND_REVERSE_STEREO] = DECLARE_MESSAGE(SOUND_COMMAND_REVERSE_STEREO);
	emap[SOUND_SIGNAL_CUMULATIVE_CUE] = DECLARE_MESSAGE(SOUND_SIGNAL_CUMULATIVE_CUE);
	emap[SOUND_SIGNAL_LOOP] = DECLARE_MESSAGE(SOUND_SIGNAL_LOOP);
	emap[SOUND_SIGNAL_FINISHED] = DECLARE_MESSAGE(SOUND_SIGNAL_FINISHED);
	emap[SOUND_SIGNAL_PLAYING] = DECLARE_MESSAGE(SOUND_SIGNAL_PLAYING);
	emap[SOUND_SIGNAL_PAUSED] = DECLARE_MESSAGE(SOUND_SIGNAL_PAUSED);
	emap[SOUND_SIGNAL_RESUMED] = DECLARE_MESSAGE(SOUND_SIGNAL_RESUMED);
	emap[SOUND_SIGNAL_INITIALIZED] = DECLARE_MESSAGE(SOUND_SIGNAL_INITIALIZED);
	emap[SOUND_SIGNAL_ABSOLUTE_CUE] = DECLARE_MESSAGE(SOUND_SIGNAL_ABSOLUTE_CUE);
	emap[SOUND_COMMAND_DO_SOUND] = DECLARE_MESSAGE(SOUND_COMMAND_DO_SOUND);

	/*** initialise MIDI ***/
	if (init_midi_device(s) < 0)
		return -1;

	/*** set up what we need for data transfer ***/
	InitializeCriticalSection(&dq_cs);
	sci_init_queue(&data_queue);

	new_command_event = (sound_event_t*)sci_malloc(sizeof(sound_event_t));

	/* create event that will signal when data is waiting */
	sound_data_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	/*** register window class for messages to main thread ***/
	memset(&wc, 0, sizeof(WNDCLASS));
	wc.lpfnWndProc = BuffMainWndProc;
	hi_main = GetModuleHandle(NULL);
	if (hi_main == NULL)
	{
		fprintf(debug_stream, "sound_win32buff_init(): GetModuleHandle() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}
	wc.hInstance = hi_main;
	wc.lpszClassName = MAIN_CLASS_NAME;

	if (RegisterClass(&wc) == 0)
		fprintf(debug_stream, "sound_win32buff_init(): RegisterClass() failed, GetLastError() returned %u\n", GetLastError());

	/*** create window for main thread to receive messages (not visible of course) ***/
	main_wnd = CreateWindow (
		MAIN_CLASS_NAME,
		MAIN_CLASS_NAME WINDOW_SUFFIX,
		( WS_DISABLED | WS_POPUP ),
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		NULL,
		NULL
	);

	if (main_wnd == NULL)
	{
		fprintf(debug_stream, "sound_win32buff_init(): CreateWindow() for main failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}

#if (SSWIN_DEBUG == 1)
	fprintf(debug_stream, "sound_win32buff_init() main_wnd %i, TID %i\n", main_wnd, GetCurrentThreadId());
#endif

	/*** create thread ***/
	thread_created_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (thread_created_event == NULL)
	{
		fprintf(debug_stream, "sound_win32buff_init(): CreateEvent(thread_created_event) for main failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}

	child_thread = CreateThread( NULL,		/* not inheritable */
		                         0,			/* use default stack size */
								 win32buff_soundserver_init,	/* callback function */
								 0,			/* cb function parameter - should be s but fails on Win9x */
								 0,			/* thread runs immediately */
								 &dwChildId);	/* pointer to id of thread */
	if (child_thread == NULL)
	{
		fprintf(debug_stream, "sound_win32buff_init(): CreateThread() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}

	/*** set reverse stereo ***/
	if (flags & SOUNDSERVER_INIT_FLAG_REVERSE_STEREO)
	{
#ifdef SSWIN_DEBUG
	fprintf(debug_stream, "sound_win32buff_init() setting reverse stereo\n");
#endif
		global_sound_server->queue_command(0, SOUND_COMMAND_REVERSE_STEREO, 1);
	}

	fprintf(debug_stream, "Sound server win32buff initialised\n");

	/* signal that initialisation is done */
	if (SetEvent(thread_created_event) == 0)
	{
		fprintf(debug_stream, "win32buff_soundserver_init(): SetEvent(thread_created_event) failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}

	return 0;
}

int
sound_win32buff_configure(struct _state *s, char *option, char *value)
{
	return 1; /* No options apply to this driver */
}

sound_event_t *
sound_win32buff_get_event()
{
	/* receives message for main thread queue */

	MSG msg; /* incoming message */
	int i;

	sound_event_t *new_event_event = NULL;

	if (PeekMessage(&msg, main_wnd, 0, 0, PM_REMOVE))
	{
#if (SSWIN_DEBUG == 1)
	fprintf(debug_stream, "sound_win32e_get_event() got msg %i (TID %i)\n", msg.message, GetCurrentThreadId());
#endif
		new_event_event = (sound_event_t*)sci_malloc(sizeof(sound_event_t));
		new_event_event->signal = UNRECOGNISED_SOUND_SIGNAL;
		new_event_event->handle = msg.wParam;
		new_event_event->value = msg.lParam;

		/* map back to normal values */
		for (i = 0; i < sizeof(emap); i++)
		{
			if (emap[i] == msg.message)
			{
				new_event_event->signal = i;
				break;
			}
		}

		if (new_event_event->signal == UNRECOGNISED_SOUND_SIGNAL)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return new_event_event;
	}

	return NULL;
}

sound_event_t *
sound_win32buff_get_command(GTimeVal *wait_tvp)
{
	/* receives message from sound server queue */

	MSG msg; /* incoming message */
	int i;

	new_command_event->signal = UNRECOGNISED_SOUND_SIGNAL;

	if (PeekMessage(&msg, sound_wnd, 0, 0, PM_REMOVE))
	{
#if (SSWIN_DEBUG == 1)
		fprintf(debug_stream, "sound_win32e_get_command() got msg %i (TID %i)\n", msg.message, GetCurrentThreadId());
#endif
		new_command_event->handle = msg.wParam;
		new_command_event->value = msg.lParam;

		/* map back to normal values */
		for (i = 0; i < sizeof(emap); i++)
		{
			if (emap[i] == msg.message)
			{
				new_command_event->signal = i;
				break;
			}
		}

		if (new_command_event->signal == UNRECOGNISED_SOUND_SIGNAL)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

	} else {
		sleep(1);
	}

	return new_command_event;
}

void
sound_win32buff_queue_event(unsigned int handle, unsigned int signal, long value)
{
#if (SSWIN_DEBUG == 1)
		fprintf(debug_stream, "sound_win32buff_queue_event() sending signal %i (%i)\n", signal, emap[signal]);
#endif
	/* posts message to main thread queue */
	if (PostMessage(main_wnd, emap[signal], handle, value) == 0)
	{
		fprintf(debug_stream, "sound_win32buff_queue_event(): PostMessage() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}
}

void
sound_win32buff_queue_command(unsigned int handle, unsigned int signal, long value)
{
#if (SSWIN_DEBUG == 1)
		fprintf(debug_stream, "sound_win32buff_queue_command() sending signal %i (%i)\n", signal, emap[signal]);
#endif
	/* posts message to sound server queue */
	if (PostMessage(sound_wnd, emap[signal], handle, value) == 0)
	{
		fprintf(debug_stream, "sound_win32buff_queue_command(): PostMessage() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}
}

int
sound_win32buff_get_data(byte **data_ptr, int *size)
{
	void *data = NULL;

	__try
	{
		EnterCriticalSection(&dq_cs);
		while (!(data = sci_get_from_queue(&data_queue, size)))
		{
			/* no data */
			LeaveCriticalSection(&dq_cs);

			/* yield */
#ifdef SSWIN_DEBUG
		fprintf(debug_stream, "sound_win32buff_get_data(): waiting for data (TID %i)\n", GetCurrentThreadId());
#endif
			if (WaitForSingleObject(sound_data_event, INFINITE) != WAIT_OBJECT_0)
			{
				fprintf(debug_stream, "sound_win32buff_get_data(): WaitForSingleObject() failed, GetLastError() returned %u\n", GetLastError());
				exit(-1);
			}

			/* re-enter critical section */
			EnterCriticalSection(&dq_cs);
		}
	}
	__finally
	{
		LeaveCriticalSection(&dq_cs);
	}

#ifdef SSWIN_DEBUG
		fprintf(debug_stream, "sound_win32buff_get_data(): got data (TID %i)\n", GetCurrentThreadId());
#endif
	*data_ptr = (byte *)data;
	return *size;
}

int
sound_win32buff_send_data(byte *data_ptr, int maxsend)
{
#ifdef SSWIN_DEBUG
		fprintf(debug_stream, "sound_win32buff_send_data(): sending data (TID %i)\n", GetCurrentThreadId());
#endif
	__try
	{
		EnterCriticalSection(&dq_cs);
		sci_add_to_queue(&data_queue, sci_memdup(data_ptr, maxsend), maxsend);
	}
	__finally
	{
		LeaveCriticalSection(&dq_cs);
	}

	/* signal event that data is waiting */
	if (SetEvent(sound_data_event) == 0)
	{
		fprintf(debug_stream, "sound_win32buff_send_data(): SetEvent() failed, GetLastError() returned %u\n", GetLastError());
		exit(-1);
	}

	return maxsend;
}

void
sound_win32buff_exit(struct _state *s)
{
	/* kill server */
	global_sound_server->queue_command(0, SOUND_COMMAND_SHUTDOWN, 0);

	/* kill child thread */
	WaitForSingleObject(child_thread, INFINITE);
	CloseHandle(child_thread);
	DeleteCriticalSection(&dq_cs);

	/* close handles */
	CloseHandle(sound_data_event);
	DestroyWindow(sound_wnd);
	DestroyWindow(main_wnd);
	UnregisterClass(SOUND_CLASS_NAME, hi_sound);
	UnregisterClass(MAIN_CLASS_NAME, hi_main);

	sci_free(new_command_event);
}

int
sound_win32buff_save(struct _state *s, char *dir)
{
	int retval;
	int size;
	int *success = NULL;

	/* we ignore the dir */
	global_sound_server->queue_command(0, SOUND_COMMAND_SAVE_STATE, 2);
	global_sound_server->send_data((byte *) ".", 2);

	global_sound_server->get_data((byte **) &success, &size);
	retval = *success;
	sci_free(success);

	return retval;
}

sound_server_t sound_server_win32b = {
	"win32b",
	"0.2",
	0,
	&sound_win32buff_init,
	&sound_win32buff_configure,
	&sound_win32buff_exit,
	&sound_win32buff_get_event,
	&sound_win32buff_queue_event,
	&sound_win32buff_get_command,
	&sound_win32buff_queue_command,
	&sound_win32buff_get_data,
	&sound_win32buff_send_data,
	&sound_win32buff_save,
	&sound_restore_default,
	&sound_command_default,
	&sound_suspend_default,
	&sound_resume_default
};

#endif