/***************************************************************************
 tools.c Copyright (C) 1999,2000,2001 Christoph Reichenbach


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
#include <kdebug.h>
#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#ifdef _MSC_VER
#  include <sys/timeb.h>
#  include <windows.h>
#  define sleep(x) timeBeginPeriod(1); Sleep(x); SwitchToThread(); timeEndPeriod(1);
#endif
#if !defined(HAVE_FNMATCH) && !defined(_MSC_VER)
#  include <beos/fnmatch.h>
#endif


#ifdef HAVE_MEMFROB
void *memfrob(void *s, size_t n);
#endif

int script_debug_flag = 0; /* Defaulting to running mode */
int sci_debug_flags = 0; /* Special flags */


#define MEMTEST_HARDNESS 15

int
memtest(char *where, ...)
{
	va_list argp;
	int i;
	void *blocks[MEMTEST_HARDNESS + 1];
	fprintf(stderr,"Memtesting ");

	va_start(argp, where);
	vfprintf(stderr, where, argp);
	va_end(argp);

	fprintf(stderr,"\n");
	for (i = 0; i < MEMTEST_HARDNESS; i++) {
		blocks[i] = sci_malloc(1 + i);
#ifdef HAVE_MEMFROB
		memfrob(blocks[i], 1 + i);
#else
		memset(blocks[i], 42, 1 + i);
#endif
	}
	for (i = 0; i < MEMTEST_HARDNESS; i++)
		free(blocks[i]);

	for (i = 0; i < MEMTEST_HARDNESS; i++) {
		blocks[i] = sci_malloc(5 + i*5);
#ifdef HAVE_MEMFROB
		memfrob(blocks[i], 5 + i*5);
#else
		memset(blocks[i], 42, 5 + i*5);
#endif
	}
	for (i = 0; i < MEMTEST_HARDNESS; i++)
		free(blocks[i]);

	for (i = 0; i < MEMTEST_HARDNESS; i++) {
		blocks[i] = sci_malloc(5 + i*100);
#ifdef HAVE_MEMFROB
		memfrob(blocks[i], 5 + i*100);
#else
		memset(blocks[i], 42, 5 + i*100);
#endif
	}
	for (i = 0; i < MEMTEST_HARDNESS; i++)
		free(blocks[i]);

	for (i = 0; i < MEMTEST_HARDNESS; i++) {
		blocks[i] = sci_malloc(5 + i*1000);
#ifdef HAVE_MEMFROB
		memfrob(blocks[i], 5 + i * 1000);
#else
		memset(blocks[i], 42, 5 + i * 1000);
#endif
	}
	for (i = 0; i < MEMTEST_HARDNESS; i++)
		free(blocks[i]);
	fprintf(stderr,"Memtest succeeded!\n");
	return 0;
}

int sci_ffs(int _mask)
{
	int retval = 0;

	if (!_mask) return 0;
	retval++;
	while (! (_mask & 1))
	{
		retval++;
		_mask >>= 1;
	}

	return retval;
}


/******************** Debug functions ********************/

void
_SCIkvprintf(FILE *file, char *format, va_list args)
{
	vfprintf(file, format, args);
	if (con_file) vfprintf(con_file, format, args);
}

void
_SCIkprintf(FILE *file, char *format, ...)
{
	va_list args;

	va_start(args, format);
	_SCIkvprintf(file, format, args);
	va_end (args);
}


void
_SCIkwarn(state_t *s, char *file, int line, int area, char *format, ...)
{
	va_list args;

	if (area == SCIkERROR_NR)
		_SCIkprintf(stderr, "ERROR: ");
	else
		_SCIkprintf(stderr, "Warning: ");

	va_start(args, format);
	_SCIkvprintf(stderr, format, args);
	va_end(args);
	fflush(NULL);

	if (sci_debug_flags & _DEBUG_FLAG_BREAK_ON_WARNINGS) script_debug_flag=1;
}

void
_SCIkdebug(state_t *s, char *file, int line, int area, char *format, ...)
{
	va_list args;

	if (s->debug_mode & (1 << area)) {
		_SCIkprintf(stdout, " kernel: (%s L%d): ", file, line);
		va_start(args, format);
		_SCIkvprintf(stdout, format, args);
		va_end(args);
		fflush(NULL);
	}
}

void
_SCIGNUkdebug(char *funcname, state_t *s, char *file, int line, int area, char *format, ...)
{
	va_list xargs;
	int error = ((area == SCIkWARNING_NR) || (area == SCIkERROR_NR));

	if (error || (s->debug_mode & (1 << area))) { /* Is debugging enabled for this area? */

		_SCIkprintf(stderr, "FSCI: ");

		if (area == SCIkERROR_NR)
			_SCIkprintf(stderr, "ERROR in %s ", funcname);
		else if (area == SCIkWARNING_NR)
			_SCIkprintf(stderr, "%s: Warning ", funcname);
		else _SCIkprintf(stderr, funcname);

		_SCIkprintf(stderr, "(%s L%d): ", file, line);

		va_start(xargs, format);
		_SCIkvprintf(stderr, format, xargs);
		va_end(xargs);

	}
}


#ifdef HAVE_SYS_TIME_H
void
sci_gettime(long *seconds, long *useconds)
{
	struct timeval tv;

	assert(!gettimeofday(&tv, NULL));
	*seconds = time(NULL);
	*useconds = tv.tv_usec;
}
#elif defined (_MSC_VER)

/*WARNING(Incorrect)*/
/* Warning: This function only retrieves the amount of mseconds since the start of
** the Win32 kernel; it does /not/ provide the number of seconds since the epoch!
** There are no known cases where this causes problems, though.  */
void sci_gettime(long *seconds, long *useconds)
{
	DWORD tm;

	if (TIMERR_NOERROR != timeBeginPeriod(1))
	{
		fprintf(stderr, "timeBeginPeriod(1) failed in sci_gettime\n");
	}

	tm = timeGetTime();

	if (TIMERR_NOERROR != timeEndPeriod(1))
	{
		fprintf(stderr, "timeEndPeriod(1) failed in sci_gettime\n");
	}

	*seconds = tm/1000;
	*useconds = tm*1000;
}
#else
#  error "You need to provide a microsecond resolution sci_gettime implementation for your platform!"
#endif


void
sci_get_current_time(GTimeVal *val)
{
	long foo, bar;
	sci_gettime(&foo, &bar);
	val->tv_sec = foo;
	val->tv_usec = bar;
}


/************* Directory entities *************/
#if defined(_WIN32) && defined(_MSC_VER)
/******** Dir: Win32 CODE ********/

void
sci_init_dir(sci_dir_t *dir)
{
	dir->search = -1;
}

char *
sci_find_first(sci_dir_t *dir, char *mask)
{
	dir->search = _findfirst(mask, &(dir->fileinfo));

	if (dir->search != -1)
	{
		if (dir->fileinfo.name == NULL)
		{
			return NULL;
		}

		if (strcmp(dir->fileinfo.name, ".") == 0 ||
			strcmp(dir->fileinfo.name, "..") == 0)
		{
			if (sci_find_next(dir) == NULL)
			{
				return NULL;
			}
		}

		return dir->fileinfo.name;
	}
	else
	{
		switch (errno)
		{
			case ENOENT:
			{
#ifdef _DEBUG
				printf("_findfirst errno = ENOENT: no match\n");

				if (mask)
					printf(" in: %s\n", mask);
				else
					printf(" - searching in undefined directory\n");
#endif
				break;
			}
			case EINVAL:
			{
				printf("_findfirst errno = EINVAL: invalid filename\n");
				break;
			}
			default:
				printf("_findfirst errno = unknown (%d)", errno);
		}
	}

	return NULL;
}

char *
sci_find_next(sci_dir_t *dir)
{
			  if (dir->search == -1)
			          return NULL;

			  if (_findnext(dir->search, &(dir->fileinfo)) < 0) {
			          _findclose(dir->search);
			          dir->search = -1;
			          return NULL;
			  }

		if (strcmp(dir->fileinfo.name, ".") == 0 ||
			strcmp(dir->fileinfo.name, "..") == 0)
		{
			if (sci_find_next(dir) == NULL)
			{
				return NULL;
			}
		}

		return dir->fileinfo.name;
}

void
sci_finish_find(sci_dir_t *dir)
{
			  if(dir->search != -1) {
			          _findclose(dir->search);
		dir->search = -1;
	}
}

#else /* !_WIN32 */
/******** Dir: UNIX CODE ********/

void
sci_init_dir(sci_dir_t *dir)
{
	dir->dir = NULL;
	dir->mask_copy = NULL;
}

char *
sci_find_first(sci_dir_t *dir, char *mask)
{
	if (dir->dir)
		closedir(dir->dir);

	if (!(dir->dir = opendir("."))) {
		sciprintf("%s, L%d: opendir(\".\") failed!\n", __FILE__, __LINE__);
		return NULL;
	}

	dir->mask_copy = sci_strdup(mask);

	return sci_find_next(dir);
}

char *
sci_find_next(sci_dir_t *dir)
{
	struct dirent *match;

	while ((match = readdir(dir->dir))) {
		if (match->d_name[0] == '.')
			continue;

		if (!fnmatch(dir->mask_copy, match->d_name, 0))
			return match->d_name;
	}

	sci_finish_find(dir);
	return NULL;
}

void
sci_finish_find(sci_dir_t *dir)
{
	if (dir->dir) {
		closedir(dir->dir);
		dir->dir = NULL;
		free(dir->mask_copy);
		dir->mask_copy = NULL;
	}
}

#endif /* !_WIN32 */

/************* /Directory entities *************/


int
sci_mkpath(char *path)
{
        char *nextsep = NULL, *path_pos = path;

	fprintf(stderr, "sci_mkpath: PATH='%d'\n", path);

        if (chdir(G_DIR_SEPARATOR_S)) { /* Go to root */
                sciprintf("Error: Could not change to root directory '%s'!\n",
			  G_DIR_SEPARATOR_S);
                return -1;
        }

        do {
                if (nextsep)
                        *nextsep = G_DIR_SEPARATOR_S[0];
                nextsep = strchr(path_pos, G_DIR_SEPARATOR_S[0]);

                if (nextsep)
                        *nextsep = 0;

		if (*path_pos) { /* Unless we're at the first slash... */
			if (chdir(path_pos)) {
				if (scimkdir(path_pos, 0700) || chdir(path_pos)) {
					sciprintf("Error: Could not create subdirectory '%s' in",
						  path_pos);
					if (nextsep)
						*nextsep = G_DIR_SEPARATOR_S[0];
					sciprintf(" '%s'!\n", path);
					return -2;
				}
			}
		}
		path_pos = nextsep + 1;

        } while (nextsep);

        return 0;
}



char *
sci_get_homedir()
{
#ifdef _WIN32
	return getenv("WINDIR");
#elif defined(__unix__) || !defined(X_DISPLAY_MISSING) || defined (__BEOS__)
	return getenv("HOME");
#else
#  error Please add a $HOME policy for your platform!
#endif
}


sci_queue_t *
sci_init_queue(sci_queue_t *queue)
{
	queue->start = queue->end = NULL;
	return queue;
}

sci_queue_t *
sci_add_to_queue(sci_queue_t *queue, void *data, int type)
{
	sci_queue_node_t *node = sci_malloc(sizeof(sci_queue_node_t));

	node->next = NULL;
	node->data = data;
	node->type = type;

	if (queue->start)
		queue->start->next = node;

	queue->start = node;

	if (!queue->end)
		queue->end = node;

	return queue;
}

void *
sci_get_from_queue(sci_queue_t *queue, int *type)
{
	sci_queue_node_t *node = queue->end;
	if (node) {
		void *retval = node->data;
		if (type)
			*type = node->type;

		queue->end = node->next;

		if (queue->end == NULL) /* Queue empty? */
			queue->start = NULL;

		free(node);
		return retval;
	}
	return NULL;
}


/*-- Yielding to the scheduler --*/

#ifdef HAVE_SCHED_YIELD
#  include <sched.h>

void
sci_sched_yield()
{
	sched_yield();
}

#else

void
sci_sched_yield()
{
	sleep(0);
}

#endif /* !HAVE_SCHED_YIELD */


static char *
_fcaseseek(char *fname, sci_dir_t *dir)
		 /* Expects *dir to be uninitialized and the caller to
		 ** free it afterwards  */
{
	char *buf, *iterator;
	char _buf[14];
	char *retval = NULL, *name;

	if ((strchr(fname, '/')) || (strchr(fname, '\\'))) {
		fprintf(stderr, "fcaseopen() does not support subdirs\n");
		BREAKPOINT();
	}

	if (strlen(fname) > 12) /* not a DOS file? */
		buf = sci_malloc(strlen(fname) + 1);
	else
		buf = _buf;

	sci_init_dir(dir);

	/* Replace all letters with '?' chars */
	strcpy(buf, fname);
	iterator = buf;
	while (*iterator) {
		if (isalpha(*iterator))
			*iterator = '?';
		iterator++;
	}

	name = sci_find_first(dir, buf);

	while (name && !retval) {
		if (!strcasecmp(fname, name))
			retval = name;
		else
			name = sci_find_next(dir);
	}

	if (strlen(fname) > 12)
		free(buf);

	return retval;
}


FILE *
sci_fopen(char *fname, char *mode)
{
	sci_dir_t dir;
	char *name = _fcaseseek(fname, &dir);
	FILE *file = NULL;

	if (name)
		file = fopen(name, mode);
	else if (strchr(mode, 'w'))
		file = fopen(fname, mode);

	sci_finish_find(&dir); /* Free memory */

	return file;
}

int
sci_open(char *fname, int flags)
{
	sci_dir_t dir;
	char *name = _fcaseseek(fname, &dir);
	int file = 0;

	if (name)
		file = open(name, flags);

	sci_finish_find(&dir); /* Free memory */

	return file;
}

char *
sci_getcwd()
{
	int size = 0;
	char *cwd = NULL;

	while (size < 8192) {
		size += 256;
		cwd = sci_malloc(size);
		if (getcwd(cwd, size-1))
			return cwd;

		sci_free(cwd);
	}

	fprintf(stderr,"Could not determine current working directory!\n");
	return NULL;
}
