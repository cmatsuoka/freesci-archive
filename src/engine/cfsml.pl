#! /usr/bin/perl
# The C File Storage Meta Language "reference" implementation
# This implementation is supposed to conform to version
$version = "0.8.1";
# of the spec. Please contact the maintainer if it doesn't.
#
# cfsml.pl Copyright (C) 1999, 2000, 2001 Christoph Reichenbach
#
#
# This program may be modified and copied freely according to the terms of
# the GNU general public license (GPL), as long as the above copyright
# notice and the licensing information contained herein are preserved.
#
# Please refer to www.gnu.org for licensing details.
#
# This work is provided AS IS, without warranty of any kind, expressed or
# implied, including but not limited to the warranties of merchantibility,
# noninfringement, and fitness for a specific purpose. The author will not
# be held liable for any damage caused by this work or derivatives of it.
#
# By using this source code, you agree to the licensing terms as stated
# above.
#
#
# Please contact the maintainer for bug reports or inquiries.
#
# Current Maintainer:
#
#    Christoph Reichenbach (CJR) [jameson@linuxgames.com]
#
#
# Warning: This is still a "bit" messy. Sorry for that.
#

#$debug = 1;

$type_integer = "integer";
$type_string = "string";
$type_record = "RECORD";
$type_pointer = "POINTER";
$type_abspointer = "ABSPOINTER";

%types;      # Contains all type bindings
%records;    # Contains all record bindings


sub create_string_functions
  {
    $firstline = __LINE__;
    $firstline += 4;
    print "#line $firstline \"cfsml.pl\"\n";
    print <<'EOF';

#include <stdarg.h> /* We need va_lists */

static void
_cfsml_error(char *fmt, ...)
{
  va_list argp;

  fprintf(stderr, "Error: ");
  va_start(argp, fmt);
  vfprintf(stderr, fmt, argp);
  va_end(argp);

}


static struct _cfsml_pointer_refstruct {
    struct _cfsml_pointer_refstruct *next;
    void *ptr;
} *_cfsml_pointer_references = NULL;

static struct _cfsml_pointer_refstruct **_cfsml_pointer_references_current = &_cfsml_pointer_references;

static char *_cfsml_last_value_retreived = NULL;
static char *_cfsml_last_identifier_retreived = NULL;

static void
_cfsml_free_pointer_references_recursively(struct _cfsml_pointer_refstruct *refs, int free_pointers)
{
    if (!refs)
	return;

    _cfsml_free_pointer_references_recursively(refs->next, free_pointers);

    if (free_pointers)
	free(refs->ptr);

    free(refs);
}

static void
_cfsml_free_pointer_references(struct _cfsml_pointer_refstruct **meta_ref, int free_pointers)
{
    _cfsml_free_pointer_references_recursively(*meta_ref, free_pointers);
    *meta_ref = NULL;
    _cfsml_pointer_references_current = meta_ref;
}

static struct _cfsml_pointer_refstruct **
_cfsml_get_current_refpointer()
{
    return _cfsml_pointer_references_current;
}

static void _cfsml_register_pointer(void *ptr)
{
    struct _cfsml_pointer_refstruct *newref = malloc(sizeof (struct _cfsml_pointer_refstruct));

    newref->next = *_cfsml_pointer_references_current;
    newref->ptr = ptr;
    *_cfsml_pointer_references_current = newref;
}


static char *
_cfsml_mangle_string(char *s)
{
  char *source = s;
  char c;
  char *target = (char *) malloc(1 + strlen(s) * 2); /* We will probably need less than that */
  char *writer = target;

  while ((c = *source++)) {

    if (c < 32) { /* Special character? */
      *writer++ = '\\'; /* Escape... */
      c += ('a' - 1);
    } else if (c == '\\' || c == '"')
      *writer++ = '\\'; /* Escape, but do not change */
    *writer++ = c;

  }
  *writer = 0; /* Terminate string */

  return (char *) realloc(target, strlen(target) + 1);
}


static char *
_cfsml_unmangle_string(char *s)
{
  char *target = (char *) malloc(1 + strlen(s));
  char *writer = target;
  char *source = s;
  char c;

  while ((c = *source++) && (c > 31)) {
    if (c == '\\') { /* Escaped character? */
      c = *source++;
      if ((c != '\\') && (c != '"')) /* Un-escape 0-31 only */
	c -= ('a' - 1);
    }
    *writer++ = c;
  }
  *writer = 0; /* Terminate string */

  return (char *) realloc(target, strlen(target) + 1);
}


static char *
_cfsml_get_identifier(FILE *fd, int *line, int *hiteof, int *assignment)
{
  char c;
  int mem = 32;
  int pos = 0;
  int done = 0;
  char *retval = (char *) malloc(mem);

  if (_cfsml_last_identifier_retreived) {
      free(_cfsml_last_identifier_retreived);
      _cfsml_last_identifier_retreived = NULL;
  }

  while (isspace(c = fgetc(fd)) && (c != EOF));
  if (c == EOF) {
    _cfsml_error("Unexpected end of file at line %d\n", *line);
    free(retval);
    *hiteof = 1;
    return NULL;
  }

  ungetc(c, fd);

  while (((c = fgetc(fd)) != EOF) && ((pos == 0) || (c != '\n')) && (c != '=')) {

     if (pos == mem - 1) /* Need more memory? */
       retval = (char *) realloc(retval, mem *= 2);

     if (!isspace(c)) {
        if (done) {
           _cfsml_error("Single word identifier expected at line %d\n", *line);
           free(retval);
           return NULL;
        }
        retval[pos++] = c;
     } else
        if (pos != 0)
           done = 1; /* Finished the variable name */
        else if (c == '\n')
           ++(*line);
  }

  if (c == EOF) {
    _cfsml_error("Unexpected end of file at line %d\n", *line);
    free(retval);
    *hiteof = 1;
    return NULL;
  }

  if (c == '\n') {
    ++(*line);
    if (assignment)
      *assignment = 0;
  } else
    if (assignment)
      *assignment = 1;

  if (pos == 0) {
    _cfsml_error("Missing identifier in assignment at line %d\n", *line);
    free(retval);
    return NULL;
  }

  if (pos == mem - 1) /* Need more memory? */
     retval = (char *) realloc(retval, mem += 1);

  retval[pos] = 0; /* Terminate string */
EOF

if ($debug) {
    print "  printf(\"idenditifier is '%s'\\n\", retval);\n";
}

  $firstline = __LINE__;
  $firstline += 4;
  print "#line $firstline \"cfsml.pl\"\n";
  print <<'EOF2';

  return _cfsml_last_identifier_retreived = retval;
}


static char *
_cfsml_get_value(FILE *fd, int *line, int *hiteof)
{
  char c;
  int mem = 64;
  int pos = 0;
  char *retval = (char *) malloc(mem);

  if (_cfsml_last_value_retreived) {
      free(_cfsml_last_value_retreived);
      _cfsml_last_value_retreived = NULL;
  }

  while (((c = fgetc(fd)) != EOF) && (c != '\n')) {

     if (pos == mem - 1) /* Need more memory? */
       retval = (char *) realloc(retval, mem *= 2);

     if (pos || (!isspace(c)))
        retval[pos++] = c;

  }

  while ((pos > 0) && (isspace(retval[pos - 1])))
     --pos; /* Strip trailing whitespace */

  if (c == EOF)
    *hiteof = 1;

  if (pos == 0) {
    _cfsml_error("Missing value in assignment at line %d\n", *line);
    free(retval);
    return NULL;
  }

  if (c == '\n')
     ++(*line);

  if (pos == mem - 1) /* Need more memory? */
    retval = (char *) realloc(retval, mem += 1);

  retval[pos] = 0; /* Terminate string */
EOF2

    if ($debug) {
	print "  printf(\"value is '%s'\\n\", retval);\n";
    }

    $firstline = __LINE__;
    $firstline += 4;
    print "#line $firstline \"cfsml.pl\"\n";
  print <<'EOF3';
  return (_cfsml_last_value_retreived = (char *) realloc(retval, strlen(retval) + 1));
  /* Re-allocate; this value might be used for quite some while (if we are
  ** restoring a string)
  */
}
EOF3
  }


# Call with $expression as a simple expression, like "tos + 1".
# Returns (in this case) ("tos", "-1").
sub lvaluize
  {
    my @retval;
#    print "//DEBUG: $expression [";
    my @tokens = split (/([+-\/\*])/, $expression);
#    print join(",", @tokens);
    $retval[0] = $tokens[0];

    my $rightvalue = "";
    for ($i = 1; $tokens[$i]; $i++) {

      if ($tokens[$i] eq "+") {
	$rightvalue .= "-";
      } elsif ($tokens[$i] eq "-") {
	$rightvalue .= "+";
      } elsif ($tokens[$i] eq "/") {
	$rightvalue .= "*";
      } elsif ($tokens[$i] eq "*") {
	$rightvalue .= "/";
      } else {
	$rightvalue .= $tokens[$i];
      }
    }

    $retval[1] = $rightvalue;

#   print "] => ($retval[0];$retval[1])\n";

    return @retval;
  }



sub create_declaration
  {
    $typename = $type;
    $ctype = $types{$type}->{'ctype'};

    if (not $types{$type}->{'external'}) {
      $types{$type}{'writer'} = "_cfsml_write_" . $typename;
      $types{$type}{'reader'} = "_cfsml_read_" . $typename;
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "static void\n$types{$type}{'writer'}(FILE *fh, $ctype* foo);\n";
      print "static int\n$types{$type}{'reader'}(FILE *fh, $ctype* foo, char *lastval,".
	" int *line, int *hiteof);\n\n";
    };

  }

sub create_writer
  {
    $typename = $type;
    $ctype = $types{$type}{'ctype'};

    print "#line ", __LINE__, " \"cfsml.pl\"\n";
    print "static void\n_cfsml_write_$typename(FILE *fh, $ctype* foo)\n{";
    print "\n  char *bar;\n  int min, max, i;\n\n";

    if ($types{$type}{'type'} eq $type_integer) {
      print "  fprintf(fh, \"%li\", (long) *foo);\n";
    }
    elsif ($types{$type}{'type'} eq $type_string) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "  if (!(*foo))\n";
      print "    fprintf(fh, \"\\\\null\\\\\");";
      print "  else {\n";
      print "    bar = _cfsml_mangle_string((char *) *foo);\n";
      print "    fprintf(fh, \"\\\"%s\\\"\", bar);\n";
      print "    free(bar);\n";
      print "  }\n";
    }
    elsif ($types{$type}{'type'} eq $type_record) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "  fprintf(fh, \"{\\n\");\n";

      for $n (@{$records{$type}}) {

	print "  fprintf(fh, \"$n->{'name'} = \");\n";

	if ($n->{'array'}) { # Check for arrays

	  if ($n->{'array'} eq 'static' or $n->{'size'} * 2) { # fixed integer value?
	    print "    min = max = $n->{'size'};\n";
	  }
	  else { # No, a variable
	    print "    min = max = foo->$n->{'size'};\n";
	  }

	  if ($n->{'maxwrite'}) { # A write limit?
	    print "    if (foo->$n->{'maxwrite'} < min)\n";
	    print "       min = foo->$n->{'maxwrite'};\n";
	  }

	  if ($n->{'array'} eq 'dynamic') {
	    print "    if (!foo->$n->{'name'})\n";
	    print "       min = max = 0; /* Don't write if it points to NULL */\n";
	  }

	  print "#line ", __LINE__, " \"cfsml.pl\"\n";
	  print "    fprintf(fh, \"[%d][\\n\", max);\n";
	  print "    for (i = 0; i < min; i++) {\n";
	  print "      $types{$n->{'type'}}{'writer'}";
	  my $subscribstr = "[i]"; # To avoid perl interpolation problems
	  print "(fh, &(foo->$n->{'name'}$subscribstr));\n";
	  print "      fprintf(fh, \"\\n\");\n";
	  print "    }\n";
	  print "    fprintf(fh, \"]\");\n";

	} elsif ($n->{'type'} eq $type_pointer) { # Relative pointer

	  print "    fprintf(fh, \"%d\", foo->$n->{'name'} - foo->$n->{'anchor'});" .
	    " /* Relative pointer */\n";

      } elsif ($n->{'type'} eq $type_abspointer) { # Absolute pointer

	  print "    if (!foo->$n->{'name'})\n";
	  print "      fprintf(fh, \"\\\\null\\\\\");";
	  print "    else \n";
	  print "      $types{$n->{'reftype'}}{'writer'}";
	  print "(fh, foo->$n->{'name'});\n";

	} else { # Normal record entry

	  print "    $types{$n->{'type'}}{'writer'}";
	  print "(fh, &(foo->$n->{'name'}));\n";

	}

	print "    fprintf(fh, \"\\n\");\n";
      }

      print "  fprintf(fh, \"}\");\n";
    }
    else {
      print STDERR "Warning: Attemt to create_writer for invalid type '$types{$type}{'type'}'\n";
    }
    print "}\n\n";

  }


sub create_reader
  {
    $typename = $type;
    $ctype = $types{$type}{'ctype'};

    print "#line ", __LINE__, " \"cfsml.pl\"\n";
    print "static int\n_cfsml_read_$typename";
    print "(FILE *fh, $ctype* foo, char *lastval, int *line, int *hiteof)\n{\n";

    print "  char *bar;\n  int min, max, i;\n";

    my $reladdress_nr = 0; # Number of relative addresses needed
    my $reladdress = 0; # Current relative address number
    my $reladdress_resolver = ""; # Relative addresses are resolved after the main while block

    if ($types{$type}{'type'} eq $type_record) {

      foreach $n (@{$records{$type}}) { # Count relative addresses we need
	if ($n->{'type'} eq $type_pointer) {
	  ++$reladdress_nr;
	}
      }

      if ($reladdress_nr) { # Allocate stack space for all relative addresses needed
	print "  int reladdresses[$reladdress_nr];\n";
      }
    }

    if ($types{$type}{'type'} eq $type_integer) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "\n  *foo = strtol(lastval, &bar, 0);\n";
      print "  if (*bar != 0) {\n";
      print "     _cfsml_error(\"Non-integer encountered while parsing int value at line %d\\n\",";
      print " *line);\n";
      print "     return CFSML_FAILURE;\n";
      print "  }\n";
      print "  return CFSML_SUCCESS;\n";
    } elsif ($types{$type}{'type'} eq $type_string) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "\n";
      print "  if (strcmp(lastval, \"\\\\null\\\\\")) { /* null pointer? */\n";
      print "    if (*lastval == '\"') { /* Quoted string? */\n";
      print "      int seeker = strlen(lastval);\n\n";
      print "      while (lastval[seeker] != '\"')\n";
      print "        --seeker;\n\n";
      print "      if (!seeker) { /* No matching double-quotes? */\n";
      print "        _cfsml_error(\"Unbalanced quotes at line %d\\n\", *line);\n";
      print "        return CFSML_FAILURE;\n";
      print "      }\n\n";
      print "      lastval[seeker] = 0; /* Terminate string at closing quotes... */\n";
      print "      lastval++; /* ...and skip the opening quotes locally */\n";
      print "    }\n";
      print "    *foo = _cfsml_unmangle_string(lastval);\n";
      print "    _cfsml_register_pointer(foo);\n";
      print "    return CFSML_SUCCESS;\n";
      print "  } else {\n";
      print "    *foo = NULL;\n";
      print "    return CFSML_SUCCESS;\n";
      print "  }\n";
    } elsif ($types{$type}{'type'} eq $type_record) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "  int assignment, closed, done;\n\n";
      print "  if (strcmp(lastval, \"{\")) {\n";
      print "     _cfsml_error(\"Reading record; expected opening braces in line %d, got \\\"%s\\\"\\n\",";
      print "line, lastval);\n";
      print "     return CFSML_FAILURE;\n";
      print "  };\n";
      print "  closed = 0;\n";
      print "  do {\n";
      print "    char *value;\n";
      print "    bar = _cfsml_get_identifier(fh, line, hiteof, &assignment);\n\n";
      print "    if (!bar)\n";
      print "       return CFSML_FAILURE;\n";
      print "    if (!assignment) {\n";
      print "      if (!strcmp(bar, \"}\")) \n";
      print "         closed = 1;\n";
      print "      else {\n";
      print "        _cfsml_error(\"Expected assignment or closing braces in line %d\\n\", *line);\n";
      print "        return CFSML_FAILURE;\n";
      print "      }\n";
      print "    } else {\n";
      print "      value = \"\";\n";
      print "      while (!value || !strcmp(value, \"\"))\n";
      print "        value = _cfsml_get_value(fh, line, hiteof);\n";
      print "      if (!value)\n";
      print "         return CFSML_FAILURE;\n";


      foreach $n (@{$records{$type}}) { # Now take care of all record elements

	my $type = $n->{'type'};
	my $reference = undef;
	if ($type eq $type_abspointer) {
	    $reference = 1;
	    $type = $n->{'reftype'};
	}
	my $name = $n->{'name'};
	my $reader = $types{$type}{'reader'};
	my $size = $n->{'size'};

	print "      if (!strcmp(bar, \"$name\")) {\n";

	if ($type eq $type_pointer) { # A relative pointer

	  $reader = $types{'int'}{'reader'}; # Read relpointer as int

	  print "#line ", __LINE__, " \"cfsml.pl\"\n";
	  print "         if ($reader(fh, &(reladdresses[$reladdress]), value, line, hiteof))\n";
	  print "            return CFSML_FAILURE;\n";

	  # Make sure that the resulting variable is interpreted correctly
	  $reladdress_resolver .= "  foo->$n->{'name'} =".
	    " foo->$n->{'anchor'} + reladdresses[$reladdress];\n";

	  ++$reladdress; # Prepare reladdress for next element

	} elsif ($n->{'array'}) { # Is it an array?
	  print "#line ", __LINE__, " \"cfsml.pl\"\n";
	  print "         if ((value[0] != '[') || (value[strlen(value) - 1] != '[')) {\n";
	  # The value must end with [, since we're starting array data, and it must also
	  # begin with [, since this is either the only character in the line, or it starts
	  # the "amount of memory to allocate" block
	  print "            _cfsml_error(\"Opening brackets expected at line %d\\n\", *line);\n";
	  print "            return CFSML_FAILURE;\n;";
	  print "         }\n";

	  if ($n->{'array'} eq 'dynamic') {
	    print "#line ", __LINE__, " \"cfsml.pl\"\n";
	    # We need to allocate the array first
	    print "         /* Prepare to restore dynamic array */\n";
	    # Read amount of memory to allocate
	    print "         max = strtol(value + 1, NULL, 0);\n";
	    print "         if (max < 0) {\n";
	    print "            _cfsml_error(\"Invalid number of elements to allocate for dynamic ";
	    print "array '%s' at line %d\\n\", bar, *line);\n";
	    print "            return CFSML_FAILURE;\n;";
	    print "         }\n\n";

	    print "         if (max) {\n";
	    print "           foo->$name = ($n->{'type'} *) malloc(max * sizeof($type));\n";
	    print "           _cfsml_register_pointer(foo->$name);\n";
	    print "         }\n";
	    print "         else\n";
	    print "           foo->$name = NULL;\n"

	  } else { # static array
	    print "         /* Prepare to restore static array */\n";
	    print "         max = $size;\n";
	  }

	  print "#line ", __LINE__, " \"cfsml.pl\"\n";
	  print "         done = i = 0;\n";
	  print "         do {\n";
	  if ($type eq $type_record) {
	    print "           if (!(value = _cfsml_get_value(fh, line, hiteof)))\n";
	  } else {
	    print "           if (!(value = _cfsml_get_identifier(fh, line, hiteof, NULL)))\n";
	  }
	  print "#line ", __LINE__, " \"cfsml.pl\"\n";
	  print "              return 1;\n";
	  print "           if (strcmp(value, \"]\")) {\n";
	  print "             if (i == max) {\n";
	  print "               _cfsml_error(\"More elements than space available (%d) in '%s' at ";
	  print "line %d\\n\", max, bar, *line);\n";
	  print "               return CFSML_FAILURE;\n";
	  print "             }\n";
	  my $helper = "[i++]";
	  print "             if ($reader(fh, &(foo->$name$helper), value, line, hiteof))\n";
	  print "                return CFSML_FAILURE;\n";
	  print "           } else done = 1;\n";
	  print "         } while (!done);\n";

	  if ($n->{'array'} eq "dynamic") {
	    my @xpr = lvaluize($expression = $n->{'size'});
	    print "         foo->$xpr[0] = max $xpr[1]; /* Set array size accordingly */\n";
	  }

	  if ($n->{'maxwrite'}) {
	    my @xpr = lvaluize($expression = $n->{'maxwrite'});
	    print "         foo->$xpr[0] = i $xpr[1]; /* Set number of elements */\n";
	  }

	}
	elsif ($reference) {
	  print "#line ", __LINE__, " \"cfsml.pl\"\n";
	  print "        if (strcmp(value, \"\\\\null\\\\\")) { /* null pointer? */\n";
	  print "           foo->$name = malloc(sizeof ($type));\n";
	  print "           _cfsml_register_pointer(foo->$name);\n";
	  print "           if ($reader(fh, foo->$name, value, line, hiteof))\n";
	  print "              return CFSML_FAILURE;\n";
	  print "        } else foo->$name = NULL;\n";
	}
	else { # It's a simple variable or a struct
	  print "#line ", __LINE__, " \"cfsml.pl\"\n";
	  print "         if ($reader(fh, &(foo->$name), value, line, hiteof))\n";
	  print "            return CFSML_FAILURE;\n";
	}
	print "      } else\n";

      }
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "       {\n";
      print "          _cfsml_error(\"Assignment to invalid identifier '%s' in line %d\\n\",";
      print " bar, *line);\n";
      print "          return CFSML_FAILURE;";
      print "       }\n";
      print "     }\n";

      print "  } while (!closed); /* Until closing braces are hit */\n";

      print $reladdress_resolver; # Resolves any relative addresses

      print "  return CFSML_SUCCESS;\n";
    } else {
      print STDERR "Warning: Attempt to create_reader for invalid type '$types{$type}{'type'}'\n";
    }

    print "}\n\n";
  }

# Built-in types

%types = (
	  'int' => {
		    'type' => $type_integer,
		    'ctype' => "int",
		   },

	  'string' => {
		       'type' => $type_string,
		       'ctype' => "char *",
		      },
	 );



sub create_function_block {
  print "\n/* Auto-generated CFSML declaration and function block */\n\n";
  print "#line ", __LINE__, " \"cfsml.pl\"\n";
  print "#define CFSML_SUCCESS 0\n";
  print "#define CFSML_FAILURE 1\n\n";
  create_string_functions;

  foreach $n ( keys %types ) {
    create_declaration($type = $n);
  }

  foreach $n ( keys %types ) {
    if (not $types{$n}->{'external'}) {
      create_writer($type = $n);
      create_reader($type = $n);
    }
  }
  print "\n/* Auto-generated CFSML declaration and function block ends here */\n";
  print "/* Auto-generation performed by cfsml.pl $version */\n";
}


# Gnerates code to read a data type
# Parameters: $type: Type to read
#             $datap: Pointer to the write destination
#             $fh: Existing filehandle of an open file to use
#             $eofvar: Variable to store _cfsml_eof into
sub insert_reader_code {
  print "/* Auto-generated CFSML data reader code */\n";
  print "#line ", __LINE__, " \"cfsml.pl\"\n";
  print "  {\n";
  if (!$linecounter) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "    int _cfsml_line_ctr = 0;\n";
      $linecounter = '_cfsml_line_ctr';
  }
  if ($atomic) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "    struct _cfsml_pointer_refstruct **_cfsml_myptrrefptr = _cfsml_get_current_refpointer();\n";
  }
  print "#line ", __LINE__, " \"cfsml.pl\"\n";
  print "    int _cfsml_eof = 0, _cfsml_error;\n";
  print "    int dummy;\n";

  if ($firsttoken) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "    char *_cfsml_inp = $firsttoken;\n";
  } else {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "    char *_cfsml_inp =".
	  " _cfsml_get_identifier($fh, &($linecounter), &_cfsml_eof, &dummy);\n\n";
  }

  print "#line ", __LINE__, " \"cfsml.pl\"\n";
  print "    _cfsml_error =".
      " $types{$type}{'reader'}($fh, $datap, _cfsml_inp, &($linecounter), &_cfsml_eof);\n";

  if ($eofvar) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "    $eofvar = _cfsml_error;\n";
  }
  if ($atomic) {
      print "#line ", __LINE__, " \"cfsml.pl\"\n";
      print "     _cfsml_free_pointer_references(_cfsml_myptrrefptr, _cfsml_error);\n";
  }
  print "#line ", __LINE__, " \"cfsml.pl\"\n";
  print "     if (_cfsml_last_value_retreived) {\n";
  print "       free(_cfsml_last_value_retreived);\n";
  print "       _cfsml_last_value_retreived = NULL;\n";
  print "     }\n";
  print "     if (_cfsml_last_identifier_retreived) {\n";
  print "       free(_cfsml_last_identifier_retreived);\n";
  print "       _cfsml_last_identifier_retreived = NULL;\n";
  print "     }\n";
  print "  }\n";
  print "/* End of auto-generated CFSML data reader code */\n";
}

# Generates code to write a data type
# Parameters: $type: Type to write
#             $datap: Pointer to the write destination
#             $fh: Existing filehandle of an open file to use
sub insert_writer_code {
  print "#line ", __LINE__, " \"cfsml.pl\"\n";
  print "/* Auto-generated CFSML data writer code */\n";
  print "  $types{$type}{'writer'}($fh, $datap);\n";
  print "  fprintf($fh, \"\\n\");\n";
  print "/* End of auto-generated CFSML data writer code */\n";
}


################
# Main program #
################

$parsing = 0;
$struct = undef; # Not working on a struct
$commentmode = undef;
$line = 0;

while (<STDIN>) {

  $line++;

  if ($parsing) {
    ($data) = split "#"; # Remove shell-style comments
    @_ = ($data);

    s/\/\*.*\*\///g; # Remove C-style one-line comments

    ($data) = split "\/\/"; # Remove C++-style comments
    @_ = ($data);

    if ($commentmode) {

      if (grep /\*\//, $_) {
	($empty, $_) = split /\*\//;
      } else {
	@_ = (); # Empty line
      }

    } else {
      if (grep /\/\*/, $_) {
	$commentmode = 1;
	($_) = split /\/\*/;
      }
    }


    # Now tokenize:
    s/;//;
    split /(\".*\"|[,\[\]\(\)\{\}])|\s+/;

    @items = @_;

    @tokens = ();

    $tokens_nr = 0;
    for ($n = 0; $n < scalar @items; $n++) { # Get rid of all undefs
      if ($_[$n]) {
	$_ = $items[$n];
	s/\"//g;
	$tokens[$tokens_nr++] = $_;
      }
    }

    # Now all tokens are in @tokens, and we have $tokens_nr of them.

#    print "//DEBUG: " . join ("|", @tokens) . "\n";

    if ($tokens_nr) {
      if ($tokens_nr == 2 && $tokens[0] eq "%END" && $tokens[1] eq "CFSML") {

	$struct && die "Record $struct needs closing braces in intput file (line $line).";

	$parsing = 0;
	create_function_block;
	my $linep = $line + 1;
	print "#line $linep \"CFSML input file\"";
      } elsif ($struct) { # Parsing struct
	if ($tokens_nr == 1) {
	  if ($tokens[0] eq "}") {
	    $struct = undef;
	  } else { die "Invalid declaration of $token[0] in input file (line $line)\n";};
	} else { # Must be a member declaration

	  my @structrecs = (@{$records{$struct}});
	  my $newidx = (scalar @structrecs) or "0";
	  my %member = ();
	  $member{'name'} = $tokens[1];
	  $member{'type'} = $tokens[0];

	  if ($tokens_nr == 3 && $tokens[1] == "*") {
	      $tokens_nr = 2;
	      $member{'name'} = $tokens[2];
	      $member{'reftype'} = $tokens[0];
	      $member{'type'} = $type_abspointer;
	  }

	  if ($tokens_nr == 4 and $tokens[0] eq $type_pointer) { # Relative pointer

	    if (not $tokens[2] eq "RELATIVETO") {
	      die "Invalid relative pointer declaration in input file (line $line)\n";
	    }

	    $member{'anchor'} = $tokens[3]; # RelPointer anchor

	  } else { # Non-pointer

	    if (not $types{$tokens[0]}) {
	      die "Unknown type $tokens[0] used in input file (line $line)\n";
	    }

	    if ($tokens_nr > 2) { # Array

	      if ($tokens[2] ne "[") {
		die "Invalid token '$tokens[2]' in input file (line $line)\n";
	      }

	      $member{'array'} = "static";

	      if ($tokens[$tokens_nr - 1] ne "]") {
		die "Array declaration incorrectly terminated in input file (line $line)\n";
	      }

	      $parsepos = 3;

	      while ($parsepos < $tokens_nr) {

		if ($tokens[$parsepos] eq ",") {

		  $parsepos++;

		} elsif ($tokens[$parsepos] eq "STATIC") {

		  $member{'array'} = "static";
		  $parsepos++;

		} elsif ($tokens[$parsepos] eq "DYNAMIC") {

		  $member{'array'} = "dynamic";
		  $parsepos++;

		} elsif ($tokens[$parsepos] eq "MAXWRITE") {

		  $member{'maxwrite'} = $tokens[$parsepos + 1];
		  $parsepos += 2;

		} elsif ($tokens[$parsepos] eq "]") {

		  $parsepos++;
		  if ($parsepos != $tokens_nr) {
		    die "Error: Invalid tokens after array declaration in input file (line $line)\n";

		  }
		} else {

		  if ($member{'size'}) {
		    die "Attempt to use more than one array size in input file (line $line)\n" .
		      "(Original size was \"$member->{'size'}\", new size is \"$tokens[$parsepos]\"\n";
		  }

		  $member{'size'} = $tokens[$parsepos];
		  $parsepos++;
		}
	      }


	      unless ($member{'size'}) {
		die "Array declaration without size in input file (line $line)\n";
	      }
	    }
	  }

	  @{$records{$struct}}->[$newidx] = \%member;
	}
      } else { # not parsing struct; normal operation.

	if ($tokens[0] eq "TYPE") { # Simple type declaration

	  my $newtype = $tokens[1];

	  $types{$newtype}->{'ctype'} = $tokens[2];

	  if ($tokens_nr == 5) { # must be ...LIKE...

	    unless ($tokens[3] eq "LIKE") {
	      die "Invalid TYPE declaration in input file (line $line)\n";
	    }

	    $types{$newtype}->{'type'} = $types{$tokens[4]}->{'type'};
	    $types{$newtype}->{'reader'} = $types{$tokens[4]}->{'reader'};
	    $types{$newtype}->{'writer'} = $types{$tokens[4]}->{'writer'};

	  } elsif ($tokens_nr == 6) { # must be ...USING...

	    unless ($tokens[3] eq "USING") {
	      die "Invalid TYPE declaration in input file (line $line)\n";
	    }

	    $types{$newtype}->{'writer'} = $tokens[4];
	    $types{$newtype}->{'reader'} = $tokens[5];
	    $types{$newtype}->{'external'} = 'T';

	  } else {
	    die "Invalid TYPE declaration in input file (line $line)\n";
	  }

	} elsif ($tokens[0] eq "RECORD") {

	  $struct = $tokens[1];
	  if ($types{$struct}) {
	    die "Attempt to re-define existing type $struct as a struct in input file (line $line)";
	  }
	  $types{$struct}{'type'} = $type_record;
	  if ($tokens_nr < 3 or $tokens_nr > 6 or $tokens[$tokens_nr - 1] ne "{") {
	    die "Invalid record declaration in input file (line $line)";
	  }

	  my $extoffset = 2;

	  if ($tokens_nr > 3) {
	      if ($tokens[2] ne "EXTENDS") { # Record declaration with explicit c type
		  $types{$struct}{'ctype'} = $tokens[2];
		  $extoffset = 3;
	      } else { # Record name is the same as the c type name
		  $types{$struct}{'ctype'} = $struct;
	      }
	  } elsif ($tokens_nr == 3) {
		  $types{$struct}{'ctype'} = $struct;
	  }

	  if (($tokens_nr > $extoffset + 1) && ($extoffset + 1 <= $tokens_nr)) {
	      if ($tokens[$extoffset] ne "EXTENDS") {
		  die "Invalid or improper keyword \"$tokens[$extoffset]\" in input file (line $line)";
	      }
	      if ($extoffset + 2 >= $tokens_nr) {
		  die "RECORD \"$struct\" extends on unspecified type in input file (line $line)";
	      }
	      my $ext_type = $tokens[$extoffset + 1];

	      if (!($types{$ext_type}{type} eq $type_record)) {
		  print "$types{$ext_type}{type}";
		  die "RECORD \"$struct\" attempts to extend non-existing or non-record type \"$ext_type\" in input file (line $line)";
	      }

	      (@{$records{$struct}}) = (@{$records{$ext_type}}); # Copy type information from super type
	  }

	} else {
	  die "Invalid declaration \"$tokens[0]\" in line $line";
	}
      }
    }


  } else {

    ($subtoken) = split ";"; # Get rid of trailing ;s
    $tokens_nr = @tokens = split " ", $subtoken;

    if ($tokens_nr == 1 && $tokens[0] eq "%CFSML") {

      $parsing = 1;

    } elsif ($tokens[0] eq "%CFSMLWRITE" and $tokens[3] eq "INTO" and $tokens_nr >= 5) {

      insert_writer_code($type = $tokens[1], $datap = $tokens[2], $fh = $tokens[4]);
      my $templine = $line + 1;
      print "#line $templine \"CFSML input file\"\n"; # Yes, this sucks.

    } elsif (($tokens[0] eq "%CFSMLREAD") or ($tokens[0] eq "%CFSMLREAD-ATOMIC") and $tokens[3] eq "FROM" and $tokens_nr >= 5) {

      my $myeofvar = 0;
      my $myfirsttoken = 0;
      my $mylinecounter = 0;

      my $idcounter = 5;

      while ($idcounter < $tokens_nr) {
	if ($tokens[$idcounter] eq "ERRVAR" and $tokens_nr >= $idcounter + 2) {
	  $myeofvar = $tokens[$idcounter + 1];
	  $idcounter += 2;
	} elsif ($tokens[$idcounter] eq "FIRSTTOKEN" and $tokens_nr >= $idcounter + 2) {
	  $myfirsttoken = $tokens[$idcounter + 1];
	  $idcounter += 2;
	} elsif ($tokens[$idcounter] eq "LINECOUNTER" and $tokens_nr >= $idcounter + 2) {
	  $mylinecounter = $tokens[$idcounter + 1];
	  $idcounter += 2;
	} else {
	  die "Unknown %CFSMLREAD operational token: $tokens[$idcounter]\n";
	}
      }
      insert_reader_code($type = $tokens[1], $datap = $tokens[2],
			 $fh = $tokens[4], $eofvar = $myeofvar, $firsttoken = $myfirsttoken,
			$linecounter = $mylinecounter, $atomic = ($tokens[0] eq "%CFSMLREAD-ATOMIC"));
      my $templine = $line + 1;
      print "#line $templine \"CFSML input file\"\n"; # Yes, this sucks, too.

    } else {
      print;
    }
  }

}

if ($parsing) {
  print <STDERR>, "Warning: Missing %END CFSML\n";
}
