/***************************************************************************
 resource_patch.c  Copyright (C) 2001 Christoph Reichenbach


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


#include <sciresource.h>
#include <sci_memory.h>


void
sci0_sprintf_patch_file_name(char *string, resource_t *res)
{
	sprintf(string, "%s.%03i", sci_resource_types[res->type], res->number);
}

int
sci0_read_resource_patches(char *path, resource_t **resource_p, int *resource_nr_p)
{
	sci_dir_t dir;
	char *entry;

	sci_init_dir(&dir);
	entry = sci_find_first(&dir, "*.???");
	while (entry) {
		int restype = sci_invalid_resource;
		int resnumber = -1;
		int i;
		unsigned int resname_len;
		char *endptr;

		for (i = sci_view; i < sci_invalid_resource; i++)
			if (strncasecmp(sci_resource_types[i], entry,
					strlen(sci_resource_types[i])) == 0)
				restype = i;

		if (restype != sci_invalid_resource) {

			resname_len = strlen(sci_resource_types[restype]);
			if (entry[resname_len] != '.')
				restype = sci_invalid_resource;
			else {
				resnumber = strtol(entry + 1 + resname_len,
						   &endptr, 10); /* Get resource number */
				if ((*endptr != '\0') || (resname_len+1 == strlen(entry)))
					restype = sci_invalid_resource;

				if ((resnumber < 0) || (resnumber > 1000))
					restype = sci_invalid_resource;
			}
		}

		if (restype != sci_invalid_resource) {
			struct stat filestat;

			printf("Patching \"%s\": ", entry);

			if (stat(entry, &filestat))
				perror("""__FILE__"": (""__LINE__""): stat()");
			else {
				int file;
				guint8 filehdr[2];
				resource_t *newrsc = _scir_find_resource_unsorted(*resource_p,
										  *resource_nr_p,
										  restype,
										  resnumber);

				if (filestat.st_size < 3) {
					printf("File too small\n");
					entry = sci_find_next(&dir);
					continue; /* next file */
				}

				file = open(entry, O_RDONLY);
				if (!file)
					perror("""__FILE__"": (""__LINE__""): open()");
				else {

					read(file, filehdr, 2);
					if ((filehdr[0] & 0x7f) != restype) {
						printf("Resource type mismatch\n");
						close(file);
					} else {

						if (!newrsc) {
							/* Completely new resource! */
							++(*resource_nr_p);
							*resource_p = sci_realloc(*resource_p,
										  *resource_nr_p
										  * sizeof(resource_t));
							newrsc = *resource_p + *resource_nr_p;
#ifdef SATISFY_PURIFY
							memset(newrsc, 0, sizeof(resource_t));
#endif
						}

						/* Overwrite everything, because we're patching */
						newrsc->size = filestat.st_size - 2;
						newrsc->id = restype << 11 | resnumber;
						newrsc->number = resnumber;
						newrsc->status = SCI_STATUS_NOMALLOC;
						newrsc->type = restype;
						newrsc->file = SCI_RESOURCE_FILE_PATCH;
						newrsc->file_offset = 2;

#ifdef SATISFY_PURIFY
						memset(newrsc->data, 0, newrsc->size);
#endif
						_scir_add_altsource(newrsc, SCI_RESOURCE_FILE_PATCH, 2);

						close(file);

						printf("OK\n");

					}
				}
			}
		}
		entry = sci_find_next(&dir);
	}

	return 0;
}
