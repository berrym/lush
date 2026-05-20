# &> and &>> combined stdout+stderr redirection
cmd &> /tmp/combined.log
cmd2 &>> /tmp/combined.log
{ echo out; echo err >&2; } &> /tmp/both
