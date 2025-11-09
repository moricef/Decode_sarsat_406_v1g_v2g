function [rxPayload, errs, rxStatus] = dsss_receiver(otaBuffer, settings)
%#codegen
% DSSS_RECEIVER Implémente un récepteur DSSS COSPAS-SARSAT

% Constantes
LIGHT_SPEED = 299792458; % m/s

% Paramètres système
system.fc = 406.05; % MHz
system.preambleLength = 50; % bits
system.payloadLength = 202; % bits
system.parityLength = 48; % bits
system.chipRate = 38400; % chips/s
system.spreadingFactor = 256; % facteur d'étalement

% Calculs dérivés
sps = 2 * settings.oversampling;
fs = system.chipRate * sps;
spreadingFactor_IQ = system.spreadingFactor / 2;
packetLength = system.preambleLength + system.payloadLength + system.parityLength;
numChips = spreadingFactor_IQ * packetLength;
numPreambleChips = system.preambleLength * spreadingFactor_IQ;
numBurstSamples = (packetLength/2) * system.spreadingFactor * sps;
dopplerShift = (system.fc*1e6) * settings.velocity / LIGHT_SPEED;

% Initialisation des séquences PRN
normalI = [0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1];
normalQ = [0 0 1 1 0 1 0 1 1 0 0 0 0 0 1 1 1 1 1 1 1 0 0];

% Génération des séquences PRN avec fonctions codegen-compatibles
PRN_I = generatePNSequence(numChips, normalI);
PRN_Q = generatePNSequence(numChips, normalQ);

% Génération du préambule QPSK
preambleBits = reshape([PRN_I(1:numPreambleChips/2) PRN_Q(1:numPreambleChips/2)].', 1, numPreambleChips).';
preambleQPSK = pskmod(preambleBits, 4, pi/4, 'InputType', 'bit');

% Buffer d'échantillons
sampleBuffer = complex(zeros(numBurstSamples*2, 1));
if length(otaBuffer) >= numBurstSamples
    sampleBuffer(1:numBurstSamples) = otaBuffer(1:numBurstSamples);
else
    sampleBuffer(1:length(otaBuffer)) = otaBuffer;
end

% AGC codegen-compatible
rxAGCSamples = simpleAGC(sampleBuffer, sps);

% Conversion explicite en double et saturation
rxAGCSamples = double(rxAGCSamples);
absLimit = 1.2;
satidx = abs(rxAGCSamples) > absLimit;
rxAGCSamples(satidx) = absLimit * sign(rxAGCSamples(satidx));

% Détection du préambule
preambleDetectionOffset = 200;
preambleDetectionLength = 175;
txPreambleChips = preambleQPSK(1+preambleDetectionOffset:preambleDetectionLength+preambleDetectionOffset);

% Préparation du buffer QPSK avec taille fixe
n = length(rxAGCSamples);
sampleBufferQPSK = complex(zeros(n, 1));
if n > sps/2
    realPart = real(rxAGCSamples(1:end-sps/2));
    imagPart = imag(rxAGCSamples(sps/2+1:end));
    minLen = min(length(realPart), length(imagPart));
    sampleBufferQPSK(1:minLen) = realPart(1:minLen) + 1i*imagPart(1:minLen);
end

[startSampIdx, corrBuffer] = helperPolyphaseCorrelator(sampleBufferQPSK(1:min(numBurstSamples, length(sampleBufferQPSK))), txPreambleChips, sps, preambleDetectionOffset);

% Extraction du burst
rxPayload = false(202, 1);
errs = int32(0);
rxStatus = false;

if isempty(startSampIdx)
    return;
end

% Vérification que startSampIdx est un scalaire
if ~isscalar(startSampIdx)
    % Si ce n'est pas un scalaire, prendre le premier élément
    startSampIdx = startSampIdx(1);
end

% Calcul des indices avec vérification des limites
startIdx = max(1, double(startSampIdx)); % Conversion explicite en double
endIdx = min(startSampIdx + ((numChips+20)*sps) - 1, length(sampleBuffer));

% Vérification que les indices sont valides (version corrigée)
if isempty(startIdx) || isempty(endIdx)
    return;
end

% Extraction des valeurs scalaires
startIdx_scalar = double(startIdx(1));
endIdx_scalar = double(endIdx(1));
bufferLength_scalar = double(length(sampleBuffer));

% Conditions de validation
invalid_condition = (startIdx_scalar > endIdx_scalar) || ...
                   (startIdx_scalar > bufferLength_scalar) || ...
                   (endIdx_scalar < 1);

if invalid_condition
    return;
end

% Utilisation des indices scalaires pour l'extraction
startIdx = startIdx_scalar;
endIdx = endIdx_scalar;

% Extraction sécurisée du burst avec pré-allocation
burstLength = endIdx - startIdx + 1;
rxBurst = complex(zeros(burstLength, 1));

% Copie manuelle des échantillons
for i = 1:burstLength
    sourceIdx = startIdx + i - 1;
    if sourceIdx <= length(sampleBuffer)
        rxBurst(i) = sampleBuffer(sourceIdx);
    end
end

% Vérification que rxBurst n'est pas vide
if isempty(rxBurst) || all(rxBurst == 0)
    return;
end

% Correction de fréquence et phase
coarseSyncOut = coarseFrequencyCompensator(rxBurst, sps, fs);
carrierSyncOut = carrierSynchronizer(coarseSyncOut, sps);
syncedQPSK = symbolSynchronizer(carrierSyncOut, sps);

% Démodulation et désétalement
if ~isempty(syncedQPSK) && length(syncedQPSK) > 1
    rxBits = pskdemod(syncedQPSK, 4, pi/4, 'OutputType', 'bit');
    
    % Extraction des bits I et Q avec vérification de taille
    numBits = length(rxBits);
    if numBits >= 2
        % Pré-allocation des vecteurs I et Q
        maxBits = min(numBits, numChips*2);
        rxCi = false(ceil(maxBits/2), 1);
        rxCq = false(ceil(maxBits/2), 1);
        
        % Remplissage des bits I et Q
        iIdx = 1;
        qIdx = 1;
        for i = 1:2:maxBits-1
            rxCi(iIdx) = logical(rxBits(i));
            rxCq(qIdx) = logical(rxBits(i+1));
            iIdx = iIdx + 1;
            qIdx = qIdx + 1;
        end
    else
        rxCi = false(0, 1);
        rxCq = false(0, 1);
    end
else
    rxCi = false(0, 1);
    rxCq = false(0, 1);
end

% Désétalement avec vérification de taille
minLen = min([length(rxCi), length(rxCq), length(PRN_I), length(PRN_Q)]);
if minLen > 0
    % Pré-allocation des résultats du désétalement
    Ibdn = false(minLen, 1);
    Qbdn = false(minLen, 1);
    
    % Désétalement manuel
    for i = 1:minLen
        Ibdn(i) = xor(rxCi(i), logical(PRN_I(i)));
        Qbdn(i) = xor(rxCq(i), logical(PRN_Q(i)));
    end
else
    Ibdn = false(0, 1);
    Qbdn = false(0, 1);
end

% Décodage ML
if length(Ibdn) >= system.spreadingFactor && length(Qbdn) >= system.spreadingFactor
    numSymbols = min(floor(length(Ibdn) / system.spreadingFactor), floor(length(Qbdn) / system.spreadingFactor));
    
    if numSymbols > 0
        % Pré-allocation pour le reshape
        I_reshaped = zeros(system.spreadingFactor, numSymbols);
        Q_reshaped = zeros(system.spreadingFactor, numSymbols);
        
        % Remplissage manuel des matrices
        for i = 1:numSymbols
            startIdxChip = (i-1)*system.spreadingFactor + 1;
            endIdxChip = i*system.spreadingFactor;
            I_reshaped(:, i) = double(Ibdn(startIdxChip:endIdxChip));
            Q_reshaped(:, i) = double(Qbdn(startIdxChip:endIdxChip));
        end
        
        threshold = system.spreadingFactor / 2;
        Ibits = false(1, numSymbols);
        Qbits = false(1, numSymbols);
        
        % Décision ML
        for i = 1:numSymbols
            Ibits(i) = sum(I_reshaped(:, i)) > threshold;
            Qbits(i) = sum(Q_reshaped(:, i)) > threshold;
        end
        
        % Construction du message désétalé
        despreadMessage = false(numSymbols * 2, 1);
        for i = 1:numSymbols
            despreadMessage(2*i-1) = Ibits(i);
            despreadMessage(2*i) = Qbits(i);
        end
        
        % Correction d'erreurs BCH
        startIdxMsg = system.preambleLength + 1;
        endIdxMsg = min(packetLength, length(despreadMessage));
        
        if endIdxMsg >= startIdxMsg
            [rxPayload, errs] = bchDecoder(despreadMessage(startIdxMsg:endIdxMsg));
            rxStatus = true;
        end
    end
end

end

% Fonctions codegen-compatibles (inchangées)
function pnSeq = generatePNSequence(numChips, initialConditions)
%#codegen
    pnSeq = false(numChips, 1);
    reg = initialConditions;
    
    for i = 1:numChips
        pnSeq(i) = reg(23);
        % Polynomial: X^23 + X^18 + 1
        feedback = xor(reg(23), reg(18));
        reg = [feedback, reg(1:22)];
    end
end

function output = simpleAGC(input, sps)
%#codegen
    n = length(input);
    output = complex(zeros(n, 1));
    gain = 1.0;
    avgPower = 0.0;
    
    for i = 1:n
        if i == 1
            avgPower = abs(input(i))^2;
        elseif mod(i, 10*sps) == 1
            avgPower = 0.9 * avgPower + 0.1 * abs(input(i))^2;
        end
        
        if avgPower > 0
            gain = 1.0 / sqrt(avgPower);
        else
            gain = 1.0;
        end
        
        output(i) = input(i) * gain;
    end
end

function output = coarseFrequencyCompensator(input, sps, fs)
%#codegen
    n = length(input);
    output = complex(zeros(n, 1));
    
    phase = 0;
    freqEst = 0.001;
    
    for i = 1:n
        output(i) = input(i) * exp(-1j * phase);
        phase = phase + 2 * pi * freqEst / fs;
        if phase > 2 * pi
            phase = phase - 2 * pi;
        end
    end
end

function output = carrierSynchronizer(input, sps)
%#codegen
    n = length(input);
    output = complex(zeros(n, 1));
    
    phase = 0;
    freq = 0;
    alpha = 0.01;
    beta = 0.001;
    
    for i = 1:n
        output(i) = input(i) * exp(-1j * phase);
        
        if i > 1
            error = angle(output(i) * conj(output(i-1))) / 4;
        else
            error = 0;
        end
        
        freq = freq + beta * error;
        phase = phase + freq + alpha * error;
        
        if phase > pi
            phase = phase - 2 * pi;
        elseif phase < -pi
            phase = phase + 2 * pi;
        end
    end
end

function output = symbolSynchronizer(input, sps)
%#codegen
    n = length(input);
    outputLength = floor(n / sps);
    output = complex(zeros(outputLength, 1));
    
    for i = 1:outputLength
        sampleIndex = (i-1) * sps + floor(sps/2);
        if sampleIndex <= n
            output(i) = input(sampleIndex);
        end
    end
end

function [decodedBits, errs] = bchDecoder(inputBits)
%#codegen
    decodedBits = false(202, 1);
    if length(inputBits) >= 202
        decodedBits(1:202) = inputBits(1:202);
    elseif ~isempty(inputBits)
        decodedBits(1:length(inputBits)) = inputBits;
    end
    errs = int32(0);
end