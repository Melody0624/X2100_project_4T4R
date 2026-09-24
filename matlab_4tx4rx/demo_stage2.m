function result = demo_stage2(showPlots)
%DEMO_STAGE2 Synthetic ADC -> fast-time preprocessing -> range spectrum.
if nargin < 1, showPlots = true; end
result = demo_stage1(false);
result.range = radar.processRange(result.adc,result.cfg);
r = result.range;
fprintf('Range cube: %d bins x %d chirps x %d RX\n',size(r.cube));
fprintf('Strongest range bin: %.4f m (target %.4f m)\n', ...
    r.peakRangeM,result.info.targets(1).rangeM);
if showPlots
    figure('Name','Stage 2 - synthetic range processing');
    tiledlayout(2,2);
    nexttile;
    plot(0:result.cfg.numSamples-1,result.adc(:,1,1)); hold on;
    plot(0:result.cfg.numSamples-1,r.windowed(:,1,1));
    xlabel('Sample index'); ylabel('ADC counts'); grid on;
    title('First chirp / RX1'); legend('Input','DC removed + Blackman');
    nexttile;
    plot(r.rangeAxisM,max(r.relativePowerDb,-80)); hold on;
    xline(result.info.targets(1).rangeM,'--','Truth');
    xlim([0 50]); ylim([-80 5]); grid on;
    xlabel('Range (m)'); ylabel('Relative power (dB)');
    title('Power averaged over chirps and RX');
    nexttile;
    reference = max(r.powerPerRx(:));
    perRxDb = 10*log10(max(r.powerPerRx,realmin)/max(reference,realmin));
    plot(r.rangeAxisM,max(perRxDb,-80));
    xlim([0 50]); ylim([-80 5]); grid on;
    xlabel('Range (m)'); ylabel('Relative power (dB)');
    title('Per-RX range spectra (common reference)'); legend('RX1','RX2','RX3','RX4');
    nexttile;
    powerMap = abs(r.cube(:,:,1)).^2;
    mapDb = 10*log10(max(powerMap,realmin)/max(max(powerMap(:)),realmin));
    imagesc(0:result.cfg.numChirps-1,r.rangeAxisM,mapDb);
    axis xy; ylim([0 50]); clim([-60 0]); colorbar;
    xlabel('Chirp index'); ylabel('Range (m)');
    title('RX1 range / chirp power (dB relative to maximum)');
end
end
