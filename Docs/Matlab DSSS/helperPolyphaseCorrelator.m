function [idx,corrBuffer] = helperPolyphaseCorrelator(rxBuffer,referenceSignal,sps,offset)
%HELPERPOLYPHASECORRELATOR correlate a sequence against an oversampled
%sequence
% [IDX,CORRBUFFER] = HELPERPOLYPHASECORRELATOR(RXBUFFER,REFERENCESIGNAL,SPS,OFFSET)
% is a polyphase correlator that correlates a sample-rate time domain
% reference signal against an oversampled signal. The signal's oversampling
% factor must be an integer value.
%
% IDX - location of the reference signal within the sample buffer. Returns
%   empty if no preamble is found.
% CORRBUFFER - the correlation output buffer
% RXBUFFER - the oversampled signal buffer
% REFERENCESIGNAL - the reference signal sampled at the sample rate
% SPS - oversampling factor
% OFFSET - offset of the reference signal from the beginning of the
%   reference. Set this parameter to zero when you need to correlate the
%   entirety of the reference signal. If the offset is not zero, it is
%   implied that the reference signal does not contain the first <OFFSET>
%   samples of the signal. This is typically done when the beginning of the
%   input signal is not reliable (for instance when the signal is first
%   distorted while the AGC is converging).

%   Copyright 2023 The MathWorks, Inc.

% Create an array of decimated samples from the sample buffer
% Decimate the sample buffer by the samples per symbol
decimatedSampleBuffer = reshape(rxBuffer,sps,[]);
bufferLen = length(decimatedSampleBuffer);
xcorrBuffer = zeros(bufferLen+length(referenceSignal)-1,sps);
startIdxs = [];
for k=1:sps
    [idx2,xcorrBuffer(:,k)] = timingEstimate(decimatedSampleBuffer(k,:).',referenceSignal,Threshold=0.35);
    if ~isempty(idx2)
        startIdxs(k) = idx2 - offset + 1; % adjust the detected indices to account for offset preamble (starts at index 1)
    end
end
[maxXcorrVals,maxXcorrIdxs] = max(abs(xcorrBuffer)); % get the max value from each of the polyphases
[maxDetectorVal,kidx] = max(maxXcorrVals); % find the decimated vector that contains the max value
corrBuffer = xcorrBuffer(length(referenceSignal):end,kidx); % get the correlation vector of that decimated vector
if maxDetectorVal < 5.5*mean(abs(corrBuffer)) % set threshold to 5.5x the average correlator magnitude
    idx = []; % return an empty index if no preamble is found
else
    startIdx = startIdxs(kidx); % Get the start index within that correlation vector
    idx = max(1,(startIdx-1)*sps + kidx - (sps/2));

    fprintf('Found preamble at correlation buffer number %d, index %d, sample index %d\n',kidx,startIdx,idx);
end
