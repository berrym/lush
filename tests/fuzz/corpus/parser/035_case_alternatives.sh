case "$x" in
    a|b|c)
        echo "letter"
        ;&
    1|2|3)
        echo "digit"
        ;;&
    *)
        echo "other"
        ;;
esac
