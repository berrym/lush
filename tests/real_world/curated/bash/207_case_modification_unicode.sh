#!/bin/bash
## Case-modification parameter expansion with Unicode input.
##
## Exercises bash's ${var^^} / ${var,,} / ${var^} / ${var,} forms,
## both unrestricted and pattern-restricted. The pattern-restricted
## case is the form that lush previously implemented bytewise and
## dropped non-ASCII case mapping on; the fix iterates by codepoint
## and routes each codepoint through the Unicode case-mapping tables.
##
## Inputs include Latin-1 supplement (é, ï, ä), Latin Extended-A,
## and mixed ASCII / non-ASCII patterns.

## --- Unrestricted full case ---
v1="CAFÉ NAÏVE"
echo "1a: ${v1,,}"        ## café naïve
echo "1b: ${v1^^}"        ## CAFÉ NAÏVE (already upper -- still correct)

v2="café naïve"
echo "2a: ${v2^^}"        ## CAFÉ NAÏVE
echo "2b: ${v2,,}"        ## café naïve

## --- First-character only ---
v3="école"
echo "3a: ${v3^}"         ## École

v4="ÉCOLE"
echo "4a: ${v4,}"         ## éCOLE

## --- ASCII pattern restriction (existing behaviour preserved) ---
v5="hello world"
echo "5a: ${v5^^[aeiou]}" ## hEllO wOrld
echo "5b: ${v5^[aeiou]}"  ## hello world  (^ tests only the first char;
                          ## first char 'h' does not match, so no change.
                          ## bash spec, not "first matching anywhere".)

## --- Unicode pattern restriction (the new behaviour) ---
v6="CAFÉ NAÏVE"
echo "6a: ${v6,,[ÉÏ]}"    ## CAFé NAïVE (only É and Ï lowercased)

v7="aäbïcëd"
echo "7a: ${v7^^[äïë]}"   ## aÄbÏcËd

## --- First-only with Unicode pattern ---
v8="aäbäc"
echo "8a: ${v8^[ä]}"      ## aÄbäc (only first matching codepoint)

## --- Empty / edge cases ---
v9=""
echo "9a: '${v9^^}'"      ## ''
echo "9b: '${v9,,[a]}'"   ## ''
