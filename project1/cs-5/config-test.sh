# 1. search for optimized_config.json
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=search -mxp -history config-test.c

# 2. change src by optimized_config.json automatically & run
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=change -target=optimized_config.json config-test.c
echo "Changed Program Output:"
./changed_output
echo "Please modify src file manually, then run command 'clang -O2 -o manual manual.c -lm & ./manual'"

# 3. change src by optimized_config.json manually, (REMOVE PRAGMA PRECISION !!!)
# clang -O2 -o manual manual.c -lm & ./manual

# Generate manual.bc (Optional for DEBUG)
# clang -g -S -emit-llvm -c manual.c -fno-discard-value-names -o manual.bc -lm