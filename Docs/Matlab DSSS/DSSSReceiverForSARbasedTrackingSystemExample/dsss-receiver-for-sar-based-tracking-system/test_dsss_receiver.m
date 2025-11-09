function test_dsss_receiver()
%#codegen
% Test du récepteur DSSS

% Paramètres
settings.oversampling = 4;
settings.velocity = -7000;
settings.txPower = 33;

% Calcul de la taille du buffer
packetLength = 50 + 202 + 48;
sps = 2 * settings.oversampling;
numBurstSamples = (packetLength/2) * 256 * sps;

% Génère un signal d'entrée de la bonne taille
otaBuffer = complex(randn(numBurstSamples, 1), randn(numBurstSamples, 1));

% Appelle la fonction
[rxPayload, errs, rxStatus] = dsss_receiver(otaBuffer, settings);

% Affichage compatible codegen
if rxStatus
    fprintf('Détection réussie, erreurs corrigées : %d\n', errs);
else
    fprintf('Échec de la détection\n');
end

end