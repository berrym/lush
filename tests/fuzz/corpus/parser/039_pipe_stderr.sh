ls /nonexistent |& cat
make 2>&1 | tee build.log
