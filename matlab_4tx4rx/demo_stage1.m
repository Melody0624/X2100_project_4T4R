function result = demo_stage1(showPlots)
%DEMO_STAGE1 Generate one synthetic 4TX4RX ADC frame.
if nargin < 1, showPlots = true; end
cfg = config_4tx4rx();
targets = struct('rangeM', 20, 'velocityMps', 5, ...
    'azimuthDeg', 15, 'amplitudeCounts', 150, 'phaseRad', 0);
[adc, rawWords, info] = radar.simulateAdc(cfg, targets);
result = struct('cfg',cfg,'adc',adc,'rawWords',rawWords,'info',info);
fprintf('SYNTHETIC ADC ONLY: %d samples x %d chirps x %d RX\n', size(adc));
fprintf('Clipping: %.4f%%; range FFT bin: %.6f m\n', ...
    100*info.clippedFraction, cfg.rangeBinM);
if showPlots
    figure('Name','Stage 1 - synthetic 4TX4RX ADC');
    tiledlayout(2,1);
    nexttile;
    plot((0:cfg.numSamples-1)/cfg.sampleRateHz*1e6, squeeze(adc(:,1,:)));
    xlabel('Fast time (us)'); ylabel('ADC counts');
    title('First chirp: summed TX echoes at four RX');
    legend('RX1','RX2','RX3','RX4'); grid on;
    nexttile;
    imagesc(0:cfg.numChirps-1, 0:cfg.numSamples-1, adc(:,:,1));
    axis xy; colorbar; xlabel('Chirp index'); ylabel('Sample index');
    title('RX1 raw ADC (synthetic)');
end
end
