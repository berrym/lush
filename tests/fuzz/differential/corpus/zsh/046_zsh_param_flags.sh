var="hello world"
echo ${(U)var}
echo ${(L)var}
echo ${(C)var}
list="a,b,c,d"
echo ${(s/,/)list}
items=(one two three)
echo ${(j/-/)items}
