function tx = helperDSSSTransmitter(system,sps,DSSS)
%HELPERDSSSTRANSMITTER builds a COSPAS-SARSAT burst
% TX = HELPERDSSSTRANSMITTER(SYSTEM,SPS,DSSS) reads in fields in SYSTEM and
% DSSS and outputs an OOQPSK baseband signal TX with SPS samples per
% symbol. The transmit data is returned in the structure TX.
% SYSTEM - structure containing system parameters
%    PREAMBLELENGTH - preamble length in chips
%    PAYLOADLENGTH - payload length in chips
%    SPREADINGFACTOR - spreading factor
%    CHIPRATE - chip rate in chips per second
% SPS - samples per symbol
% DSSS - structure containing the spreading sequence
%    PRN_I - in-phase psuedorandom sequence
%    PRN_Q - quadrature pseudorandom sequence

%   Copyright 2023 The MathWorks, Inc.

% The preamble consists of the first 6400 chips of the two I and Q PRN
% sequences. This is equivalent to sending 50 logical '0' bits at the
% beginning of the transmission burst when using a spreading factor of 256.
% Generate 202 random bits to simulate the information bits.
tx.preambleBits = zeros(system.preambleLength,1,'logical');
tx.payloadBits = randi([0,1],system.payloadLength,1,'logical');

% The COSPAS-SARSAT standard uses Bose-Chaudhuri-Hocquengham (BCH) encoding
% for error detection and correction. The linear block encoder reads 202
% bits of information and computes 48 parity bits to send after the
% information bits. The decoder uses the parity bits to detect and locate
% bit errors for error correction. Configure the comm.BCHEncoder to use a
% shortened form of a (255,207) BCH code to create a (250,202) code. This
% code can correct up to six errors. Encode the payload using a (255,207)
% BCH encoder with shortened form (250,202) encoding
hEncode = comm.BCHEncoder(255, 207,...
    ['x48+x47+x46+x42+x41+x40+x39+x38+x37+x35+x33+x32+x31+x26+' ...
    'x24+x23+x22+x20+x19+x18+x17+x16+x13+x12+x11+x10+x7+x4+x2+x+1'], 202);
% tx.message contains the payload and error correction bits
tx.message =  hEncode(tx.payloadBits);

% Create the transmission burst by concatenating the preamble bits with the
% payload and error correction bits.
tx.burst = [tx.preambleBits; tx.message];

% Partition the data burst into the I and Q streams and spread the streams
% using the PRN sequences generated earlier.
% Split the I and Q branches (serial to parallel converter)
Ibits = tx.burst(1:2:end); % odd bits on I
Qbits = tx.burst(2:2:end); % even bits on Q

%% Spreading and Modulation
% Spread the I and Q streams by lengthening each bit to the length of the
% spreading factor. Per the COSPAS-SARSAT standard, a logical '1' inverts
% the spreading sequence for that bit, while a logical '0' preserves the
% spreading sequence. Use XOR to modulate the bits to chips and
% comm.OQPSKModulator to perform the OQPSK modulation. This mapping also
% implies that Gray mapping is used in the OQPSK modulator.

Ibup = repelem(Ibits, system.spreadingFactor);
Qbup = repelem(Qbits, system.spreadingFactor);

tx.Ci = xor(Ibup, DSSS.PRN_I);
tx.Cq = xor(Qbup, DSSS.PRN_Q);

% Create the transmit bit stream for OQPSK modulation
tx.Chips = zeros(system.chipRate*2,1,'logical');
tx.Chips(1:2:end) = tx.Ci; % assign in-phase bits to the odd bits
tx.Chips(2:2:end) = tx.Cq; % assign quadrature bits to the even bits

% Configure the OQPSK modulator and modulate using OQPSK with half-sine
% pulse shaping
oqpskMod = comm.OQPSKModulator(...
    BitInput =         true, ...
    PulseShape =       "Half sine", ...
    SamplesPerSymbol = sps); 
tx.Samples = oqpskMod(tx.Chips);

end
