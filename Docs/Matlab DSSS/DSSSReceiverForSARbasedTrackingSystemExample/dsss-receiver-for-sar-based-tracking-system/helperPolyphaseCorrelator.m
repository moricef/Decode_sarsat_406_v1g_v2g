function [idx, corrBuffer] = helperPolyphaseCorrelator(rxBuffer, referenceSignal, sps, offset)
%#codegen
% HELPERPOLYPHASECORRELATOR Corrélation polyphase pour la détection de préambule

% Décimation du buffer d'entrée
decimatedSampleBuffer = reshape(rxBuffer, sps, []);
bufferLen = size(decimatedSampleBuffer, 2);
refLen = length(referenceSignal);

% Initialisation du buffer de corrélation
xcorrLen = bufferLen + refLen - 1;
xcorrBuffer = zeros(xcorrLen, sps);

% Initialisation des indices de détection
startIdxs = zeros(1, sps);

% Corrélation pour chaque phase
for k = 1:sps
    % Corrélation entre le signal décimé et le préambule
    currentPhase = decimatedSampleBuffer(k, :);
    xcorrBuffer(:, k) = abs(conv(currentPhase', conj(fliplr(referenceSignal))));

    % Détection du pic de corrélation
    [maxVal, idx2] = max(xcorrBuffer(:, k));
    threshold = 0.35 * maxVal;
    if xcorrBuffer(idx2, k) > threshold
        startIdxs(k) = idx2 - offset + 1;
    else
        startIdxs(k) = 0;
    end
end

% Trouver le pic de corrélation maximal parmi toutes les phases
[maxXcorrVals, maxXcorrIdxs] = max(xcorrBuffer);
[maxDetectorVal, kidx] = max(maxXcorrVals);

% Extraction du buffer de corrélation pour la phase sélectionnée
corrBuffer = xcorrBuffer(refLen:end, kidx);

% Décision de détection finale
if isempty(corrBuffer) || maxDetectorVal < 5.5 * mean(abs(corrBuffer))
    idx = [];
else
    startIdx = startIdxs(kidx);
    if startIdx == 0
        idx = [];
    else
        idx = max(1, (startIdx - 1) * sps + kidx - floor(sps / 2));
    end
end
end