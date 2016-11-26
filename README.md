# tpwl

*tpwl* is turly's _Tiny Powerline_-style prompt for bash (only.)
![Example](tpwl.jpg)

Set `PS1` to the resulting string and you'll get a Powerline-style bash prompt.

tpwl is loosely based on the Python-based [powerline shell](https://github.com/banga/powerline-shell) 
but was hacked together in C and implements _only_ the stuff that I use - no built-in version control info, etc.

For best results, install and use one of the [patched Powerline fonts](https://github.com/powerline/fonts) (I use Anonymice).

_**tpwl**_:
* builds the PS1 string in the order given by its arguments.  Some arguments need to appear before others 
  as they'll affect the appearance of the later args.
* allows setting of the terminal window's title 
* works around bash / readline UTF-8 bugs is prompt length calculation but this can be turned off.
* allows arbitrary text (including UTF-8 characters) in arbitrary colors to be added to the prompt, 
  see the excerpt from my _.bashrc_ below.


## Installation
Download tpwl.c and do
```bash
cc -Wall -Wextra -Werror tpwl.c -o tpwl
```
Put the binary somewhere reachable - mine's in /usr/local/bin/tpwl.

## Checking it works and experimenting with it

In bash, type
```bash
PS1=$(tpwl --ssh-all --history --pwd --status=$? --title)
```
and note the changes in your prompt.  `tpwl --help` will show you all the options.

## Usage

If you're happy with it, you could add something like this to your _.bashrc_:
```bash
function _update_ps1() {
    PS1="$($TPWL --history --ssh-all --cwd-max-depth=-4 --cwd-max-dir-size=10 --pwd --status=$? --title)"
}
if [[ "$PROMPT_COMMAND" != *_update_ps1* ]]; then   # doesn't already contain _update_ps1
    PROMPT_COMMAND="_update_ps1; $PROMPT_COMMAND"
```

This will arrange for the bash function `_update_ps1` to be called every time bash outputs a prompt.
`update_ps1` just calls sets `PS1` to whatever string is printed by the _tpwl_ invocation.

Here's what's in my _.bashrc_, a bit more complicated as I have different 
parameters depending on whether I'm in a Clearcase view, plus I don't want
to call tpwl every time as it's on a network drive.  To this end it relies
on other code setting the `PS1_NEEDS_UPDATE_P` shell variable
- look at the `cd` function for example.  Note that because _tpwl_ is not
called every time, I don't use the `--status=$?` arg.

```bash

function cd () {                    # This just sets PS1_NEEDS_UPDATE_P
    PS1_NEEDS_UPDATE_P=1
    builtin cd "$@"
}

function reset_ps1() {              # In case of screw-up - type reset_ps1
    PS1_NEEDS_UPDATE_P=1
    PROMPT_COMMAND=""
    PS1="\\W \\! \\$ "              # Revert to simple prompt
}
PS1="[\\W]\\! \\$ "                 # Simple prompt by default


if [ "$TERM" != "linux" ]; then                         # not Linux console
  TPWL="~/bin/$OSTYPE/tpwl"
  if [ -x "$TPWL" ]; then
    TPWL_ARGS="--history"                               # --tighten --plain
    if [ "$CLEARCASE_VIEW" != "" ]; then                # Special update_ps1 for Clearcase view
        function _update_ps1() {
          if [ $PS1_NEEDS_UPDATE_P -eq 1 ] ; then
            #branch_str=$'\xee\x82\xa0'                  # Powerline font's BRANCH glyph U+E0A0
            #branch_str="$branch_str "                   # space after
            PS1_NEEDS_UPDATE_P=0
            PS1="$($TPWL $TPWL_ARGS --ssh-all --fgbg=240:123 $CLEARCASE_VIEW \
                   --fgbg=240:6 " $branch_str$CLEARCASE_BRANCH" \
                   --cwd-max-depth=-4 --cwd-max-dir-size=10 --pwd --title=^$CLEARCASE_VIEW)"
          fi
        }
    else                                                # no CLEARCASE_VIEW
        function _update_ps1() {
          if [ $PS1_NEEDS_UPDATE_P -eq 1 ] ; then
            PS1_NEEDS_UPDATE_P=0
            PS1="$($TPWL $TPWL_ARGS --ssh-all --cwd-max-depth=-4 --cwd-max-dir-size=10 --pwd --title)"
          fi
        }
    fi                                                  # CLEARCASE_VIEW
    if [[ "$PROMPT_COMMAND" != *_update_ps1* ]]; then   # doesn't already contain _update_ps1
        PROMPT_COMMAND="_update_ps1; $PROMPT_COMMAND"
    fi
  fi                                                    # TPWL
fi                                                      # $TERM
```

## Arguments
```
$ tpwl --help
Usage: tpwl OPTIONS [TEXT]
Tiny Powerline-style prompt for bash - set PS1 to resulting string
PS1 prompt is constructed in order of appearance of the following options
Order is important, e.g. you should have '--cwd-max-depth=N' before '--pwd'

 --patched/--compat/--flat  Use patched Powerline fonts for prompt component
                            separators, or ASCII versions, or no separators
 --user[=BLAH]              Indicate user in PS1 (explicitly or bash '\u')
 --pwd[=PATH]               Indicate working dir in PS1 (implicitly '$PWD')
 --plain                    Do not split working directory path a la Powerline
 --host[=NAME]              Indicate hostname in PS1 (explicitly or bash '\h')
 --history                  Add bash command history number in prompt ('\!')
 --prompt=BLAH              Override PS1 bash prompt from default ('\$')
 --title[=XTEXT]            Set terminal title to "user@host: $PWD [ - XTEXT]"
                            (if XTEXT begins with '^', add at start of title instead)
 --ssh-[host|user|all]      If ssh is being used, add host / user / both to PS1
 --ssh                      Tiny indication in PS1 if ssh is being used
 --status=$?                Indicate status of last command
 --home=PATH                If different from HOME env var, substitutes '~' in pwd
                            Note: this arg should appear BEFORE '--pwd' arg
 --cwd-max-depth=DEPTH      Maximum number of directories to show in path
                            (if negative, only last DEPTH directories shown)
 --cwd-max-dir-size=SIZE    Directory names longer than SIZE will be truncated
 --fgbg=FGCOLOR:BGCOLOR     Set color indices to use for user items
 --utf8-ok                  Do not use workarounds to fixup Bash prompt length
 --tighten                  Don't add spaces around prompt components (shorter PS1)
 --help                     Show this help and exit

tpwl is (very) loosely based on https://github.com/banga/powerline-shell
Hacked together in C and implements ONLY the stuff that I use - no version control, etc.
```

