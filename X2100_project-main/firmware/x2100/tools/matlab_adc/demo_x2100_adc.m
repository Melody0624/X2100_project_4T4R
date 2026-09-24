%% X2100 raw ADC processing demo
% Change dataFile to the adc_YYYYMMDD_HHMM_0.bin copied from the TF card.

clear; clc;
dataFile = "adc_YYYYMMDD_HHMM_0.bin";
frameIndex = 1;

frame = read_x2100_adc(char(dataFile), frameIndex);
result = x2100_process_frame(frame.adc);

fprintf('Frame number in packet: %u\n', frame.header.frameNumber);
fprintf('Packet length: %u bytes\n', frame.header.totalPacketLen);
fprintf('ADC min/max: %.0f / %.0f\n', min(frame.adc(:)), max(frame.adc(:)));

figure('Name', 'X2100 raw ADC');
tiledlayout(2, 2, 'Padding', 'compact', 'TileSpacing', 'compact');

nexttile;
plot(frame.adc(:, 1, 1)); grid on;
xlabel('ADC sample'); ylabel('Code');
title('RX1, chirp 1 raw ADC');

nexttile;
rangeProfileDb = 20*log10(max(abs(result.rangeFFT(:, 1, 1)), eps));
plot(result.rangeAxisM, rangeProfileDb); grid on;
xlabel('Range (m)'); ylabel('Magnitude (dB)');
title('RX1, chirp 1 range FFT');

nexttile;
fullRd = fftshift(result.dopplerMagnitudeSumRx, 2);
fullVelocity = fftshift(result.fullDopplerVelocityAxisMps);
imagesc(result.rangeAxisM, fullVelocity, ...
    20*log10(max(fullRd.', eps)));
axis xy; colorbar;
xlabel('Range (m)'); ylabel('Velocity bin (m/s)');
title('BPM-despread full Doppler spectrum');

nexttile;
imagesc(result.rangeAxisM, 0:result.config.dopplerBinsPerSubBand-1, ...
    result.rdMapDb.');
axis xy; colorbar;
xlabel('Range (m)'); ylabel('DDMA sub-Doppler index');
title('Firmware-equivalent DDMA RD map');

% The decoded velocity varies cell-by-cell because the chosen DDMA subband
% is different for each range/sub-Doppler cell. Use result.velocityMapMps
% together with result.rdMapDb for CFAR/detection work.
