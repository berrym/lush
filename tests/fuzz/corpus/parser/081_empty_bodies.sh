# Empty / minimal command_body across compounds
if true; then :; fi
while false; do :; done
until true; do :; done
for x in; do :; done
case x in *) :; ;; esac
{ :; }
( : )

# Multiple separators only
if true; then ; ; ; echo body; ; ; fi
