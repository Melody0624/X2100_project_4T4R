function result = calibrate_tx01_8ch(filePath)
%CALIBRATE_TX01_8CH Broadside 8-channel candidate for 2TX x 4RX capture.
% TX0/TX1 map to Doppler bins 0/32 at zero velocity.  The result is ordered
% TX0RX0..RX3, TX1RX0..RX3 and is relative to TX0RX0.

cfg = config_4tx4rx();
info = dir(filePath);
assert(~isempty(info), 'ADC file not found');
packetBytes = 28 + 8 + cfg.numChirps*(32 + cfg.numSamples*cfg.numRx*2);
assert(mod(info.bytes, packetBytes) == 0, 'Incomplete ADC packet');
nFrames = info.bytes / packetBytes;
assert(nFrames >= 4, 'At least four frames are needed for split validation');

rangeBin = 6; % Zero-based; 2.398 m with the current 512-point range FFT.
k = (0:cfg.numSamples-1)';
rangeWindow = 0.42 - 0.5*cos(2*pi*k/(cfg.numSamples-1)) ...
    + 0.08*cos(4*pi*k/(cfg.numSamples-1));
dopplerWindow = 0.5*(1-cos(2*pi*(1:cfg.numChirps)'/(cfg.numChirps+1)));
weight = double(radar.bpmCode(cfg.numChirps)).*dopplerWindow;
samples = complex(zeros(nFrames,8));
frameNumbers = zeros(nFrames,1);
for f = 1:nFrames
    frame = radar.readAdcFrame(filePath,f,cfg);
    frameNumbers(f) = frame.frameNumber;
    adc = frame.adc - mean(frame.adc,1);
    spectrum = fft(adc.*reshape(rangeWindow,[],1,1),cfg.rangeFftSize,1);
    doppler = fft(spectrum(rangeBin+1,:,:).*reshape(weight,1,[],1),cfg.dopplerFftSize,2);
    samples(f,:) = [reshape(doppler(1,1,:),1,4), ...
                    reshape(doppler(1,33,:),1,4)];
end
assert(all(diff(frameNumbers)==1), 'Capture contains frame-number gaps');

split = floor(nFrames/2);
ratios = samples(:,1)./samples;
coefficients = complex(median(real(ratios(1:split,:)),1), ...
                       median(imag(ratios(1:split,:)),1));
coefficients(1) = 1;
corrected = samples(split+1:end,:).*coefficients;
relative = corrected./corrected(:,1);
phaseErrorDeg = angle(relative)*180/pi;
gainError = abs(relative)-1;

result.coefficients = coefficients(:);
result.frameNumbers = frameNumbers;
result.rangeBin = rangeBin;
result.rangeM = rangeBin*cfg.rangeBinM;
result.trainingFrames = split;
result.validationFrames = nFrames-split;
result.maxMedianPhaseErrorDeg = max(abs(median(phaseErrorDeg,1)));
result.maxMedianGainErrorPercent = 100*max(abs(median(gainError,1)));
end
