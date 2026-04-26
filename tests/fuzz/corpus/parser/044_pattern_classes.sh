if [[ $x == [aA]* ]]; then echo letter; fi
[[ $word =~ ^[0-9]+$ ]] && echo digits
case $f in
    [Mm]akefile) echo makefile ;;
    *.[ch]) echo c source ;;
    !(*.tmp)) echo not tmp ;;
esac
