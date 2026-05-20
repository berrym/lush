# >| clobber redirect overrides noclobber
set -C
echo first > /tmp/once
echo override >| /tmp/once
set +C
