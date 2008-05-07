/**********************************************************************

  markdown.c - markdown in C using a PEG grammar. 
  (c) 2008 John MacFarlane (jgm at berkeley dot edu).

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

 ***********************************************************************/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "markdown_peg.h"
#include "my_getopt-1.5/getopt.h"

extern char *strdup(const char *string);
static int extensions;

/**********************************************************************

  Utility functions for printing

 ***********************************************************************/

static int padded = 2;      /* Number of newlines after last output.
                               Starts at 2 so no newlines are needed at start.
                               */

/* pad - add newlines if needed */
void pad(int num) {
    while (num-- > padded)
        printf("\n");
    padded = num;
}

/**********************************************************************

  Functions for printing Elements as HTML

 ***********************************************************************/

/* print_html_string - print string, escaping for HTML  
 * If obfuscate selected, convert characters to hex or decimal entities at random */
void print_html_string(char *str, bool obfuscate) {
    while (*str != '\0') {
        switch (*str) {
        case '&':
            printf("&amp;");
            break;
        case '<':
            printf("&lt;");
            break;
        case '>':
            printf("&gt;");
            break;
        case '"':
            printf("&quot;");
            break;
        default:
            if (obfuscate) {
                if (rand() % 2 == 0)
                    printf("&#%d;", *str);
                else
                    printf("&#x%x;", *str);
            }
            else
                putchar(*str);
            break;
        }
    str++;
    }
}

void print_html_element(element elt, bool obfuscate);

/* print_html_element_list - print a list of elements as HTML */
void print_html_element_list(item *list, bool obfuscate) {
    while (list != NULL) {
        print_html_element((*list).val, obfuscate);
        list = (*list).next;
    }
}

/* print_html_element - print an element as HTML */
void print_html_element(element elt, bool obfuscate) {
    int lev;
    char *contents;
    switch (elt.key) {
    case SPACE:
        printf("%s", elt.contents.str);
        break;
    case LINEBREAK:
        printf("<br/>");
        break;
    case STR:
        print_html_string(elt.contents.str, obfuscate);
        break;
    case ELLIPSIS:
        printf("&hellip;");
        break;
    case CODE:
        printf("<code>");
        print_html_string(elt.contents.str, obfuscate);
        printf("</code>");
        break;
    case HTML:
        printf(elt.contents.str);
        break;
    case LINK:
        if (strstr(elt.contents.link.url, "mailto:") == elt.contents.link.url)
            obfuscate = true;  /* obfuscate mailto: links */
        printf("<a href=\"");
        print_html_string(elt.contents.link.url, obfuscate);
        printf("\"");
        if (strlen(elt.contents.link.title) > 0) {
            printf(" title=\"");
            print_html_string(elt.contents.link.title, obfuscate);
            printf("\"");
        }
        printf(">");     
        print_html_element_list(elt.contents.link.label, obfuscate);
        printf("</a>");
        break;
    case IMAGE:
        printf("<img src=\"");
        print_html_string(elt.contents.link.url, obfuscate);
        printf("\" alt=\"");
        print_html_element_list(elt.contents.link.label, obfuscate);
        printf("\"");
        if (strlen(elt.contents.link.title) > 0) {
            printf(" title=\"");
            print_html_string(elt.contents.link.title, obfuscate);
            printf("\"");
        }
        printf(" />");
        break;
    case EMPH:
        printf("<em>");
        print_html_element_list(elt.contents.list, obfuscate);
        printf("</em>");
        break;
    case STRONG:
        printf("<strong>");
        print_html_element_list(elt.contents.list, obfuscate);
        printf("</strong>");
        break;
    case LIST:
        print_html_element_list(elt.contents.list, obfuscate);
        break;
    case H1: case H2: case H3: case H4: case H5: case H6:
        lev = elt.key - H1 + 1;  /* assumes H1 ... H6 are in order */
        pad(2);
        printf("<h%1d>", lev);
        print_html_element_list(elt.contents.list, obfuscate);
        printf("</h%1d>", lev);
        padded = 0;
        break;
    case PLAIN:
        pad(1);
        print_html_element_list(elt.contents.list, obfuscate);
        padded = 0;
        break;
    case PARA:
        pad(2);
        printf("<p>");
        print_html_element_list(elt.contents.list, obfuscate);
        printf("</p>");
        padded = 0;
        break;
    case HRULE:
        pad(2);
        printf("<hr />");
        padded = 0;
        break;
    case HTMLBLOCK:
        pad(2);
        printf(elt.contents.str);
        padded = 0;
        break;
    case VERBATIM:
        pad(2);
        printf("<pre><code>");
        print_html_string(elt.contents.str, obfuscate);
        printf("</code></pre>");
        padded = 0;
        break;
    case BULLETLIST:
        pad(2);
        printf("<ul>");
        padded = 0;
        print_html_element_list(elt.contents.list, obfuscate);
        pad(1);
        printf("</ul>");
        padded = 0;
        break;
    case ORDEREDLIST:
        pad(2);
        printf("<ol>");
        padded = 0;
        print_html_element_list(elt.contents.list, obfuscate);
        pad(1);
        printf("</ol>");
        padded = 0;
        break;
    case LISTITEM:
        pad(1);
        printf("<li>");
        padded = true;
        /* \001 is used to indicate boundaries between nested lists when there
         * is no blank line.  We split the string by \001 and parse
         * each chunk separately. */
        contents = strtok(elt.contents.str, "\001");
        print_html_element(markdown(contents, extensions), obfuscate);
        while ((contents = strtok(NULL, "\001")))
            print_html_element(markdown(contents, extensions), obfuscate);
        printf("</li>");
        padded = 0;
        break;
    case BLOCKQUOTE:
        pad(2);
        printf("<blockquote>");
        print_html_element(markdown(elt.contents.str, extensions), obfuscate);
        printf("</blockquote>");
        padded = 0;
        break;
    case REFERENCE:
        /* Nonprinting */
        break;
    default: 
        fprintf(stderr, "print_html_element encountered unknown element key = %d\n", elt.key); 
        exit(EXIT_FAILURE);
        break;
    }
}

/**********************************************************************

  Functions for printing Elements as LaTeX

 ***********************************************************************/

/* print_latex_string - print string, escaping for LaTeX */
void print_latex_string(char *str) {
    while (*str != '\0') {
        switch (*str) {
          case '{': case '}': case '$': case '%':
          case '&': case '_': case '#':
            printf("\\%c", *str);
            break;
        case '^':
            printf("\\^{}");
            break;
        case '\\':
            printf("\\textbackslash{}");
            break;
        case '~':
            printf("\\ensuremath{\\sim}");
            break;
        case '|':
            printf("\\textbar{}");
            break;
        case '<':
            printf("\\textless{}");
            break;
        case '>':
            printf("\\textgreater{}");
            break;
        default:
            putchar(*str);
            break;
        }
    str++;
    }
}

void print_latex_element(element elt);

/* print_latex_element_list - print a list of elements as LaTeX */
void print_latex_element_list(item *list) {
    while (list != NULL) {
        print_latex_element((*list).val);
        list = (*list).next;
    }
}

/* print_latex_element - print an element as LaTeX */
void print_latex_element(element elt) {
    int lev;
    int i;
    char *contents;
    switch (elt.key) {
    case SPACE:
        printf("%s", elt.contents.str);
        break;
    case LINEBREAK:
        printf("\\\\\n");
        break;
    case STR:
        print_latex_string(elt.contents.str);
        break;
    case ELLIPSIS:
        printf("\\ldots{}");
        break;
    case CODE:
        printf("\\texttt{");
        print_latex_string(elt.contents.str);
        printf("}");
        break;
    case HTML:
        /* don't print HTML */
        break;
    case LINK:
        printf("\\href{%s}{", elt.contents.link.url);
        print_latex_element_list(elt.contents.link.label);
        printf("}");
        break;
    case IMAGE:
        printf("\\includegraphics{%s}", elt.contents.link.url);
        break;
    case EMPH:
        printf("\\emph{");
        print_latex_element_list(elt.contents.list);
        printf("}");
        break;
    case STRONG:
        printf("\\textbf{");
        print_latex_element_list(elt.contents.list);
        printf("}");
        break;
    case LIST:
        print_latex_element_list(elt.contents.list);
        break;
    case H1: case H2: case H3:
        lev = elt.key - H1 + 1;  /* assumes H1 ... H6 are in order */
        printf("\\");
        for (i = elt.key; i > H1; i--)
            printf("sub");
        printf("section{");
        print_latex_element_list(elt.contents.list);
        printf("}\n\n");
        break;
    case H4: case H5: case H6:
        printf("\\noindent\\textbf{");
        print_latex_element_list(elt.contents.list);
        printf("}\n");
    case PLAIN:
        print_latex_element_list(elt.contents.list);
        printf("\n");
        break;
    case PARA:
        print_latex_element_list(elt.contents.list);
        printf("\n\n"); 
        break;
    case HRULE:
        printf("\\begin{center}\\rule{3in}{0.4pt}\\end{center}\n");
        break;
    case HTMLBLOCK:
        /* don't print HTML block */
        break;
    case VERBATIM:
        printf("\\begin{verbatim}\n");
        print_latex_string(elt.contents.str);
        printf("\n\\end{verbatim}\n");
        break;
    case BULLETLIST:
        printf("\\begin{itemize}\n");
        padded = 0;
        print_latex_element_list(elt.contents.list);
        printf("\\end{itemize}\n\n");
        break;
    case ORDEREDLIST:
        printf("\\begin{enumerate}\n");
        print_latex_element_list(elt.contents.list);
        printf("\\end{enumerate}\n\n");
        break;
    case LISTITEM:
        printf("\\item ");
        /* \001 is used to indicate boundaries between nested lists when there
         * is no blank line.  We split the string by \001 and parse
         * each chunk separately. */
        contents = strtok(elt.contents.str, "\001");
        print_latex_element(markdown(contents, extensions));
        while ((contents = strtok(NULL, "\001")))
            print_latex_element(markdown(contents, extensions));
        break;
    case BLOCKQUOTE:
        printf("\\begin{quote}");
        print_latex_element(markdown(elt.contents.str, extensions));
        printf("\\end{quote}\n\n");
        break;
    case REFERENCE:
        /* Nonprinting */
        break;
    default: 
        fprintf(stderr, "print_latex_element encountered unknown element key = %d\n", elt.key); 
        exit(EXIT_FAILURE);
        break;
    }
}

/**********************************************************************

  Functions for printing Elements as groff (mm macros)

 ***********************************************************************/

static bool in_list_item = false; /* True if we're parsing contents of a list item. */

/* print_groff_string - print string, escaping for groff */
void print_groff_string(char *str) {
    while (*str != '\0') {
        switch (*str) {
        case '\\':
            printf("\\e");
            break;
        default:
            putchar(*str);
            break;
        }
    str++;
    }
}

void print_groff_mm_element(element elt, int count);

/* print_groff_mm_element_list - print a list of elements as groff ms */
void print_groff_mm_element_list(item *list) {
    int count = 1;
    while (list != NULL) {
        print_groff_mm_element((*list).val, count);
        list = (*list).next;
        count++;
    }
}

/* print_groff_mm_element - print an element as groff ms */
void print_groff_mm_element(element elt, int count) {
    int lev;
    char *contents;
    switch (elt.key) {
    case SPACE:
        printf("%s", elt.contents.str);
        padded = 0;
        break;
    case LINEBREAK:
        pad(1);
        printf(".br");
        padded = 0;
        break;
    case STR:
        print_groff_string(elt.contents.str);
        padded = 0;
        break;
    case ELLIPSIS:
        printf("...");
        break;
    case CODE:
        printf("\\fC");
        print_groff_string(elt.contents.str);
        printf("\\fR");
        padded = 0;
        break;
    case HTML:
        /* don't print HTML */
        break;
    case LINK:
        print_groff_mm_element_list(elt.contents.link.label);
        printf(" (%s)", elt.contents.link.url);
        padded = 0;
        break;
    case IMAGE:
        printf("[IMAGE: ");
        print_groff_mm_element_list(elt.contents.link.label);
        printf("]");
        padded = 0;
        /* not supported */
        break;
    case EMPH:
        printf("\\fI");
        print_groff_mm_element_list(elt.contents.list);
        printf("\\fR");
        padded = 0;
        break;
    case STRONG:
        printf("\\fB");
        print_groff_mm_element_list(elt.contents.list);
        printf("\\fR");
        padded = 0;
        break;
    case LIST:
        print_groff_mm_element_list(elt.contents.list);
        padded = 0;
        break;
    case H1: case H2: case H3: case H4: case H5: case H6:
        lev = elt.key - H1 + 1;
        pad(1);
        printf(".H %d \"", lev);
        print_groff_mm_element_list(elt.contents.list);
        printf("\"");
        padded = 0;
        break;
    case PLAIN:
        pad(1);
        print_groff_mm_element_list(elt.contents.list);
        padded = 0;
        break;
    case PARA:
        pad(1);
        if (!in_list_item || count != 1)
            printf(".P\n");
        print_groff_mm_element_list(elt.contents.list);
        padded = 0;
        break;
    case HRULE:
        pad(1);
        printf("\\l'\\n(.lu*8u/10u'");
        padded = 0;
        break;
    case HTMLBLOCK:
        /* don't print HTML block */
        break;
    case VERBATIM:
        pad(1);
        printf(".VERBON 2\n");
        print_groff_string(elt.contents.str);
        printf(".VERBOFF");
        padded = 0;
        break;
    case BULLETLIST:
        pad(1);
        printf(".BL");
        padded = 0;
        print_groff_mm_element_list(elt.contents.list);
        pad(1);
        printf(".LE 1");
        padded = 0;
        break;
    case ORDEREDLIST:
        pad(1);
        printf(".AL");
        padded = 0;
        print_groff_mm_element_list(elt.contents.list);
        pad(1);
        printf(".LE 1");
        padded = 0;
        break;
    case LISTITEM:
        pad(1);
        printf(".LI\n");
        in_list_item = true;
        /* \001 is used to indicate boundaries between nested lists when there
         * is no blank line.  We split the string by \001 and parse
         * each chunk separately. */
        contents = strtok(elt.contents.str, "\001");
        padded = 2;
        print_groff_mm_element(markdown(contents, extensions), 1);
        while ((contents = strtok(NULL, "\001"))) {
            padded = 2;
            print_groff_mm_element(markdown(contents, extensions), 1);
        }
        in_list_item = false;
        break;
    case BLOCKQUOTE:
        pad(1);
        printf(".DS I\n");
        padded = 2;
        print_groff_mm_element(markdown(elt.contents.str, extensions), 1);
        pad(1);
        printf(".DE");
        padded = 0;
        break;
    case REFERENCE:
        /* Nonprinting */
        break;
    default: 
        fprintf(stderr, "print_groff_mm_element encountered unknown element key = %d\n", elt.key); 
        exit(EXIT_FAILURE);
        break;
    }
}


/**********************************************************************

  Main program and auxiliary functions.

  Reads input from files on command line, or from stdin
  if no arguments given.  Converts tabs to spaces using TABSTOP.
  Parses the result for references (References), and then again for
  conversion to HTML (Doc).  The parser actions print the converted
  HTML, so there is no separate printing step here.  Character encodings
  are ignored.

 ***********************************************************************/

#define VERSION "0.1"
#define COPYRIGHT "Copyright (c) 2008 John MacFarlane.\n" \
                  "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n" \
                  "This is free software: you are free to change and redistribute it.\n" \
                  "There is NO WARRANTY, to the extent permitted by law."

#define TABSTOP 4
#define INCREMENT 4096  /* size of chunks in which to allocate memory */

/* print version and copyright information */
void version(char *progname)
{
  printf("%s version %s\n"
         "%s\n",
         progname,
         VERSION,
         COPYRIGHT);
}

/* print a help summary */
void help(char *progname)
{
  printf("Usage: %s [options] [FILE]...\n"
         "Options:\n"
         "-t FORMAT or --to FORMAT        convert to FORMAT (default is html)\n"
         "                                FORMAT = html|latex|groff-mm\n"
         "-o FILE or --output FILE        send output to FILE (default is stdout)\n"
         "-x[EXTS] or --extensions [EXTS] use syntax extensions (all if EXTS not specified)\n"
         "                                EXTS = smart, ...\n"
         "-V or --version                 print program version and exit\n"
         "-h or --help                    show this message and exit\n",
         progname);
}

/* print usage information to stderr */
void usage(char *progname)
{
  fprintf(stderr,
          "Summary: %s [--help] [--version] [options] [FILE]...\n",
          progname);
}

int main(int argc, char * argv[]) {

    char *progname = argv[0];
    int opt;
    /* the output filename is initially 0 (a.k.a. stdout) */
    char *outfilename = 0;
    char *format = 0;
    char *exts = 0;

    /* Output formats. */
    enum formats { HTML_FORMAT,
                   LATEX_FORMAT,
                   GROFF_MM_FORMAT
                 };

 
    int output_format = HTML_FORMAT;

    char *shortopts = "Vhx::o:t:";
    /* long options list */
    struct option longopts[] =
    {
      /* name,        has_arg,           flag, val */ /* longind */
      { "version",    no_argument,       0,    'V' }, /*       0 */
      { "help",       no_argument,       0,    'h' }, /*       1 */
      { "output",     required_argument, 0,    'o' }, /*       2 */
      { "to",         required_argument, 0,    't' }, /*       3 */
      { "extensions", optional_argument, 0,    'x' }, /*       3 */
      /* end-of-list marker */
      { 0, 0, 0, 0 }
    };
    /* long option list index */
    int longind = 0;
    
    extensions = 0;

    /* parse all options from the command line */
    while ((opt = getopt_long_only(argc, argv, shortopts, longopts, &longind)) != -1)
        switch (opt) {
        case 'V': /* -version */
            version(progname);
            return 0;
        case 'h': /* -help */
            help(progname);
            return 0;
        case 'x': /* -extended */
            exts = optarg;
            if (exts == NULL) {
                extensions = 0xFFFFFF;  /* turn on all extensions */
                break;
            }
            exts = strtok(optarg, ",");
            while (exts != NULL) {
                if (strcmp(exts, "smart") == 0)
                    extensions = extensions | EXT_SMART;
                else {
                    fprintf(stderr, "%s: Unknown extension '%s'\n", progname, exts);
                    exit(EXIT_FAILURE);
                }   
                exts = strtok(NULL, ",");
            }
            break;
        case 't': /* -to */
            format = optarg;
            if (strcmp(format, "html") == 0)
                output_format = HTML_FORMAT;
            else if (strcmp(format, "latex") == 0)
                output_format = LATEX_FORMAT;
            else if (strcmp(format, "groff-mm") == 0)
                output_format = GROFF_MM_FORMAT;
            else {
                fprintf(stderr, "%s: Unknown output format '%s'\n", progname, format);
                exit(EXIT_FAILURE);
            }   
            break;
        case 'o': /* -output=FILE */
            outfilename = optarg;
            /* we allow "-" as a synonym for stdout here */
            if (! strcmp(optarg, "-"))
            {
                outfilename = 0;
            }
            break;
        default: /* something unexpected has happened */
            usage(progname);
            return 1;
        }

    /* re-open stdout to outfilename, if requested */
    if (outfilename)
        if (! freopen(outfilename, "w", stdout)) {
            perror(outfilename);
            return 1;
        }

    int numargs;            /* number of filename arguments */
    FILE *inputs[argc];     /* array of file pointers for inputs */
    int lastinp;            /* index of last file in inputs */
    int i;
    int buflength = 0;
    int maxlength = INCREMENT;
    char *inputbuf;
    inputbuf = malloc(INCREMENT);
    FILE *input;
    char *curchar = inputbuf;
    int charstotab;
     
    numargs = argc - optind; 
    if (numargs == 0) {        /* use stdin if no files specified */
       inputs[0] = stdin;
       lastinp = 0;
    }
    else {                  /* open all the files on command line */
       for (i = 0; i < numargs; i++)
            if ((inputs[i] = fopen(argv[optind + i], "r")) == NULL) {
                perror(argv[optind + i]);
                exit(EXIT_FAILURE);
            }
       lastinp = i - 1;
    }
   
    for (i=0; i <= lastinp; i++) {
        input = inputs[i];
        charstotab = TABSTOP;
        while ((*curchar = fgetc(input)) != EOF) {
            switch (*curchar) {
            case '\t':
                while (charstotab > 0)
                   *curchar = ' ', curchar++, buflength++, charstotab--; 
                break;
            case '\n':
                curchar++, buflength++, charstotab = TABSTOP;
                break;
            default:
                curchar++, buflength++, charstotab--; 
            }
            if (charstotab == 0)
                charstotab = 4;
            if (buflength > maxlength - TABSTOP - 3) {
                maxlength += INCREMENT;
                inputbuf = realloc(inputbuf, maxlength);
                curchar = inputbuf + buflength;
                if (inputbuf == NULL) {
                    perror(progname);
                    exit(EXIT_FAILURE);
                }
            }
        }
        fclose(input);
    }

    strcat(inputbuf, strdup("\n\n"));   /* add newlines to end to match Markdown.pl behavior */
   
    element parsed_input = markdown(inputbuf, extensions);

    switch (output_format) {
    case HTML_FORMAT:
        print_html_element(parsed_input, false);
        break;
    case LATEX_FORMAT:
        print_latex_element(parsed_input);
        break;
    case GROFF_MM_FORMAT:
        print_groff_mm_element(parsed_input, 1);
        break;
    default:
        fprintf(stderr, "print_element - unknown format = %d\n", output_format); 
        exit(EXIT_FAILURE);
        break;
    }

    printf("\n");

    return(EXIT_SUCCESS);
}

