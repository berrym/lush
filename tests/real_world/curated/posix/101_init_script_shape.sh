# Pattern from /etc/init.d/* scripts and systemd shell wrappers: usage
# function, action dispatch via case, helper functions, exit codes.

NAME="example-service"
PIDFILE="/tmp/${NAME}.pid"

usage() {
    cat <<EOF
Usage: $0 {start|stop|restart|status|reload}
Manage the $NAME service.
EOF
}

is_running() {
    [ -f "$PIDFILE" ] && return 0
    return 1
}

do_start() {
    if is_running; then
        echo "$NAME is already running"
        return 0
    fi
    # Simulate writing a pidfile
    echo "12345" > "$PIDFILE"
    echo "$NAME started"
    return 0
}

do_stop() {
    if ! is_running; then
        echo "$NAME is not running"
        return 0
    fi
    rm -f "$PIDFILE"
    echo "$NAME stopped"
    return 0
}

do_status() {
    if is_running; then
        echo "$NAME is running (pid $(cat "$PIDFILE"))"
        return 0
    else
        echo "$NAME is stopped"
        return 3
    fi
}

case "${1:-status}" in
    start)   do_start ;;
    stop)    do_stop ;;
    restart) do_stop; do_start ;;
    status)  do_status; exit $? ;;
    reload)  echo "$NAME reloaded" ;;
    *)       usage; exit 2 ;;
esac

# Clean up before exit
rm -f "$PIDFILE"
exit 0
