# 1. search for optimized_config.json
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=search-optimize -mxp multiversion-test.c -history

# 2. change src by optimized_config.json automatically & run
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=multi-version -multi-ir=optimized_output.bc -multi-func=fun

export PERCI_TUNER_ORIGINAL_VERSION=TRUE
echo "precision-first version output & time"
time ./merged

export PERCI_TUNER_ORIGINAL_VERSION=FALSE
echo "speed-first version output & time"
time ./merged
