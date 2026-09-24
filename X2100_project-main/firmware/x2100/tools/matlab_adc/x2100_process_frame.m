function result = x2100_process_frame(adc)
%X2100_PROCESS_FRAME Reproduce the firmware range/Doppler/DDMA front end.
%
% Input adc dimensions: [506 samples, 128 chirps, 4 RX].
% This reproduces detection_processing.c through compute_rd_map():
% per-chirp DC removal, Blackman range window, 512-point range FFT,
% firmware Hanning Doppler window, BPM despreading, 128-point Doppler FFT,
% noncoherent RX accumulation, four-subband DDMA selection and RD map.

expectedSize = [506, 128, 4];
if ~isequal(size(adc), expectedSize)
    error('X2100:BadADCShape', 'ADC size must be [506 128 4], got [%s].', ...
        num2str(size(adc)));
end

cfg.c = 299792458.0;
cfg.fs = 26.665e6;
cfg.fc = 76.5e9;
cfg.slope = 19.531e12;
cfg.chirpPeriod = 26e-6;
cfg.numSamples = 506;
cfg.numChirps = 128;
cfg.numRx = 4;
cfg.numTx = 2;
cfg.numEmptyBands = 2;
cfg.numTotalSubBands = cfg.numTx + cfg.numEmptyBands;
cfg.rangeFFTSize = 512;
cfg.dopplerFFTSize = 128;
cfg.numRangeBins = cfg.rangeFFTSize / 2;
cfg.dopplerBinsPerSubBand = cfg.dopplerFFTSize / cfg.numTotalSubBands;
cfg.wavelength = cfg.c / cfg.fc;
cfg.rangeBinSpacing = cfg.c * cfg.fs / ...
    (2 * cfg.slope * cfg.rangeFFTSize);
cfg.dopplerBinSpacing = cfg.wavelength / ...
    (2 * cfg.chirpPeriod * cfg.dopplerFFTSize);

% Match blackman_window(): n/(L-1).
nRange = (0:cfg.numSamples-1).';
rangeWindow = 0.42 - 0.5*cos(2*pi*nRange/(cfg.numSamples-1)) + ...
    0.08*cos(4*pi*nRange/(cfg.numSamples-1));

% Match the implementation (not its stale comment): (n+1)/(L+1).
nDoppler = (0:cfg.numChirps-1).';
dopplerWindow = 0.5 * (1 - ...
    cos(2*pi*(nDoppler+1)/(cfg.numChirps+1)));

x = double(adc);
x = x - mean(x, 1);                  % DC removal per chirp and RX
x = x .* reshape(rangeWindow, [], 1, 1);

rangeFFT = fft(x, cfg.rangeFFTSize, 1);
rangeFFT = rangeFFT(1:cfg.numRangeBins, :, :);

bpmCode = x2100_bpm_code();
slowTimeWeight = bpmCode .* dopplerWindow;
dopplerInput = rangeFFT .* reshape(slowTimeWeight, 1, [], 1);
dopplerFFT = fft(dopplerInput, cfg.dopplerFFTSize, 2);

% Firmware accumulates magnitudes, not powers, across the four RX channels.
dopplerMagnitudeSumRx = sum(abs(dopplerFFT), 3);
[rdMapDb, maxMetricSubband, decodedDopplerBin] = ...
    ddmaDecode(dopplerMagnitudeSumRx, cfg);

signedDopplerBin = decodedDopplerBin;
% Match cfar_detection.c's deliberately asymmetric temporary unwrap rule.
unwrapThreshold = (160.0 / 3.6) / cfg.dopplerBinSpacing;
signedDopplerBin(decodedDopplerBin > unwrapThreshold) = ...
    decodedDopplerBin(decodedDopplerBin > unwrapThreshold) - ...
    cfg.dopplerFFTSize;

result.config = cfg;
result.rangeAxisM = (0:cfg.numRangeBins-1) * cfg.rangeBinSpacing;
result.fullDopplerVelocityAxisMps = signedFftBins(cfg.dopplerFFTSize) * ...
    cfg.dopplerBinSpacing;
result.rangeFFT = rangeFFT;
result.dopplerFFT = dopplerFFT;
result.dopplerMagnitudeSumRx = dopplerMagnitudeSumRx;
result.rdMapDb = rdMapDb;
result.maxMetricSubband = maxMetricSubband;
result.decodedDopplerBin = decodedDopplerBin;
result.velocityMapMps = signedDopplerBin * cfg.dopplerBinSpacing;
end

function [rdMapDb, maxMetricSubband, decodedBin] = ddmaDecode(magnitudeSumRx, cfg)
numRng = cfg.numRangeBins;
numSubDop = cfg.dopplerBinsPerSubBand;
numBands = cfg.numTotalSubBands;
numTx = cfg.numTx;

rdMapLinear = zeros(numRng, numSubDop);
maxMetricSubband = zeros(numRng, numSubDop);
decodedBin = zeros(numRng, numSubDop);

for rng = 1:numRng
    for subDop = 0:numSubDop-1
        bandValues = zeros(1, numBands);
        for band = 0:numBands-1
            dopplerBin = subDop + band*numSubDop;
            bandValues(band+1) = magnitudeSumRx(rng, dopplerBin+1);
        end

        metric = zeros(1, numBands);
        for startBand = 0:numBands-1
            txBands = mod(startBand + (0:numTx-1), numBands) + 1;
            metric(startBand+1) = min(bandValues(txBands));
        end
        [~, bestOneBased] = max(metric);
        bestBand = bestOneBased - 1;
        maxMetricSubband(rng, subDop+1) = bestBand;

        selectedBands = mod(bestBand + (0:numTx-1), numBands);
        for tx = 1:numTx
            dopplerBin = subDop + selectedBands(tx)*numSubDop;
            rdMapLinear(rng, subDop+1) = rdMapLinear(rng, subDop+1) + ...
                magnitudeSumRx(rng, dopplerBin+1);
        end
        decodedBin(rng, subDop+1) = subDop + bestBand*numSubDop;
    end
end

rdMapDb = 20*log10(max(rdMapLinear, realmin('double')));
end

function bins = signedFftBins(n)
bins = 0:n-1;
bins(bins >= n/2) = bins(bins >= n/2) - n;
end
