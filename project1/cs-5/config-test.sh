# 1. search for optimized_config.json
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=search -mxp config-test.c

# 2. change src by optimized_config.json manually, (REMOVE PRAGMA PRECISION !!!)
# clang -O2 -o manual manual.c -lm

# 3. change src by optimized_config.json automatically & run
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=change -target=optimized_config.json
