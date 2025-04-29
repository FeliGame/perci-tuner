# 1. search for optimized_config.json by dd
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=search-optimize $1
mv optimized_config.json dd.json
echo "DD Search output: dd.json"

# 2. search for optimized_config.json by exhaust
sh $PERCI_TUNER/scripts/mxpcc.sh -mode=search-optimize -exhaust $1
mv optimized_config.json exhaust.json
echo "Exhaust Search output: exhaust.json"

echo "Difference between dd & exhaust:"
diff dd.json exhaust.json
