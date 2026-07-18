# return/exit status is an 8-bit value: it must be normalized mod 256.
# Regression guard for the 200-455 function-return decode bound (#516).
a() { return -1; }; a; echo "$?"
b() { return 200; }; b; echo "$?"
c() { return 100; }; c; echo "$?"
d() { return 256; }; d; echo "$?"
e() { return 257; }; e; echo "$?"
