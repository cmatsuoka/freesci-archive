/***************************************************************************
 kstring.c Copyright (C) 1999 Christoph Reichenbach


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
/* String and parser handling */

#include <sciresource.h>
#include <engine.h>
#include <vocabulary.h>
#include <kernel_compat.h>

#define CHECK_OVERFLOW1(pt, size) \
	if (((pt) - (str_base)) + (size) > maxsize) { \
		SCIkwarn(SCIkERROR, "String expansion exceeded heap boundaries\n"); \
		return;\
	}

char *
kernel_lookup_text(state_t *s, reg_t address, int index)
     /* Returns the string the script intended to address */
{
	char *seeker;
	resource_t *textres;

	if (address.segment)
		return (char *) kernel_dereference_pointer(s, address, 0);
	else {
		int textlen;
		int _index = index;
		textres = scir_find_resource(s->resmgr, sci_text, address.offset, 0);

		if (!textres) {
			SCIkwarn(SCIkERROR, "text.%03d not found\n", address);
			return NULL; /* Will probably segfault */
		}

		textlen = textres->size;
		seeker = (char *) textres->data;

		while (index--)
			while ((textlen--) && (*seeker++));

		if (textlen)
			return seeker;
		else {
			SCIkwarn(SCIkERROR, "Index %d out of bounds in text.%03d\n", _index, address);
			return 0;
		}

	}
}


/*************************************************************/
/* Parser */
/**********/

#ifdef SCI_SIMPLE_SAID_CODE
int
vocab_match_simple(state_t *s, heap_ptr addr)
{
  int nextitem;
  int listpos = 0;

  if (!s->parser_valid)
    return SAID_NO_MATCH;

  if (s->parser_valid == 2) { /* debug mode: sim_said */
    do {
      sciprintf("DEBUGMATCH: ");
      nextitem = s->heap[addr++];

      if (nextitem < 0xf0) {
	nextitem = nextitem << 8 | s->heap[addr++];
	if (s->parser_nodes[listpos].type
	    || nextitem != s->parser_nodes[listpos++].content.value)
	  return SAID_NO_MATCH;
      } else {

	if (nextitem == 0xff)
	  return (s->parser_nodes[listpos++].type == -1)? SAID_FULL_MATCH : SAID_NO_MATCH; /* Finished? */

	if (s->parser_nodes[listpos].type != 1
	    || nextitem != s->parser_nodes[listpos++].content.value)
	  return SAID_NO_MATCH;

      }
    } while (42);
  } else { /* normal simple match mode */
    return vocab_simple_said_test(s, addr);
  }
}
#endif /* SCI_SIMPLE_SAID_CODE */


reg_t
kSaid(state_t *s, int funct_nr, int argc, reg_t *argv)
{
	reg_t heap_said_block = argv[0];
	byte *said_block = (byte *) kernel_dereference_pointer(s, heap_said_block, 0);
	int new_lastmatch;

	if (!said_block) {
		SCIkdebug(SCIkWARNING, "Said on non-string, pointer "PREG"\n", PRINT_REG(heap_said_block));
		return NULL_REG;
	}

	if (s->debug_mode & (1 << SCIkPARSER_NR)) {
		SCIkdebug(SCIkPARSER, "Said block:", 0);
		vocab_decypher_said_block(s, said_block);
	}

	if (IS_NULL_REG(s->parser_event) || (GET_SEL32V(s->parser_event, claimed))) {
		return NULL_REG;
	}

#ifdef SCI_SIMPLE_SAID_CODE

  s->acc = 0;

  if (s->parser_lastmatch_word == SAID_FULL_MATCH)
    return; /* Matched before; we're not doing any more matching work today. */

  if ((new_lastmatch = vocab_match_simple(s, said_block)) != SAID_NO_MATCH) {

    if (s->debug_mode & (1 << SCIkPARSER_NR))
      sciprintf("Match (simple).\n");
    s->acc = 1;

    if (new_lastmatch == SAID_FULL_MATCH) /* Finished matching? */
      PUT_SELECTOR(s->parser_event, claimed, 1); /* claim event */
    /* otherwise, we have a partial match: Set new lastmatch word in all cases. */

    s->parser_lastmatch_word = new_lastmatch;
  }

#else /* !SCI_SIMPLE_SAID_CODE */
	if ((new_lastmatch = said(s, said_block, (s->debug_mode & (1 << SCIkPARSER_NR))))
	    != SAID_NO_MATCH) { /* Build and possibly display a parse tree */

		if (s->debug_mode & (1 << SCIkPARSER_NR))
			sciprintf("Match.\n");

		s->r_acc = make_reg(0, 1);

		if (new_lastmatch != SAID_PARTIAL_MATCH)
			PUT_SEL32V(s->parser_event, claimed, 1);

		s->parser_lastmatch_word = new_lastmatch;

	} else {
		s->parser_lastmatch_word = SAID_NO_MATCH;
		return NULL_REG;
	}
#endif /* !SCI_SIMPLE_SAID_CODE */
	return s->r_acc;
}


void
kSetSynonyms(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
#ifdef __GNUC__
#warning "Re-implement kSetSynonyms()"
#endif
#if 0
	heap_ptr list = UPARAM(0);
	int script;
	int synpos = 0;

	SCIkASSERT(list > 800);
	if (s->synonyms_nr)
		free(s->synonyms);

	s->synonyms_nr = 0;

	list = UGET_SELECTOR(list, elements); /* Get the number of elements */
	list = UGET_HEAP(list + LIST_FIRST_NODE); /* Get first list node */

	while (list) {
		heap_ptr objpos = GET_HEAP(list + LIST_NODE_VALUE);

		script = UGET_SELECTOR(objpos, number);
		SCIkASSERT(script <= 1000);

		if (s->scripttable[script].heappos) {

			if (s->scripttable[script].synonyms_nr) {
				int i;
				if (s->synonyms_nr)
					s->synonyms = sci_realloc(s->synonyms,
								  sizeof(synonym_t) * (s->synonyms_nr + s->scripttable[script].synonyms_nr));
				else
					s->synonyms = sci_malloc(sizeof(synonym_t) * (s->scripttable[script].synonyms_nr));

				s->synonyms_nr +=  s->scripttable[script].synonyms_nr;

				SCIkdebug(SCIkPARSER, "Setting %d synonyms for script.%d\n",
					  s->scripttable[script].synonyms_nr, script);

				if (s->scripttable[script].synonyms_nr > 16384) {
					SCIkwarn(SCIkERROR, "Heap corruption: script.%03d has %d synonyms!\n",
						 script, s->scripttable[script].synonyms_nr);
					s->scripttable[script].synonyms_nr = 0;
				} else

					for (i = 0; i < s->scripttable[script].synonyms_nr; i++) {
						s->synonyms[synpos].replaceant = UGET_HEAP(s->scripttable[script].synonyms_offset + i * 4);
						s->synonyms[synpos].replacement = UGET_HEAP(s->scripttable[script].synonyms_offset + i * 4 + 2);

						synpos++;
					}
			}

		} else SCIkwarn(SCIkWARNING, "Synonyms of script.%03d were requested, but script is not available\n");

		list = GET_HEAP(list + LIST_NEXT_NODE);
	}

	SCIkdebug(SCIkPARSER, "A total of %d synonyms are active now.\n", s->synonyms_nr);

	if (!s->synonyms_nr)
		s->synonyms = NULL;
#endif
}



reg_t
kParse(state_t *s, int funct_nr, int argc, reg_t *argv)
{
	reg_t stringpos = argv[0];
	char *string = (char *) kernel_dereference_pointer(s, stringpos, 0);
	int words_nr;
	char *error;
	result_word_t *words;
	reg_t event = argv[1];

	s->parser_event = event;

	s->parser_lastmatch_word = SAID_NO_MATCH;

	if (s->parser_valid == 2) {
		sciprintf("Parsing skipped: Parser in simparse mode\n");
		return;
	}

	words = vocab_tokenize_string(string, &words_nr,
				      s->parser_words, s->parser_words_nr,
				      s->parser_suffices, s->parser_suffices_nr,
				      &error);
	s->parser_valid = 0; /* not valid */

	if (words) {

		int syntax_fail = 0;

		vocab_synonymize_tokens(words, words_nr, s->synonyms, s->synonyms_nr);

		s->acc = 1;

		if (s->debug_mode & (1 << SCIkPARSER_NR)) {
			int i;

			SCIkdebug(SCIkPARSER, "Parsed to the following blocks:\n", 0);

			for (i = 0; i < words_nr; i++)
				SCIkdebug(SCIkPARSER, "   Type[%04x] Group[%04x]\n", words[i].w_class, words[i].group);
		}

		if (vocab_build_parse_tree(&(s->parser_nodes[0]), words, words_nr, s->parser_branches,
					   s->parser_rules))
			syntax_fail = 1; /* Building a tree failed */

#ifdef SCI_SIMPLE_SAID_CODE
		vocab_build_simple_parse_tree(&(s->parser_nodes[0]), words, words_nr);
#endif /* SCI_SIMPLE_SAID_CODE */

		free(words);

		if (syntax_fail) {

			s->acc = 1;
			PUT_SEL32V(event, claimed, 1);

			invoke_selector(INV_SEL(s->game_obj, syntaxFail, 0), 2, s->parser_base, stringpos);
			/* Issue warning */

			SCIkdebug(SCIkPARSER, "Tree building failed\n");

		} else {
			s->parser_valid = 1;
			PUT_SEL32V(event, claimed, 0);
#ifndef SCI_SIMPLE_SAID_CODE
			if (s->debug_mode & (1 << SCIkPARSER_NR))
				vocab_dump_parse_tree("Parse-tree", s->parser_nodes);
#endif /* !SCI_SIMPLE_SAID_CODE */
		}

	} else {

		s->r_acc = make_reg(0, 0);
		PUT_SEL32V(event, claimed, 1);
		if (error) {
			char *pbase_str = (char *) kernel_dereference_pointer(s, s->parser_base, 0);
			strcpy(pbase_str, error);
			SCIkdebug(SCIkPARSER,"Word unknown: %s\n", error);
			/* Issue warning: */

			invoke_selector(INV_SEL(s->game_obj, wordFail, 0), 2, s->parser_base, stringpos);
			free(error);
			return make_reg(0, 1); /* Tell them that it dind't work */
		}
	}

	return s->r_acc;
}


void
kStrEnd(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
	heap_ptr address = UPARAM(0);
	char *seeker = (char *) s->heap + address;

	while (*seeker++)
		++address;

	s->acc = address + 1; /* End of string */
}

void
kStrCat(state_t *s, int funct_nr, int argc, heap_ptr argp)
{

	strcat((char *) s->heap + UPARAM(0), (char *) s->heap + UPARAM(1));
}

void
kStrCmp(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
	if (argc > 2)
		s->acc = strncmp((char *) (s->heap + UPARAM(0)), (char *) (s->heap + UPARAM(1)), UPARAM(2));
	else
		s->acc = strcmp((char *) (s->heap + UPARAM(0)), (char *) (s->heap + UPARAM(1)));
}


reg_t
kStrCpy(state_t *s, int funct_nr, int argc, reg_t *argv)
{
	char *dest = (char *) kernel_dereference_pointer(s, argv[0], 0);
	char *src = (char *) kernel_dereference_pointer(s, argv[1], 0);

	if (!dest) {
		SCIkdebug(SCIkWARNING, "Attempt to strcpy TO invalid pointer "PREG"!\n",
			  PRINT_REG(argv[0]));
		return NULL_REG;
	}
	if (!src) {
		SCIkdebug(SCIkWARNING, "Attempt to strcpy FROM invalid pointer "PREG"!\n",
			  PRINT_REG(argv[1]));
		return NULL_REG;
	}

	if (argc > 2)
	{
		int length = SKPV(2);

		if (length>=0)
			strncpy(dest, src, length); else
				memcpy(dest, src, -length); 
	}
	else
		strcpy(dest, src);

	return argv[0];
}


reg_t
kStrAt(state_t *s, int funct_nr, int argc, reg_t *argv)
{
	unsigned char *dest = (unsigned char *) kernel_dereference_pointer(s, argv[0], 0);

	if (!dest) {
		SCIkdebug(SCIkWARNING, "Attempt to strat at invalid pointer "PREG"!\n",
			  PRINT_REG(argv[0]));
		return NULL_REG;
	}

	dest += KP_UINT(argv[1]);

	s->r_acc = make_reg(0, *dest);

	if (argc > 2)
		*dest = KP_SINT(argv[2]); /* Request to modify this char */

	return s->r_acc;
}


void
kReadNumber(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
	char *source = (char *) (s->heap + UPARAM(0));

	while (isspace(*source))
		source++; /* Skip whitespace */

	if (*source == '$') /* SCI uses this for hex numbers */
		s->acc = (gint16)strtol(source + 1, NULL, 16); /* Hex */
	else
		s->acc = (gint16)strtol(source, NULL, 10); /* Force decimal */
}


/*  Format(targ_address, textresnr, index_inside_res, ...)
** or
**  Format(targ_address, heap_text_addr, ...)
** Formats the text from text.textresnr (offset index_inside_res) or heap_text_addr according to
** the supplied parameters and writes it to the targ_address.
*/
reg_t
kFormat(state_t *s, int funct_nr, int argc, reg_t *argv)
{
	int *arguments;
	reg_t dest = argv[0];
	char *target = (char *) kernel_dereference_pointer(s, dest, 0);
	char *tstart = target;
	reg_t position = argv[1]; /* source */
	int index = UKPV(2);
	char *source;
	char *str_base = target;
	int mode = 0;
	int paramindex = 0; /* Next parameter to evaluate */
	char xfer;
	int i;
	int startarg;
	int str_leng = 0; /* Used for stuff like "%13s" */
	int unsigned_var = 0;
	int maxsize = 4096; /* Arbitrary... */


	if (position.segment)
		startarg = 2;
	else
		startarg = 3; /* First parameter to use for formatting */

	source = kernel_lookup_text(s, position, index);

	SCIkdebug(SCIkSTRINGS, "Formatting \"%s\"\n", source);


	arguments = sci_malloc(sizeof(int) * argc);
#ifdef SATISFY_PURIFY
	memset(arguments, 0, sizeof(int) * argc);
#endif

	for (i = startarg; i < argc; i++)
		arguments[i-startarg] = UKPV(i); /* Parameters are copied to prevent overwriting */

	while ((xfer = *source++)) {
		if (xfer == '%') {
			if (mode == 1) {
				CHECK_OVERFLOW1(target, 2);
				*target++ = '%'; /* Literal % by using "%%" */
				mode = 0;
			} else {
				mode = 1;
				str_leng = 0;
			}
		} else if (mode == 1) { /* xfer != '%' */
			char fillchar = ' ';
			int align = 0; /* -1: Left, +1: Right */

			char *writestart = target; /* Start of the written string, used after the switch */

			/* int writelength; -- unused atm */

			if (xfer && (isdigit(xfer) || xfer == '-')) {
				char *destp;

				if (xfer == '0')
					fillchar = '0';

				str_leng = strtol(source -1, &destp, 10);

				if (destp > source)
					source = destp;

				if (str_leng < 0) {
					align = -1;
					str_leng = -str_leng;
				} else
					align = 0;

				xfer = *source++;
			} else
				str_leng = 0;

			CHECK_OVERFLOW1(target, str_leng + 1);

			switch (xfer) {
			case 's': { /* Copy string */
				reg_t reg = argv[startarg + paramindex];
				char *tempsource = kernel_lookup_text(s, reg,
								      arguments[paramindex + 1]);
				int slen = strlen(tempsource);
				int extralen = str_leng - slen;
				CHECK_OVERFLOW1(target, extralen);

				if (reg.segment) /* Heap address? */
					paramindex++;
				else
					paramindex += 2; /* No, text resource address */

				if (align >= 0)
					while (extralen-- > 0)
						*target++ = ' '; /* Format into the text */

				strcpy(target, tempsource);
				target += slen;

				mode = 0;
			}
				break;

			case 'c': { /* insert character */
				CHECK_OVERFLOW1(target, 2);
				if (align >= 0)
					while (str_leng-- > 1)
						*target++ = ' '; /* Format into the text */

				*target++ = arguments[paramindex++];
				mode = 0;
			}
				break;

			case 'x':
			case 'u': unsigned_var = 1;
			case 'd': { /* Copy decimal */
				/* int templen; -- unused atm */
				char *format_string = "%d";

				if (xfer == 'x')
					format_string = "%x";

				if (!unsigned_var)
					if (arguments[paramindex] & 0x8000)
						/* sign extend */
						arguments[paramindex] = (~0xffff) | arguments[paramindex];

				target += sprintf(target, format_string, arguments[paramindex++]);
				CHECK_OVERFLOW1(target, 0);

				unsigned_var = 0;

				mode = 0;
			}
				break;
			default:
				*target = '%';
				target++;
				*target = xfer;
				target++;
				mode = 0;
			}

			if (align) {
				int written = target - writestart;
				int padding = str_leng - written;

				if (padding) {
					if (align > 0) {
						memmove(writestart + padding,
							writestart, padding);
						memset(writestart, fillchar, padding);
					} else {
						memset(target, ' ', padding);
						target += padding;
					}
				}
			}
		}else { /* mode != 1 */
			*target = xfer;
			target++;
		}
	}

	free(arguments);

	*target = 0; /* Terminate string */
	return dest; /* Return target addr */
}


void
kStrLen(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
	s->acc = strlen((char *) (s->heap + UPARAM(0)));
}


void
kGetFarText(state_t *s, int funct_nr, int argc, heap_ptr argp)
{
	resource_t *textres = scir_find_resource(s->resmgr, sci_text, UPARAM(0), 0);
	char *seeker;
	int counter = PARAM(1);


	if (!textres) {
		SCIkwarn(SCIkERROR, "text.%d does not exist\n", PARAM(0));
		return;
	}

	seeker = (char *) textres->data;

	while (counter--)
		while (*seeker++);
	/* The second parameter (counter) determines the number of the string inside the text
	** resource.
	*/

	s->acc = UPARAM(2);
	strcpy((char *) (s->heap + UPARAM(2)), seeker); /* Copy the string and get return value */
}

