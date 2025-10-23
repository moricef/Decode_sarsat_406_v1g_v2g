#!/bin/bash
# Test stability - run demodulator 10 times and extract metrics

cd /home/fab2/Developpement/COSPAS-SARSAT/balise_406MHz/dec406_v10.2

echo "Running 10 tests to check stability..."
echo ""

for i in $(seq 1 10); do
    echo "=== TEST $i/10 ==="
    ./dec406_iq ~/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/test_known.iq 2>&1 | \
        grep -E "(\[PHASE\] ✓ Best|Mean correlation:|Errors:.*%|Found preamble)"
    echo ""
done

echo "=== SUMMARY ==="
echo "Extracting phase correlation values..."
for i in $(seq 1 10); do
    ./dec406_iq ~/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/test_known.iq 2>&1 | \
        grep "\[PHASE\] ✓ Best" | sed 's/.*corr=//' | sed 's/%//'
done | awk '{sum+=$1; count++} END {print "Average phase correlation: " sum/count "%"}'
