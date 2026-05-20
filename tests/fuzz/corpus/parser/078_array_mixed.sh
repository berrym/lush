# array_literal with mixed positional and indexed array_element
arr=(zero one [5]=five [10]=ten eleven)
echo "${arr[0]}"
echo "${arr[5]}"
echo "${arr[10]}"
echo "${arr[11]}"

# Associative array with [key]=value
declare -A map=([alpha]=A [beta]=B [gamma]=G)
echo "${map[alpha]}"
