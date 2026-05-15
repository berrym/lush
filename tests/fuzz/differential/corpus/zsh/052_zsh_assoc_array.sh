typeset -A colors
colors=(red FF0000 green 00FF00 blue 0000FF)
echo ${colors[red]}
echo ${(k)colors}
echo ${(v)colors}
echo ${(kv)colors}
