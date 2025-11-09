#!/bin/bash
# Lance MATLAB en mode terminal (sans interface graphique)

MATLAB_PATH="/home/fab2/vm/MATLAB_R2022b_Linux/bin/matlab"
DSSS_PATH="/home/fab2/Developpement/COSPAS-SARSAT/balise_406MHz/dec406_V10.2/Docs/Matlab DSSS/DSSSReceiverForSARbasedTrackingSystemExample/dsss-receiver-for-sar-based-tracking-system"

echo "=== Lancement de MATLAB en mode terminal ==="
echo "Répertoire : $DSSS_PATH"
echo ""

cd "$DSSS_PATH"
$MATLAB_PATH -nodesktop -nosplash
