/***************************************************************************
 main.c Copyright (C) 1999,2000,01 Christoph Reichenbach


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
#include <sound.h>
#include <uinput.h>
#include <console.h>
#include <gfx_operations.h>
#include <gfx_drivers_list.h>
#include <sci_conf.h>
#include <kdebug.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef HAVE_FORK
#  include <sys/wait.h>
#endif
#ifdef HAVE_SCHED_YIELD
#  include <sched.h>
#endif /* HAVE_SCHED_YIELD */

#ifdef _MSC_VER
#define extern __declspec(dllimport) extern
#endif

#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#ifdef HAVE_READLINE_HISTORY_H
#include <readline/history.h>
#endif /* HAVE_READLINE_HISTORY_H */
#endif /* HAVE_READLINE_READLINE_H */

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif HAVE_GETOPT_H

#ifdef HAVE_GETOPT_LONG
#define EXPLAIN_OPTION(longopt, shortopt, description) "  " longopt "\t" shortopt "\t" description "\n"
#else /* !HAVE_GETOPT_H */
#define EXPLAIN_OPTION(longopt, shortopt, description) "  " shortopt "\t" description "\n"
#endif /* !HAVE_GETOPT_H */


#ifdef _WIN32
#include <direct.h>
#define PATH_MAX 255
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define sleep Sleep
#define strcasecmp stricmp
#endif

#ifndef HAVE_SCHED_YIELD
#define sched_yield() sleep(1)
/* Neither NetBSD nor Win32 have this function, although it's in POSIX 1b */
#endif /* !HAVE_SCHED_YIELD */

static gfx_state_t static_gfx_state; /* see below */
static gfx_options_t static_gfx_options; /* see below */

static state_t *gamestate; /* The main game state */
static gfx_state_t *gfx_state = &static_gfx_state; /* The graphics state */
static gfx_options_t *gfx_options = &static_gfx_options; /* Graphics options */

int
c_quit(state_t *s)
{
	script_abort_flag = 1; /* Terminate VM */
	_debugstate_valid = 0;
	_debug_seeking = 0;
	_debug_step_running = 0;
	return 0;
}

int
c_die(state_t *s)
{
	exit(0); /* Die */
}


char *old_input = NULL;

#ifdef HAVE_READLINE_READLINE_H
char *
get_readline_input(void)
{
	char *input = readline("> ");

	if (!input) { /* ^D */
		c_quit(NULL);
		return "";
	}

	if (strlen(input) == 0) {
		free (input);
	} else {

#ifdef HAVE_READLINE_HISTORY_H
		add_history(input);
#endif /* HAVE_READLINE_HISTORY_H */

		if (old_input) {
			free(old_input);
		}
		old_input = input;
	}

	return old_input? old_input : "";
}
#endif /* HAVE_READLINE_READLINE_H */


int
init_directories(char *work_dir, char *game_id)
{
	char *homedir = getenv("HOME");

	printf("Initializing directories...\n");
	if (!homedir) { /* We're probably not under UNIX if this happens */

		if (!getcwd(work_dir, PATH_MAX)) {
			fprintf(stderr,"Cannot get the working directory!\n");
			return 1;
		}

		return 0;
	}

  /* So we've got a home directory */

	if (chdir(homedir)) {
		fprintf(stderr,"Error: Could not enter home directory %s.\n", homedir);
		perror("Reason");
		return 1; /* If we get here, something really bad is happening */
	}

	if (strlen(homedir) > MAX_HOMEDIR_SIZE) {
		fprintf(stderr, "Your home directory path is too long. Re-compile FreeSCI with "
			"MAX_HOMEDIR_SIZE set to at least %i and try again.\n", strlen(homedir));
		return 1;
	}

	if (chdir(FREESCI_GAMEDIR)) {
		if (scimkdir(FREESCI_GAMEDIR, 0700)) {

			fprintf(stderr, "Warning: Could not enter ~/"FREESCI_GAMEDIR"; save files"
				" will be written to ~/\n");

			return 0;

		}
		else /* mkdir() succeeded */
			chdir(FREESCI_GAMEDIR);
	}

	if (chdir(game_id)) {
		if (scimkdir(game_id, 0700)) {

			fprintf(stderr,"Warning: Could not enter ~/"FREESCI_GAMEDIR"/%s; "
				"save files will be written to ~/"FREESCI_GAMEDIR"\n", game_id);

			return 0;
		}
		else /* mkdir() succeeded */
			chdir(game_id);
	}

	getcwd(work_dir, PATH_MAX);

	return 0;
}


char *
get_gets_input(void)
{
	static char input[1024] = "";

	putchar('>');

	while (!strchr(input, '\n'))
		fgets(input, 1024, stdin);

	if (!input) {
		c_quit(NULL);
		return "";
	}

	if (strlen(input))
		if (input[strlen(input)-1] == '\n');
	input[strlen(input)-1] = 0; /* Remove trailing '\n' */

	if (strlen(input) == 0) {
		return old_input? old_input : "";
	}

	if (old_input)
		free(old_input);

	old_input = malloc(1024);
	strcpy(old_input, input);
	return input;
}

/**********************************************************/
/* Startup and config management                          */ 
/**********************************************************/

typedef struct {
	int script_debug_flag;
	int scale_x, scale_y, color_depth;
	int mouse;
	sci_version_t version;
	char *gfx_driver_name;
	char *gamedir;
} cl_options_t;

#define ON 1
#define OFF 0
#define DONTCARE -1

static char *
parse_arguments(int argc, char **argv, cl_options_t *cl_options)
{
	int c, i, optindex;
#ifdef HAVE_GETOPT_LONG
	struct option options[] = {
		{"run", no_argument, NULL, 0 },
		{"debug", no_argument, NULL, 1 },
		{"gamedir", required_argument, 0, 'd'},
		{"sci-version", required_argument, 0, 'V'},
		{"graphics", required_argument, 0, 'g'},
		{"version", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'},
		{"scale-x", required_argument, 0, 'x'},
		{"scale-y", no_argument, 0, 'y'},
		{"color-depth", no_argument, 0, 'c'},
		{"disable-mouse", no_argument, 0, 'm'},
		{0,0,0,0}
	};

	options[0].flag = &(cl_options->script_debug_flag);
	options[1].flag = &(cl_options->script_debug_flag);
#endif /* HAVE_GETOPT_H */

	cl_options->scale_x = cl_options->scale_y = cl_options->color_depth = 0;
	cl_options->version = 0;
	cl_options->script_debug_flag = 0;
	cl_options->gfx_driver_name = NULL;
	cl_options->gamedir = NULL;
	cl_options->mouse = ON;

#ifdef HAVE_GETOPT_LONG
	while ((c = getopt_long(argc, argv, "vrhmDd:V:g:x:y:c:", options, &optindex)) > -1) {
#else /* !HAVE_GETOPT_LONG */
	while ((c = getopt(argc, argv, "vrhmDd:V:g:x:y:c:")) > -1) {
#endif /* !HAVE_GETOPT_LONG */
		switch (c) {

		case 'r':
			cl_options->script_debug_flag = 0;
			break;

		case 'D':
			cl_options->script_debug_flag = 1;
			break;

		case 'd':
			if (cl_options->gamedir)
				free(cl_options->gamedir);

			cl_options->gamedir = strdup(optarg);
			break;

		case 'V': {
			int major = *optarg - '0'; /* One version digit */
			int minor = atoi(optarg + 2);
			int patchlevel = atoi(optarg + 6);

			cl_options->version = SCI_VERSION(major, minor, patchlevel);
		}
		break;

		case 'g':
			if (cl_options->gfx_driver_name)
				free(cl_options->gfx_driver_name);
			cl_options->gfx_driver_name = strdup(optarg);
			break;

		case '?':
			/* getopt_long already printed an error message. */
			exit(1);

		case 'x':
			cl_options->scale_x = atoi(optarg);
			break;

		case 'y':
			cl_options->scale_y = atoi(optarg);
			break;

		case 'c':
			cl_options->color_depth = atoi(optarg);
			break;

		case 'm':
			cl_options->mouse = OFF;
			break;

		case 0: /* getopt_long already did this for us */
			break;

		case 'v':
			printf("This is FreeSCI, version %s\n", VERSION);
			printf("Supported graphics drivers: ");

			i = 0;
			while (gfx_drivers[i]) {
				if (i != 0)
					printf(", ");

				printf(gfx_drivers[i]->name);

				i++;
			}
			printf("\n");
			printf("Supported sound drivers: ");
			i = 0;
			while (sfx_drivers[i]) {
				if (i != 0)
					printf(", ");

				printf(sfx_drivers[i]->name);

				i++;
			}
			printf("\n");

			exit(0);

		case 'h':
			printf("Usage: sciv [options] [game name]\n"
			       "Run a Sierra SCI game.\n"
			       "\n"
			       EXPLAIN_OPTION("--gamedir dir\t", "-ddir", "read game resources from dir")
			       EXPLAIN_OPTION("--run\t\t", "-r", "do not start the debugger")
			       EXPLAIN_OPTION("--sci-version\t", "-Vver", "set the version for sciv to emulate")
			       EXPLAIN_OPTION("--version\t", "-v", "display version number and exit")
			       EXPLAIN_OPTION("--debug\t", "-D", "start up in debug mode")
			       EXPLAIN_OPTION("--help\t", "-h", "display this help text and exit")
			       EXPLAIN_OPTION("--graphics gfx", "-ggfx", "use the 'gfx' graphics driver")
			       EXPLAIN_OPTION("--scale-x", "-x", "Set horizontal scale factor")
			       EXPLAIN_OPTION("--scale-y", "-y", "Set vertical scale factor")
			       EXPLAIN_OPTION("--color-depth", "-c", "Specify color depth")
			       EXPLAIN_OPTION("--disable-mouse", "-m", "Disable support for pointing device")
			       "\n"
			       "The game name, if provided, must be equal to a game name as specified in the\n"
			       "FreeSCI config file.\n"
			       "It is overridden by --gamedir.\n"
			       "\n"
			       );
			exit(0);

		default:
			exit(1);
		}
	}
#if 0
	} /* Work around EMACS paren matching bug */
#endif

	if (optind == argc)
		return NULL;

	return
		argv[optind];
}


static int
read_config(char *game_name, config_entry_t **conf, int *conf_entries,
	    sci_version_t *version)
{
	int i, conf_nr = 0;

	*conf_entries = config_init(conf, NULL);

	for (i = 1; i < *conf_entries; i++)
		if (!strcasecmp((*conf)[i].name, game_name)) {
			conf_nr = i;
			*version = (*conf)[i].version;
	    }

	return conf_nr;
}

static void
init_console()
{
#if 0
	con_init();
	con_init_gfx();
	con_visible_rows = 1; /* Fool the functions into believing that we *have* a display */
#endif
	con_hook_command(&c_quit, "quit", "", "console: Quits gracefully");
	con_hook_command(&c_die, "die", "", "console: Quits ungracefully");

	con_hook_int(&(gfx_options->buffer_pics_nr), "buffer_pics_nr",
		     "Number of pics to buffer in LRU storage\n");
	con_hook_int(&(gfx_options->pic0_dither_mode), "pic0_dither_mode",
		     "Mode to use for pic0 dithering\n");
	con_hook_int(&(gfx_options->pic0_dither_pattern), "pic0_dither_pattern",
		     "Pattern to use for pic0 dithering\n");
	con_hook_int(&(gfx_options->pic0_unscaled), "pic0_unscaled",
		     "Whether pic0 should be drawn unscaled\n");
	con_hook_int(&(gfx_options->dirty_frames), "dirty_frames",
		     "Dirty frames management\n");

	con_passthrough = 1; /* enables all sciprintf data to be sent to stdout */

#ifdef HAVE_READLINE_HISTORY_H
	using_history(); /* Activate history for readline */
#endif /* HAVE_READLINE_HISTORY_H */

#ifdef HAVE_READLINE_READLINE_H
	_debug_get_input = get_readline_input; /* Use readline for debugging input */
#else /* !HAVE_READLINE_READLINE_H */
	_debug_get_input = get_gets_input; /* Use gets for debug input */
#endif /* !HAVE_READLINE_READLINE_H */
}


static int
init_gamestate(state_t *gamestate, sci_version_t version)
{
	int errc;

	if ((errc = script_init_engine(gamestate, version))) { /* Initialize game state */
		int recovered = 0;

		if (errc == SCI_ERROR_INVALID_SCRIPT_VERSION) {
			int tversion = SCI_VERSION_FTU_NEW_SCRIPT_HEADER - ((version < SCI_VERSION_FTU_NEW_SCRIPT_HEADER)? 0 : 1);

			while (!recovered && tversion) {
				printf("Trying version %d.%03x.%03d instead\n", SCI_VERSION_MAJOR(tversion),
				       SCI_VERSION_MINOR(tversion), SCI_VERSION_PATCHLEVEL(tversion));

				errc = script_init_engine(gamestate, tversion);

				if ((recovered = !errc))
					version = tversion;

				if (errc != SCI_ERROR_INVALID_SCRIPT_VERSION)
					break;

				switch (tversion) {

				case SCI_VERSION_FTU_NEW_SCRIPT_HEADER - 1:
					if (version >= SCI_VERSION_FTU_NEW_SCRIPT_HEADER)
						tversion = 0;
					else
						tversion = SCI_VERSION_FTU_NEW_SCRIPT_HEADER;
					break;

				case SCI_VERSION_FTU_NEW_SCRIPT_HEADER:
					tversion = 0;
					break;
				}
			}
			if (recovered)
				printf("Success.\n");
		}

		if (!recovered) {
			fprintf(stderr,"Script initialization failed. Aborting...\n");
			return 1;
		}
	}
	return 0;
}

static int
init_gfx(cl_options_t *cl_options, sci_version_t version, gfx_driver_t *driver)
{
	gfx_state->driver = driver;
	gamestate->gfx_state = gfx_state;
	gfx_state->version = version;

	if (cl_options->scale_y > 0 && !cl_options->scale_x)
		cl_options->scale_x = cl_options->scale_y;

	if (cl_options->scale_x > 0) {
		if (cl_options->scale_y == 0)
		  cl_options->scale_y = cl_options->scale_x;

		if (cl_options->color_depth > 0) {
			if (gfxop_init(gfx_state, cl_options->scale_x,
				       cl_options->scale_y, cl_options->color_depth,
				       gfx_options)) { 
				fprintf(stderr,"Graphics initialization failed. Aborting...\n");
				return 1;
			}
		} else {
			cl_options->color_depth = 4;
			while (gfxop_init(gfx_state, cl_options->scale_x,
					  cl_options->scale_y, cl_options->color_depth,
					  gfx_options) && --cl_options->color_depth);

			if (!cl_options->color_depth) {
				fprintf(stderr,"Could not find a matching color depth. Aborting...\n");
				return 1;
			}
		}
	  
	} else if (gfxop_init_default(gfx_state, gfx_options)) { 
		fprintf(stderr,"Graphics initialization failed. Aborting...\n");
		return 1;
	}

	return 0;
}


static gfx_driver_t *
find_gfx_driver(char *name)
{
	gfx_driver_t *retval = gfx_drivers[0];

	if (!name)
		return gfx_drivers[0];

	while (retval && !strcasecmp(name, retval->name))
		retval++;

	return retval;
}


int
main(int argc, char** argv)
{
	config_entry_t *conf = NULL;
	cl_options_t cl_options;
	int conf_entries = -1; /* Number of config entries */
	int conf_nr = -1; /* Element of conf to use */
	int i, errc;
	FILE *console_logfile = NULL;
	char startdir[PATH_MAX+1];
	char resource_dir[PATH_MAX+1];
	char work_dir[PATH_MAX+1];
	char *gfx_driver_name = NULL;
	char *game_name;
	sci_version_t version = 0;
	gfx_driver_t *gfx_driver;

	game_name = parse_arguments(argc, argv, &cl_options);

	getcwd(startdir, PATH_MAX);
	script_debug_flag = cl_options.script_debug_flag;

	printf("FreeSCI "VERSION" Copyright (C) 1999, 2000, 2001 Dmitry Jemerov,\n"
	       "Christopher T. Lansdown, Sergey Lapin, Rickard Lind, Carl Muckenhoupt,\n"
	       " Christoph Reichenbach, Magnus Reftel, Lars Skovlund, Rink Springer\n"
	       "This program is free software. You can copy and/or modify it freely\n"
	       "according to the terms of the GNU general public license, v2.0\n"
	       "or any later version, at your option.\n"
	       "It comes with ABSOLUTELY NO WARRANTY.\n");


	if (game_name) {

		conf_nr = read_config(game_name, &conf, &conf_entries, &version);

		if (!cl_options.gamedir)
			if (chdir(conf[conf_nr].resource_dir)) {
				if (conf_nr)
					fprintf(stderr,"Error entering '%s' to load resource data\n", conf[conf_nr].resource_dir);
				else
					fprintf(stderr,"Game '%s' isn't registered in your config file.\n", game_name);
				exit(1);
			}
	}

	if (cl_options.version)
		version = cl_options.version;

	if (cl_options.gamedir)
		if (chdir(cl_options.gamedir)) {
			printf ("Error changing to game directory '%s'\n", cl_options.gamedir);
			exit(1);
		}

	getcwd(resource_dir, PATH_MAX); /* Store resource directory */

	printf ("Loading resources...\n");
	if ((errc = loadResources(SCI_VERSION_AUTODETECT, 1))) {
		fprintf(stderr,"Error while loading resources: %s!\n",
			SCI_Error_Types[errc]);
		exit(1);
	};

	printf("SCI resources loaded.\n");

	chdir(startdir);

	printf("Mapping instruments to General Midi\n");
	mapMIDIInstruments();


	init_console();
	sciprintf("FreeSCI, version "VERSION"\n");

	gamestate = malloc(sizeof(state_t));

	if (init_gamestate(gamestate, version))
		return 1;

	gamestate->gfx_state = NULL;
	if (game_init(gamestate)) { /* Initialize */
		fprintf(stderr,"Game initialization failed: Aborting...\n");
		return 1;
	}

	if (init_directories(work_dir, gamestate->game_name)) {
		fprintf(stderr,"Error resolving the working directory\n");
		exit(1);
	}
	gamestate->resource_dir = resource_dir;
	gamestate->work_dir = work_dir;

	if (!game_name)
		game_name = gamestate->game_name;

	if (!conf) /* Unless the configuration has been read... */
		conf_nr = read_config(game_name, &conf, &conf_entries, &version);

	if (conf) {
		memcpy(gfx_options, &(conf->gfx_options), sizeof(gfx_options_t)); /* memcpy so that console works */
		gfx_driver = conf[conf_nr].gfx_driver;
	}

	if (!gfx_driver)
		gfx_driver = find_gfx_driver(cl_options.gfx_driver_name);

	if (!gfx_driver) {
		if (gfx_driver_name)
			fprintf(stderr,"Failed to find graphics driver \"%s\"\n"
				"Please run 'sciv -v' to get a list of all "
				"available drivers.\n");
		else
			fprintf(stderr,"No default gfx driver available.\n");

		return 1;
	}

	if (init_gfx(&cl_options, version, gfx_driver))
		return 1;

	if (game_init_graphics(gamestate)) { /* Init interpreter graphics */
		fprintf(stderr,"Game initialization failed: Error in GFX subsystem. Aborting...\n");
		return 1;
	}


	if (conf && conf[conf_nr].work_dir)
		gamestate->work_dir = work_dir;


	if (chdir(gamestate->work_dir)) {
		fprintf(stderr,"Error entering working directory '%s'\n", conf[conf_nr].work_dir);
		exit(1);
	}

	if (!gamestate->version_lock_flag)
		if (conf[conf_nr].version)
			gamestate->version = conf[conf_nr].version;

	if (strlen (conf[conf_nr].debug_mode))
		set_debug_mode (gamestate, 1, conf[conf_nr].debug_mode);

	/* Now configure the graphics driver with the specified options */
	for (i = 0; i < conf[conf_nr].gfx_config_nr; i++)
		if ((gfx_driver->set_parameter)(gfx_driver, conf[conf_nr].gfx_config[i].option,
						conf[conf_nr].gfx_config[i].value)) {
			fprintf(stderr, "Fatal error occured in graphics driver while processing \"%s = %s\"\n",
				conf[conf_nr].gfx_config[i].option, conf[conf_nr].gfx_config[i].value);
			exit(1);
		}

	gamestate->sfx_driver = sfx_drivers[0];

	if (gamestate->sfx_driver) {
		gamestate->sfx_driver->init(gamestate);
		sched_yield(); /* Specified by POSIX 1b. If it doesn't work on your
			       ** system, make up an #ifdef'd version of it above.
			       */
		gamestate->sfx_driver->get_event(gamestate); /* Get init message */
	}

	if (conf[conf_nr].console_log) {
		console_logfile = fopen (conf[conf_nr].console_log, "w");
		con_file = console_logfile;
	}

	printf("Emulating SCI version %d.%03d.%03d\n",
	       SCI_VERSION_MAJOR(gamestate->version),
	       SCI_VERSION_MINOR(gamestate->version),
	       SCI_VERSION_PATCHLEVEL(gamestate->version));

	gamestate->have_mouse_flag = (cl_options.mouse == DONTCARE)?
		conf[conf_nr].mouse : cl_options.mouse;

	game_run(&gamestate); /* Run the game */
  

	if (gamestate->sfx_driver)
		gamestate->sfx_driver->exit(gamestate); /* Shutdown sound daemon first */

	game_exit(gamestate);

	script_free_engine(gamestate); /* Uninitialize game state */

	freeResources();

	if (conf_entries >= 0)
		confifree(&conf, conf_entries);

	if (console_logfile)
		fclose (console_logfile);

	chdir (startdir); /* ? */

#ifdef HAVE_FORK
	printf("Waiting for sound server to die...");
	wait(NULL); /* Wait for sound server process to die, if neccessary */
	printf(" OK.\n");
#endif

	gfxop_exit(gfx_state);

	free(gamestate);

	return 0;
}
