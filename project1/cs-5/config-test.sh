# generate config.json for user
# sh $PERCI_TUNER/scripts/mxpcc.sh -mode=generate-config -history config-test.c

# user generate runnable (REMOVE PRAGMA PRECISION !!!)
clang -O2 -o ref_output config-test-ref.c -lm

# generate IR (Optional for DEBUG)
# clang -g -S -emit-llvm -c config-test-ref.c -fno-discard-value-names -o ref.bc -lm

# 2. change src by optimized_config.json automatically & run
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=change-by-config -target=config config-test.c

echo "Reference Program Output:"
./ref_output
echo "Changed Program Output:"
./changed_output
