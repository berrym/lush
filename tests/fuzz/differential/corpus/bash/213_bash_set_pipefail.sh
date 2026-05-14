# set -o pipefail: pipeline exit status is the first non-zero from
# any stage (not just the last). Without pipefail, only the last
# stage's status counts.

# Without pipefail: last stage's status
(false | true | true; echo "default: $?")

# With pipefail: first failure surfaces
(set -o pipefail; false | true | true; echo "pipefail: $?")

# With pipefail, success pipeline still 0
(set -o pipefail; true | true | true; echo "all-true: $?")

# Multiple failures: first wins
(set -o pipefail; false | (exit 2) | true; echo "multi: $?")
