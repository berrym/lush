# print builtin: combinations of -l/-n/-r and -f.
print hello world
print -l a b c
print -n "no-newline"; print "|end"
print -r "raw \n no escape"
print "escaped: \n becomes newline"
print -ln tight here
print -- "-leading-dash arg"
print -f "%-10s %d\n" name 42
print -f "%s=%s\n" key1 val1 key2 val2
