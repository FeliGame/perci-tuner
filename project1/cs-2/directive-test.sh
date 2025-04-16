sh $PERCI_TUNER/scripts/mxpcc.sh -mode=search-optimize -mxp directive-test.c -history
original_output=$(./output)
optimized_output=$(./optimized_output)
relative_error=$(awk -v orig="$original_output" -v opt="$optimized_output" '
BEGIN {
    diff = orig - opt;
    error = (diff != 0 && orig != 0) ? (diff/orig) : 0;
    printf "%.16e", error < 0 ? -error : error
}')
echo "Original_output: $original_output"
echo "Optimized_output: $optimized_output"
echo "Relative_error: $relative_error"