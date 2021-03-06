# tpwl

*tpwl* is turly's themeable _Tiny Powerline_-style prompt for bash.
It's fast, written in plain old C, and works well even on an old 80MHz Linux box. 

Set `PS1` to the resulting string and you'll get a Powerline-style bash prompt.

![Example](example.jpg)


tpwl is loosely based on the Python-based [powerline shell](https://github.com/banga/powerline-shell) 
but was hacked together in C and implements _only_ the stuff that I use - no built-in version control info, etc.

For best results, install and use one of the [patched Powerline fonts](https://github.com/powerline/fonts) (I use Anonymice).

_**tpwl**_:
* builds the PS1 string in the order given by its arguments.  Some arguments need to appear before others 
  as they'll affect the appearance of the later args
* allows setting of the terminal window's title 
* is themeable
* works around bash / readline UTF-8 bugs is prompt length calculation but this can be turned off (off by default on Macs)
* allows arbitrary text (including UTF-8 characters) in arbitrary colors to be added to the prompt, 
  see the excerpt from my _.bashrc_ below


## Installation
Using git is highly recommended:
```bash
git clone https://github.com/turly/tpwl
cd tpwl
cc -O2 -Wall -Wextra -Werror tpwl.c -o tpwl
```
Or otherwise download tpwl.c, compile it as above, and put the _tpwl_ binary somewhere on your PATH.

## Checking it works and experimenting with it

Use `tpwl --help` to see the available options.  Assuming you are using a
[patched Powerline font](https://github.com/powerline/fonts), you can quickly test it out by doing

```bash
PS1=$(tpwl --ssh-all --hist --pwd --title)
```
and note the changes in your prompt and Terminal window title.  `tpwl --help` will show you all the options.

If you're not using a patched Powerline font, add `--ascii` as the first _tpwl_ arg.

## Usage

If you're happy with it, you could add something like this to your _.bashrc_:
```bash
function _update_ps1() {
    PS1="$(tpwl --hist --ssh-all --depth=-4 --dir-size=10 --pwd --status=$? --title)"
}
if [[ "$PROMPT_COMMAND" != *_update_ps1* ]]; then   # doesn't already contain _update_ps1
    PROMPT_COMMAND="_update_ps1; $PROMPT_COMMAND"
```

This will arrange for the bash function `_update_ps1` to be called every time bash outputs a prompt.
`update_ps1` just sets `PS1` to whatever string is printed by the _tpwl_ invocation.

Here's what's in my work _.bashrc_, slightly more complicated as I have different 
parameters depending on whether I'm in a Clearcase view.

```bash
function reset_ps1() {              # In case of screw-up - type reset_ps1
    PROMPT_COMMAND=""
    PS1="\\W \\! \\$ "              # Revert to simple prompt
}
PS1="[\\W]\\! \\$ "                 # Simple prompt by default

if [ "$TERM" != "linux" ]; then                         # not Linux console
  if hash tpwl 2>/dev/null; then                        # tpwl binary exists somewhere on PATH
    TPWL_ARGS="--hist --italic"                         # --tight --plain
    if [ "$CLEARCASE_VIEW" != "" ]; then                # Special update_ps1 for Clearcase view
        function _update_ps1() {
            #branch_str=$'\xee\x82\xa0'                  # Powerline font's BRANCH glyph U+E0A0
            #branch_str="$branch_str "                   # space after
            PS1="$(tpwl $TPWL_ARGS --ssh-all --fb=240:123 $CLEARCASE_VIEW \
                   --fb=240:6 " $branch_str$CLEARCASE_BRANCH" --status=$? \
                   --depth=-4 --dir-size=10 --pwd --title=^$CLEARCASE_VIEW)"
        }
    else                                                # no CLEARCASE_VIEW
        function _update_ps1() {
            PS1="$(tpwl $TPWL_ARGS --status=$? --hist --ssh-all --depth=-4 --dir-size=10 --pwd --title)"
        }
    fi                                                  # CLEARCASE_VIEW
    if [[ "$PROMPT_COMMAND" != *_update_ps1* ]]; then   # doesn't already contain _update_ps1
        PROMPT_COMMAND="_update_ps1; $PROMPT_COMMAND"
    fi
  fi                                                    # TPWL
fi                                                      # $TERM
```

## Themes
_tpwl_ accepts a `--theme=COLORSTRING` argument, where COLORSTRING is a colon-separated list of xterm color indices 
(a bit like the `LS_COLORS` scheme used by `ls`.) Or it will use the `TPWL_COLORS` environment variable to the same effect.
_tpwl_ can visually dump the color scheme with the `--dump-theme` argument - note the order of the color indices in the string goes from USERNAME_FG ("username foreground color") to CMD_FAILED_BG ("command failed background color").

![dump-theme](dump-theme.jpg)

If you just want to change the HOSTNAME_FG / HOSTNAME_BG colors and leave the others at default values, you can do
```
export TPWL_COLORS=":::15:0"
```
which will set the hostname foreground to be 15 (white) and background to be zero (black.) Obviously you'd have to specify `--host` in your _tpwl_ invocation to see any effects.  A good source of info regarding xterm color indices is [available here](https://jonasjacek.github.io/colors/).

## Arguments
```
$ tpwl --help
Usage: tpwl OPTIONS [TEXT]
Tiny Powerline-style prompt for bash - set PS1 to resulting string
PS1 prompt is constructed in order of appearance of the following options
Order is important, e.g. place '--max-depth=N' before '--pwd'

 --patched|ascii|flat   Use patched Powerline fonts for prompt component
                        separators, or ASCII versions, or no separators
 --theme=COLORSTRING    Change the tpwl color scheme.  COLORSTRING is a colon-
                        separated list of xterm color indices.  Env var
                        TPWL_COLORS=COLORSTRING also works.  See also...
 --dump-theme           Dumps annotated current theme to stderr, and exits.
 --plain                Do not split working directory path a la Powerline
 --tight                Don't add spaces around prompt components (shorter PS1)
 --hist                 Add bash command history number in prompt ('\!')
 --status=$?            Indicate status of last command
 --depth=DEPTH          Maximum number of directories to show in path
                        (if negative, only last DEPTH directories shown)
 --dir-size=SIZE        Directory names longer than SIZE will be truncated
 --italic/--no-italic   Turn on/off italic mode.  Also -i/-I
 --[no-]utf8-ok         Do [not] use workarounds to fixup Bash prompt length
 --user[=BLAH]          Indicate user in PS1 (explicitly or bash '\u')
 --pwd[=PATH]           Indicate working dir in PS1 (implicitly '$PWD')
 --host[=NAME]          Indicate hostname in PS1 (explicitly or bash '\h')
 --prompt=BLAH          Override PS1 bash prompt from default ('\$')
 --title[=XTEXT]        Set terminal title to "ssh-user@ssh-host: $PWD [ - XTEXT]"
                        ssh-user@ssh-host appears only if ssh is being used.
                        if XTEXT begins with '^', add at start of title instead
 --ssh-[host|user|all]  Only if ssh is being used, add host/user/ both to PS1
 --ssh                  Tiny indication in PS1 if ssh is being used
 --home=PATH            If different from HOME env var, substitutes '~' in pwd
                        Note: this arg should appear BEFORE '--pwd' arg
 --fb=FGCOLOR:BGCOLOR   Set fore/back color indices to use for user items
                        (Negative index will leave color as it was)
 --help                 Show this help and exit

See tpwl project page at https://github.com/turly/tpwl
```

# License

_tpwl_ is (C) 2016-2018 Turly O'Connor and is MIT Licensed.  See the LICENSE file.

