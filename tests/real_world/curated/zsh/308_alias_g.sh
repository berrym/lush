#!/bin/zsh
## zsh `alias -g`: global aliases expand anywhere on the command line,
## not just at command position. Common idiom in oh-my-zsh /
## prezto-style configs for directory shortcuts (alias -g ...='../..').
##
## lush's implementation substitutes the alias at non-command argv
## positions as a single-slot text replacement. Structural-operator
## global aliases (`alias -g G='| grep'` introducing a real pipeline)
## are intentionally NOT supported -- that would require re-tokenizing
## the line after substitution. The directory-shortcut / text-shortcut
## cases (90% of real-world `alias -g` uses) are covered.

alias -g HELLO='hello world'
alias -g BAR=bar

echo prefix HELLO suffix
echo X BAR Y

## A nested global-alias use inside a quoted echo argument still
## substitutes (zsh expands global aliases on words, not on
## entire-line tokens). Lush matches this.
echo "BAR in quotes is not expanded: [\"BAR\"]"

## Global aliases at multiple positions in one command
alias -g LEFT='left-text'
alias -g RIGHT='right-text'
echo LEFT middle RIGHT
