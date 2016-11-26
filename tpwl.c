/* tpwl.c

   Turly's Tiny Powerline Shell for bash (ONLY) in straight C.
   (Very) loosely based on https://github.com/banga/powerline-shell
   -- with NO version control stuff, just basic prompt management.

   (C) 2016 Turly O'Connor.  No rights reserved, have at it :-)  */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

static void fatal (const char *fmt_str, ...) __attribute__ ((noreturn, format (printf, 1, 2)));
static const char TPWL_VERSION [] = "0.1";

/* By default, we say that bash/readline do NOT work properly with UTF-8.
   In that case we use some horrible bodgery to try to fix this - see
   strcpy_with_utf8_encoding () below.  */
static int bash_handles_utf8_p = 0;
static int spaced_p = 1;                /* Add extra spaces around certain items  */

/* These are the encodings for the various symbols we use in the prompts.  */
struct symbol_info_t {
    const char lock [4], network [4], sep [4], thin [4], ellipsis [4];
    int   ellipsis_width;                           /* In characters, not string bytes  */
};
enum symtype_t {SYM_COMPAT, SYM_ASCII = SYM_COMPAT, SYM_PATCHED, SYM_FLAT};
static const struct symbol_info_t info_symbols [] = {
    {"RO", "SSH", ">", ">", "...", 3},
    {"\xEE\x82\xA2", "\xEE\x82\xA2", "\xee\x82\xb0", "\xee\x82\xb1", "\xE2\x80\xA6", 1},
    {"", "", "", "", "", 0}
};

enum color_indices {    // vaguely based on Python powerline-shell's DefaultColor
    CI_NONE = 0,
    USERNAME_FG = 250,
    USERNAME_BG = 240,
    USERNAME_ROOT_BG = 124,

    HOSTNAME_FG = 250,
    HOSTNAME_BG = 238,

    HOME_BG = 31,       // blueish
    HOME_FG = 15,       // white
    PATH_BG = 32,       // bluey
    PATH_FG = 254,      // slightly-off white
    CWD_FG  = 255,      // whiter white
    SEPARATOR_FG = 250,

    SSH_BG = 166,       // medium orange
    SSH_FG = 254,

    CMD_PASSED_BG = 240,    // dark grey
    HISTORY_FG    = 251,    // greyish white
    CMD_PASSED_FG = 255,    // whiter white
    CMD_FAILED_BG = 161,
    CMD_FAILED_FG = 15,
};

static void append_fgcolor (char *buf, int code) { if (code != CI_NONE) sprintf (buf + strlen (buf), "\\e[38;5;%dm", code); }
static void append_bgcolor (char *buf, int code) { if (code != CI_NONE) sprintf (buf + strlen (buf), "\\e[48;5;%dm", code); }

static char             pline [8192];           /* "Big enough" (ahem) to avoid checks  */
static enum symtype_t   symtyp = SYM_PATCHED;   /* Assume patched fonts available - use --compat otherwise  */

#define MAXSEGS 64
struct segment_t {                              /* Individual segment ("chunk") of bash prompt  */
    uint8_t fgcolor, bgcolor;
    uint8_t sep_fg;
    char    sep [4];                            /* Must NOT be truncated */
    char    item [128 - (3 + 4)];               /* Could be truncated  */ 
};
static struct segs {
    struct segment_t segs [MAXSEGS];
    unsigned nsegs;
} pwl_segs;                                     /* All Powerline segments  */

/* Returns the length of the UTF-8 character encoded at STR.
   Only one UTF-8 character beginning at STR is examined.
   Returns 0 if string is empty, or 1 if the first char of STR
   is an ASCII char.  Else returns the length in bytes of the UTF-8
   character at STR.  */

static int get_char_len_utf8 (const char *str)
{
    const unsigned fc = * (uint8_t *) str;

    if (fc < 0x80)
        return (fc) ? 1 : 0;    /* null char: return 0, otherwise it's ASCII  */

    const unsigned nc = * (uint8_t *) (str + 1);
    if (fc >= 0xC2 && fc <= 0xDF && nc >= 0x80 && nc <= 0xBF)
        return 2;

    const unsigned xc = * (uint8_t *) (str + 2);

    /* XC, the third char, MUST always be 0x80 .. 0xBF  */
    if (xc < 0x80 || xc > 0xBF)
        return 1;               /* Nope: just say we're ASCII  */

    if (fc == 0xE0 && nc >= 0xA0 && nc <= 0xBF)
        return 3;

    if (fc == 0xED)
    {
        if (nc >= 0x80 && nc <= 0x9F)
            return 3;
    }
    else
    if (fc >= 0xE1 && fc <= 0xEF && nc >= 0x80 && nc <= 0xBF)
        return 3;

    /* 4-byte sequences begin with F0..F4  */
    const unsigned zc = * (uint8_t *) (str + 3);

    /* ZC, the fourth char, MUST always be 0x80 .. 0xBF  */
    if (zc < 0x80 || zc > 0xBF)
        return 1;               /* Nope: just say we're ASCII  */

    if (fc == 0xF0 && nc >= 0x90 && nc <= 0xBF)
        return 4;
    if (fc >= 0xF1 && fc <= 0xF3 && nc >= 0x80 && nc <= 0xBF)
        return 4;
    if (fc == 0xF4 && nc >= 0x80 && nc <= 0xBF)
        return 4;

    /* Otherwise we just say it's a 1-byte ASCII character.  */
    return 1;
}                                           /* get_char_len_utf8 ()  */

/* Bash PS1 prompt handling doesn't seem to grok UTF8 characters and
   ends up getting the prompt width wrong, affecting screen redrawing.
   We work around this by writing a space ' ' followed by 
          \[ BACKSPACE UTF8-CHAR-BYTES \] 
   Bash excludes what's in the \[ ... \] brackets from prompt length 
   calculations.  
   Returns 1 if we ended the D string with a \[ ... \] sequence (caller
   might be able to use this info to avoid immediately adding another
   \[ if another nonprintable is being added.)

   Grossly inefficient, be careful out there!
   XXX Maybe a better solution would be to use tput?  */

static int strcpy_with_utf8_encoding (char *d, const char *str)
{
    int last_was_esc_p = 0;
    int len;

    if (bash_handles_utf8_p)                    /* User has a bash/readline that groks UTF-8  */
    {
        strcpy (d, str);
        return 0;
    }

    while ((len = get_char_len_utf8 (str)) > 0) /* LEN normally 1 unless we're at a UTF-8 char  */
    {
        if (len > 1)                            /* UTF-8  */
        {   
            *d++ = ' ';                         /* ONE space  */
            *d++ = '\\'; *d++ = '[';            /* Bash begin sequence of nonprinting characters  */
            *d++ = '\\'; 
            *d++ = '0'; *d++ = '1'; *d++ = '0'; /* ONE Octal BACKSPACE ^H 010  */
            while (len--)
                *d++ = *str++;                  /* Copy UTF8 sequence  */
            *d++ = '\\'; *d++ = ']';            /* Bash end sequence of nonprinting characters  */
            last_was_esc_p = 1;
        }
        else                                    /* ASCII  */
        {
            *d++ = *str++;
            last_was_esc_p = 0;
        }
    }
    *d = 0;                                     /* Ensure string is terminated  */
    return last_was_esc_p;
}                                               /* strcpy_with_utf8_encoding ()  */
                                                    
/* Extended append an item to the PS1 segment list  - specifies everything!  */
static void xappend (struct segs *s, const char *item, int fg, int bg, const char *sep, int sep_fg)
{
    assert (s->nsegs < MAXSEGS);
    struct segment_t *sp = s->segs + s->nsegs++;
    sp->fgcolor = fg;
    sp->bgcolor = bg;
    sp->sep_fg = sep_fg;

    assert (strlen (sep) < sizeof (sp->sep));           /* MUST have room to store separators  */
    strcpy (sp->sep, sep);

    strncpy (sp->item, item, sizeof (sp->item) - 1);    /* Item text, don't care so much...  */
    sp->item [sizeof (sp->item) - 1] = 0;
}

/* Append an item to the PS1 segment list - will use default separator  */
static void append (struct segs *s, const char *item, int fg, int bg)
{
    xappend (s, item, fg, bg, info_symbols [symtyp].sep, bg);
}

/* Prints our various segments into the string which will eventually 
   be used as a bash PS1 prompt. 
   TITLE will be non-null if we're to set the window's title too.  */
static char *drawsegs (char *line, const struct segs *s, const char *title)
{
    unsigned ix;
    unsigned last_fg = CI_NONE, last_bg = CI_NONE;  /* Try to optimise  */
    int      last_was_escape_p = 0;
    char     colors [256];

    line [0] = 0;
    for (ix = 0; ix < s->nsegs; ++ix)
    {
        const struct segment_t  *sp = s->segs + ix;

        colors [0] = 0;
        if (sp->fgcolor != last_fg)
            append_fgcolor (colors, last_fg = sp->fgcolor);
        if (sp->bgcolor != last_bg)
            append_bgcolor (colors, last_bg = sp->bgcolor);

        /* If we added nonprintable stuff, escape them from bash.
           If last_was_escape_p is true, we overwrite the previously-emitted
           closing \] and thereby extend the previous \[ ... \] nonprintable
           escape sequence.  */

        if (colors [0])
        {
            char *lp = line + strlen (line) - ((last_was_escape_p) ? 2 : 0);
            sprintf (lp, "%s%s\\]", (last_was_escape_p) ? "" : "\\[", colors);
        }

        /* This adds the actual text - which could have UTF8 encodings and so
           end up with a bash nonprintable escape sequence.  */

        last_was_escape_p = strcpy_with_utf8_encoding (line + strlen (line), sp->item);

        /* Now add any final colours - also nonprintable  */
        colors [0] = 0;
        if (ix < s->nsegs - 1)
        {
            const struct segment_t *next = sp + 1;
            if (next->bgcolor != last_bg)
                append_bgcolor (colors, last_bg = next->bgcolor);
        }
        else                                        /* Last segment  */
        {
            strcat (colors, "\\e[0m");              /* Reset all attributes  */
            last_fg = last_bg = CI_NONE;
        }
        if (sp->sep_fg != last_fg && sp->sep [0])
            append_fgcolor (colors, last_fg = sp->sep_fg);

        if (colors [0])
        {
            char *lp = line + strlen (line) - ((last_was_escape_p) ? 2 : 0);
            sprintf (lp, "%s%s\\]", (last_was_escape_p) ? "" : "\\[", colors);
        }

        last_was_escape_p = strcpy_with_utf8_encoding (line + strlen (line), sp->sep);
    }

    /* Add any color resets and optionally set the terminal window title.
       These aren't bash-printable and should be enclosed in \[ ... \]  */

    colors [0] = 0;
    if (last_fg != CI_NONE || last_bg != CI_NONE)
        strcat (colors, "\\e[0m");                  /* Reset all attributes  */

    if (title != NULL)                              /* Want terminal window title  */
    {
        strcat (colors, "\\e]0;");                  /* SET TERM TITLE Escape sequence  */

        if (*title == '^')                          /* Extra Title string comes first  */
        {
            strncat (colors, title + 1, 96);        /* skip the initial '^'  */
            strcat (colors, " - ");
            strcat (colors, "\\u@\\h: \\w");        /* bash 'user @ host  cwd' window title  */
        }
        else
        if (*title != 0)                            /* Extra title string appended to the window title  */
        {
            strcat (colors, "\\u@\\h: \\w");        /* bash 'user @ host  cwd' window title  */
            strcat (colors, " - ");
            strncat (colors, title, 96);
        }
        else                                        /* Default title  */
            strcat (colors, "\\u@\\h: \\w");        /* bash 'user @ host  cwd' window title  */

        strcat (colors, "\\a");                     /* Finish off SET TERM TITLE  */
    }
    if (colors [0])                                 /* Emit escaped string  */
    {
        char *lp = line + strlen (line) - ((last_was_escape_p) ? 2 : 0);
        sprintf (lp, "%s%s\\]", (last_was_escape_p) ? "" : "\\[", colors);
    }

    return line;
}                                                   /* drawsegs ()  */

static void add_host (struct segs *s, const char *host)
{
    if (host == NULL || *host == 0)
        host = (spaced_p) ? " \\h " : "\\h";        /* bash hostname  */
    append (s, host, HOSTNAME_FG, HOSTNAME_BG);
}
static void add_user (struct segs *s, const char *user)
{
    if (user == NULL || *user == 0)
        user = (spaced_p) ? " \\u " : "\\u";        /* bash user  */

    const char *env_user = getenv ("USER");         /* Check for root  */
    int root_p = (env_user && strcmp (env_user, "root") == 0);

    append (s, user, USERNAME_FG, (root_p) ? USERNAME_ROOT_BG : USERNAME_BG);
}

/* This is a monster.  Sorry.  */
/* CWD is the path to the working directory to display.
   We check the first element of CWD against HOMEDIR and substitute '~' if matched.
   If SPLIT_P is 1, we use the swanky Powerline-style directory element split
      cygdrive > c > Development > tpwl
   otherwise it's
      /cygdrive/c/Development/tpwl.
   CWD_MAX_DEPTH is the max number of pathname component dirs to display
                 - if negative, only lastmost dirs are displayed, otherwise
                 we try to display first and last directory names.
   MAX_DIR_LEN   is the max length of each individual pathname component and
                 must be at least the width of the ellipsis
   
   If SPLIT_P is zero, we sneakily say that if the length of CWD is less than
   (cwd_max_depth * MAX_DIR_LEN) then we can display the entire path with no truncation
   even if individual directories are longer than MAX_DIR_LEN or the number
   of directories exceeds cwd_max_depth.  */

static void add_cwd (struct segs *s, const char *cwd, const char *homedir, int cwd_max_depth, int max_dir_len, int split_p)
{
    if (cwd == NULL || *cwd == 0)
        return;

    if (homedir != NULL && homedir [0] != 0 && strncmp (cwd, homedir, strlen (homedir)) == 0)
    {
        append (s, (spaced_p) ? " ~ " : "~", HOME_FG, HOME_BG);
        cwd += strlen (homedir);
        if (*cwd == '/') ++cwd;
    }

    if (*cwd)
    {
        enum {MAXDIRS=80};
        const char *cp = cwd;
        const char *dirs [MAXDIRS];             /* Ludicrously large  */
        uint16_t    lens [MAXDIRS];
        uint8_t     using_p [MAXDIRS];
        int         totlen = strlen (cwd);
        int         ndirs = 0;
        int         entire_p = 0;
        int         dir_missing_ellipsis_needed_p = 0;
        int         ix;
        int         abs_max_depth = (cwd_max_depth < 0) ? - cwd_max_depth : cwd_max_depth;

        /* FIXME this only works for ASCII pathnames  */
        //fprintf (stderr, "CWD is '%s' (len %d), cwd_max_depth %d, max_dir_len %d\n", cwd, totlen, cwd_max_depth, max_dir_len);

        if (cp [0] != '/')                      /* Path doesn't start at /  */
            dirs [ndirs++] = cp;                /* So first element is always a dir  */

        while (*cp)                             /* Split into component directories  */
        {
            if (*cp == '/' && cp [1] != '/')    /* Double-slash counts as one  */
            {
                if (ndirs)
                    lens [ndirs - 1] = cp - dirs [ndirs - 1];
                dirs [ndirs++] = cp;
            }
            ++cp;
            if (ndirs >= MAXDIRS - 1)
                break;
        }
        if (ndirs)
            lens [ndirs - 1] = cp - dirs [ndirs - 1];

        /* If we're not splitting, and ALL the text fits, just spew it... up to final '/' anyway  */
        if (! split_p && totlen < abs_max_depth * max_dir_len) 
        {
            abs_max_depth = ndirs;
            entire_p = 1;
        }

        if (ndirs > abs_max_depth)              /* We'll be skipping at least one dir...  */
        {
            int avail = abs_max_depth - 1;
            int fx = 0, lx = ndirs - 1;

            memset (using_p, 0, ndirs);         /* Say we're not using any dir component  */

            /* We're going to use an ellipsis in place of missing directories
               in the displayed path (but only if we are displaying more than
               one directory.)   */
            dir_missing_ellipsis_needed_p = (abs_max_depth > 1);

            /* If CWD_MAX_DEPTH is negative, only lastmost dirs are used.
               Otherwise, it's organised as follows: LAST FIRST LAST-1 FIRST+1 ...  */
            do
            {
                using_p [lx--] = 1;
                if (--avail >= 0 && cwd_max_depth > 0)
                {
                    using_p [fx++] = 1;
                    --avail;
                }
            } while (avail >= 0);                
        }
        else
            memset (using_p, 1, ndirs);         /* Using all dirs  */

        for (ix = 0; ix < ndirs; ++ix)          /* Output the dirs we're using  */
        {
            if (using_p [ix])                   /* Using this one  */
            {
                const struct symbol_info_t *const si = info_symbols + symtyp;

                char        thisdir [1024];
                char        *tp = thisdir;
                int         thislen = lens [ix];
                const char  *component_ptr = dirs [ix];
                const int   last_p = (ix == ndirs - 1);
                int         fgcolor = (last_p) ? CWD_FG : PATH_FG;
                int         twiddle_bodge_p = 0;

                /* Special bodgery for '~' which did not get caught above  */
                if (ix == 0 && component_ptr [0] == '~')
                    twiddle_bodge_p = 1;

                *tp = 0;
                if (dir_missing_ellipsis_needed_p && ix > 0 && using_p [ix - 1] == 0)
                {
                    dir_missing_ellipsis_needed_p = 0;
                    strcpy (tp, si->ellipsis);      /* Don't UTF8-copy - that gets done when we draw the line  */
                    while (*tp)                     /* Skip to end of ellipsis  */
                        ++tp;
                }

                if (split_p || abs_max_depth == 1)
                {
                    if (component_ptr [0] == '/')   /* Toss slash  */
                    {
                        ++component_ptr;
                        --thislen;
                    }
                    if (spaced_p)
                        *tp++ = ' ';                /* Extra space if splitting components (or if only one!)  */
                }

                memcpy (tp, component_ptr, thislen);
                tp [thislen] = 0;                   /* truncate this path component (directory)  */
                //fprintf (stderr, "%d: '%s'  (len %d)\n", ix, thisdir, thislen);
                if (! entire_p && thislen > (max_dir_len+1))    /* +1 allows for  '/'  */
                    strcpy (tp + max_dir_len - si->ellipsis_width, si->ellipsis);
                else
                if ((last_p || split_p) && spaced_p)
                    strcat (thisdir, " ");          /* Extra space for split component  */

                if (twiddle_bodge_p)
                    xappend (s, thisdir, HOME_FG, HOME_BG, si->sep, HOME_BG);
                else
                if (split_p && ! last_p)
                    xappend (s, thisdir, fgcolor, PATH_BG, si->thin, SEPARATOR_FG);
                else
                    xappend (s, thisdir, fgcolor, PATH_BG, (last_p) ? si->sep : "", PATH_BG);
            }
        }                                           /* for (ndirs)  */
    }                                               /* if (*cwd)  */
}                                                   /* add_cwd ()  */

static int usage (int exit_code)
{
    printf ("Usage: tpwl OPTIONS [TEXT]\n");
    printf ("Tiny Powerline-style prompt for bash - set PS1 to resulting string\n");
    printf ("PS1 prompt is constructed in order of appearance of the following options\n");
    printf ("Order is important, e.g. you should have '--cwd-max-depth=N' before '--pwd'\n\n");
    printf (" --patched/--compat/--flat  Use patched Powerline fonts for prompt component\n"
            "                            separators, or ASCII versions, or no separators\n");
    printf (" --user[=BLAH]              Indicate user in PS1 (explicitly or bash '\\u')\n");
    printf (" --pwd[=PATH]               Indicate working dir in PS1 (implicitly '$PWD')\n");
    printf (" --plain                    Do not split working directory path a la Powerline\n");
    printf (" --host[=NAME]              Indicate hostname in PS1 (explicitly or bash '\\h')\n");
    printf (" --history                  Add bash command history number in prompt ('\\!')\n");
    printf (" --prompt=BLAH              Override PS1 bash prompt from default ('\\$')\n");
    printf (" --title[=XTEXT]            Set terminal title to \"user@host: $PWD [ - XTEXT]\"\n"
            "                            (if XTEXT begins with '^', add at start of title instead)\n");
    printf (" --ssh-[host|user|all]      If ssh is being used, add host / user / both to PS1\n");
    printf (" --ssh                      Tiny indication in PS1 if ssh is being used\n");
    printf (" --status=$?                Indicate status of last command\n");
    printf (" --home=PATH                If different from HOME env var, substitutes '~' in pwd\n"
            "                            Note: this arg should appear BEFORE '--pwd' arg\n");
    printf (" --cwd-max-depth=DEPTH      Maximum number of directories to show in path\n"
            "                            (if negative, only last DEPTH directories shown)\n");
    printf (" --cwd-max-dir-size=SIZE    Directory names longer than SIZE will be truncated\n");
    printf (" --fgbg=FGCOLOR:BGCOLOR     Set color indices to use for user items\n");
    printf (" --utf8-ok                  Do not use workarounds to fixup Bash prompt length\n");
    printf (" --tighten                  Don't add spaces around prompt components (shorter PS1)\n");
    printf (" --help                     Show this help and exit\n");
    printf ("\n");
    printf ("tpwl is (very) loosely based on https://github.com/banga/powerline-shell\n");
    printf ("Hacked together in C and implements ONLY the stuff that I use - no version control, etc.\n");
    printf ("Version %s, built on %s %s\n", TPWL_VERSION, __DATE__, __TIME__);
    printf ("\n");
    exit (exit_code);
}

static void fatal (const char *fmt_str, ...)
{
    va_list ap;
    
    va_start (ap, fmt_str);
    vfprintf (stderr, fmt_str, ap);
    va_end (ap);
    printf ("%s", "\\!\\$ ");                       /* print a default prompt  */
    exit (-1);
}
static inline int strbegins_p (const char *str, const char *start)
{
    return (strncmp (str, start, strlen (start)) == 0);
}

int main (int argc, const char *argv [])
{
    const int   ssh_p = (getenv ("SSH_CLIENT") != 0);
    int         bad_status_p = 0;
    const char  *prompt = 0;                        /* Defaults to history prompt  */
    const char  *homedir = NULL;
    struct segs *s = &pwl_segs;
    int         cwd_max_depth = 5;                  /* use ellipsis if #CWD dirs is > this  */
    int         cwd_max_dir_size = 10;              /* Max dir len for each dir in CWD  */
    const char  *title_extra = NULL;                /* Non-NULL if --title specified  */
    int         fancy_p = 1;                        /* Fancy directory splitting?  */
    int         history_p = 0;                      /* Include bash history cmd number in PS1?  */
    uint8_t     u_fg = PATH_FG, u_bg = PATH_BG;     /* User foreground / background colours  */
    int         ix;

    /* PS1 is built up in the order args are encountered, therefore the
       arg order is important - for example, --home=PATH should appear
       before --pwd (which is what "prints" the working directory.)  */

    for (ix = 1; ix < argc; ++ix)                   /* FIXME: Use optargs or something  */
    {
        const char *arg = argv [ix];

        //fprintf (stderr, "arg %d is '%s'\n", ix, arg);
        if (strcmp (arg, "--help") == 0 || strcmp (arg, "-h") == 0)
            usage (0);
        else
        if (strcmp (arg, "--bash-groks-utf8") == 0 || strcmp (arg, "--utf8-ok") == 0)
            bash_handles_utf8_p = 1;
        else
        if (strcmp (arg, "--version") == 0)
        {
            fprintf (stderr, "tpwl version %s built on %s %s\n", TPWL_VERSION, __DATE__, __TIME__);
            return 0;
        }
        else
        if (strbegins_p (arg, "--fgbg="))
        {
            int f, b;
            if (sscanf (arg, "--fgbg=%i:%i", &f, &b) != 2)
                fatal ("tpwl: can't parse arg: '%s' (expected --fgbg=INTEGER:INTEGER)\n", arg);
            if (f > 0 && f < 255) u_fg = f;     /* -1 will leave previous U_FG value  */
            if (b > 0 && b < 255) u_bg = b;
        }
        else
        if (strbegins_p (arg, "--cwd-max-depth="))
        {
            if (sscanf (arg, "--cwd-max-depth=%i", &cwd_max_depth) != 1)
                fatal ("tpwl: can't parse arg: '%s'\n", arg);
        }
        else
        if (strbegins_p (arg, "--cwd-max-dir-size="))
        {
            if (sscanf (arg, "--cwd-max-dir-size=%i", &cwd_max_dir_size) != 1)
                fatal ("tpwl: can't parse arg: '%s'\n", arg);
            if (cwd_max_dir_size < 4)
                fatal ("tpwl: %s specifies illegal size %d (min 4)\n", arg, cwd_max_dir_size);
        }
        else
        if (strcmp (arg, "--plain") == 0) fancy_p = 0;              /* No fancy > Powerline > path > splits  */ 
        else
        if (strcmp (arg, "--patched") == 0) symtyp = SYM_PATCHED;   /* Patched Powerline fonts available  */
        else
        if (strcmp (arg, "--compat") == 0) symtyp = SYM_COMPAT;     /* ASCII versions (">", "..." etc)  */
        else
        if (strcmp (arg, "--flat") == 0) symtyp = SYM_FLAT;         /* Nothing!  */
        else
        if (strcmp (arg, "--tighten") == 0) spaced_p = 0;
        else
        if (strcmp (arg, "--history") == 0) history_p = 1;
        else
        if (strcmp (arg, "--ssh") == 0 || strcmp (arg, "--ssh-all") == 0)  /* add whether we're ssh  */
        {
            if (ssh_p)                              /* Only done if SSH active  */
            {
                append (s, info_symbols [symtyp].network, SSH_FG, SSH_BG);
                if (strcmp (arg, "--ssh-all") == 0)
                {
                    add_user (s, NULL);
                    add_host (s, NULL);
                }
            }
        }
        else
        if (strbegins_p (arg, "--status="))
            bad_status_p = (arg [9] != '0' || arg [10] != 0);   /* Nonzero status of last command  */
        else
        if (strbegins_p (arg, "--home="))           /* in case different from getenv ("HOME")  */
            homedir = arg + 7;                      /* used to substitute '~' when printing cwd  */
        else
        if (strbegins_p (arg, "--cyg-home="))       /* Cygwin home dir bodge - ignored for non-cygwin  */
#ifdef __CYGWIN__
            homedir = arg + 11;                     /* Cygwin-only  */
#else
            ;
#endif
        else
        if (strcmp (arg, "--ssh-host") == 0 || strcmp (arg, "--sshhost") == 0)
        {
            if (ssh_p) add_host (s, NULL);          /* If we're SSH-ing, show host  */
        }
        else
        if (strcmp (arg, "--ssh-user") == 0 || strcmp (arg, "--sshuser") == 0)
        {
            if (ssh_p) add_user (s, NULL);          /* If we're SSH-ing, show user  */
        }
        else
        if (strbegins_p (arg, "--user"))            /* Can have explicit --user=foo or just --user to use bash \\u  */
            add_user (s, (arg [6] == '=') ? arg + 7 : NULL);
        else
        if (strbegins_p (arg, "--pwd"))             /* Can have explicit --pwd=path or just --pwd to use HOME env var  */
        {
            add_cwd (s,
                     (arg [5] == '=') ? arg + 6 : getenv ("PWD"),   /* the directory to display  */
                     (homedir) ? homedir : getenv ("HOME"), 
                     cwd_max_depth, cwd_max_dir_size, fancy_p);
        }
        else
        if (strbegins_p (arg, "--host"))            /* Can have explicit --host=name or just --host to use bash \\h  */
            add_host (s, (arg [6] == '=') ? arg + 7 : NULL);
        else
        if (strbegins_p (arg, "--title"))           /* Can have additional --title=EXTRA or just --title for default  */
            title_extra = (arg [7] == '=') ? arg + 8 : "";      /* Empty string for default window title  */
        else
        if (strbegins_p (arg, "--prompt="))         /* override default bash prompt  */
            prompt = arg + 9;
        else
        if (arg [0] == '-' && arg [1] == '-')       /* No idea.  */
            fatal ("tpwl: unknown arg '%s'\n", arg);
        else
        {
            char buf [512];
            if (spaced_p)
                snprintf (buf, sizeof (buf), " %s ", arg);
            append (s, (spaced_p) ? buf : arg, u_fg, u_bg);  /* Add arg as user text with user fg/bg  */
        }
    }

    if (history_p)
        xappend (s, (spaced_p) ? " \\! " : "\\!", HISTORY_FG, CMD_PASSED_BG, /*no sep*/"", CMD_PASSED_BG);

    if (prompt == 0)                                /* Using default prompt  */
    {
        prompt = (spaced_p) ? "\\$ " : "\\$";       /* Just $ or # if root  */
    }

    if (bad_status_p)
        append (s, prompt, CMD_FAILED_FG, CMD_FAILED_BG);
    else
        append (s, prompt, CMD_PASSED_FG, CMD_PASSED_BG);

    printf ("%s", drawsegs (pline, s, title_extra));
    return 0;
}



