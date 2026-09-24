function [adc, rawWords, info] = simulateAdc(cfg, targets)
%SIMULATEADC Real beat samples, summed over simultaneous TX transmissions.
% targets: struct array with rangeM, velocityMps, azimuthDeg, amplitudeCounts.
% Optional phaseRad. Positive velocity means positive slow-time frequency.
% Stop-and-hop, fixed range/angle, ideal far-field array; no multipath or
% range-Doppler coupling. Amplitude is per TX, before summing TX signals.
% Outputs [sample, chirp, RX]; rawWords are ADC words, NOT a framed DAT file.
assert(cfg.numTx == 4 && cfg.numRx == 4, 'Expected 4TX4RX.');
assert(numel(cfg.txSubbands) == cfg.numTx);
assert(numel(cfg.bpmCode) == cfg.numChirps && all(abs(cfg.bpmCode) == 1));
assert(mod(cfg.dopplerFftSize, cfg.numSubbands) == 0);
assert(numel(cfg.txPositionsM) == cfg.numTx && numel(cfg.rxPositionsM) == cfg.numRx);
stream = RandStream('mt19937ar', 'Seed', cfg.seed);
t = (0:cfg.numSamples-1).' / cfg.sampleRateHz;
m = 0:cfg.numChirps-1;
signal = zeros(cfg.numSamples, cfg.numChirps, cfg.numRx);
for k = 1:numel(targets)
    target = targets(k);
    validateattributes(target.rangeM, {'numeric'}, {'scalar','finite','nonnegative'});
    validateattributes(target.velocityMps, {'numeric'}, {'scalar','finite'});
    validateattributes(target.azimuthDeg, {'numeric'}, {'scalar','finite','>=',-90,'<=',90});
    validateattributes(target.amplitudeCounts, {'numeric'}, {'scalar','finite','nonnegative'});
    fr = 2 * cfg.slopeHzPerS * target.rangeM / cfg.c;
    assert(fr < cfg.sampleRateHz / 2, 'Target range exceeds real ADC Nyquist limit.');
    fd = 2 * target.velocityMps / cfg.lambdaM;
    phase0 = 0;
    if isfield(target, 'phaseRad'), phase0 = target.phaseRad; end
    basePhase = 2*pi*(fr*t + fd*cfg.chirpPeriodS*m) + phase0;
    for tx = 1:cfg.numTx
        codePhase = 2*pi*cfg.txSubbands(tx)/cfg.numSubbands*m;
        for rx = 1:cfg.numRx
            spatialPhase = 2*pi/cfg.lambdaM * ...
                (cfg.txPositionsM(tx)+cfg.rxPositionsM(rx))*sind(target.azimuthDeg);
            signal(:,:,rx) = signal(:,:,rx) + target.amplitudeCounts * ...
                cos(basePhase + codePhase + spatialPhase) .* cfg.bpmCode(:).';
        end
    end
end
signal = signal + cfg.noiseStdCounts * randn(stream, size(signal));
quantized = round(signal + cfg.adcOffset);
info.clippedFraction = nnz(quantized < 0 | quantized > cfg.adcMax) / numel(quantized);
quantized = min(max(quantized, 0), cfg.adcMax);
rawWords = bitshift(uint16(quantized), cfg.adcShift);
adc = double(bitshift(rawWords, -cfg.adcShift)) - cfg.adcOffset;
info.synthetic = true;
info.targets = targets;
info.dimensionOrder = 'sample, chirp, RX';
info.virtualPositionsM = reshape((cfg.rxPositionsM(:) + cfg.txPositionsM(:).'), [], 1);
end
