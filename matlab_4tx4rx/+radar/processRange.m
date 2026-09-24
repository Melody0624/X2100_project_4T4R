function out = processRange(adc, cfg)
%PROCESSRANGE Fast-time DC removal, symmetric Blackman window, range FFT.
% Input is decoded signed ADC counts [sample,chirp,RX], not raw uint16 words.
% Complex FFT values retain phase for subsequent BPM/DDMA processing.
validateattributes(adc, {'double','single'}, {'real','finite','nonempty'});
if ~isequal(size(adc), [cfg.numSamples cfg.numChirps cfg.numRx])
    error('radar:range:AdcSize', 'ADC must have size [numSamples,numChirps,numRx].');
end
validateattributes(cfg.rangeFftSize, {'numeric'}, ...
    {'scalar','integer','>=',cfg.numSamples});
assert(mod(cfg.rangeFftSize,2)==0, 'Range FFT length must be even.');
validateattributes(cfg.sampleRateHz, {'numeric'}, {'scalar','finite','positive'});
validateattributes(cfg.slopeHzPerS, {'numeric'}, {'scalar','finite','positive'});
assert(cfg.numSamples > 1, 'At least two samples are required.');
adc = double(adc);
out.dcMean = mean(adc,1);
out.adcDcRemoved = adc - out.dcMean;
n = (0:cfg.numSamples-1).';
out.window = 0.42 - 0.5*cos(2*pi*n/(cfg.numSamples-1)) ...
    + 0.08*cos(4*pi*n/(cfg.numSamples-1));
out.windowed = out.adcDcRemoved .* out.window;
spectrum = fft(out.windowed,cfg.rangeFftSize,1);
% Retain bins 0..N/2-1, excluding Nyquist, to match the planned C cube.
out.cube = spectrum(1:cfg.rangeFftSize/2,:,:);
out.rangeAxisM = (0:cfg.rangeFftSize/2-1).' * ...
    cfg.c*cfg.sampleRateHz/(2*cfg.slopeHzPerS*cfg.rangeFftSize);
out.rangeResolutionM = cfg.c*cfg.sampleRateHz/(2*cfg.slopeHzPerS*cfg.numSamples);
% Unnormalized squared FFT magnitude, not watts, dBm, or a CFAR result.
out.powerPerRx = reshape(mean(abs(out.cube).^2,2),[],cfg.numRx);
out.meanPower = mean(out.powerPerRx,2);
reference = max(out.meanPower);
out.relativePowerDb = 10*log10(max(out.meanPower,realmin)/max(reference,realmin));
out.hasSignal = reference > 0;
out.peakRangeM = NaN;
out.peakBin = NaN;
if out.hasSignal
    [~,out.peakBin] = max(out.meanPower);
    out.peakRangeM = out.rangeAxisM(out.peakBin);
end
end
