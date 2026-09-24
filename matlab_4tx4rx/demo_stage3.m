function result = demo_stage3(showPlots)
%DEMO_STAGE3 Generate raw DDMA Doppler spectra; TX separation comes next.
if nargin < 1, showPlots = true; end
result = demo_stage2(false);
cfg = result.cfg;
result.doppler = radar.processDoppler(result.range.cube,cfg);
d = result.doppler;
target = result.info.targets(1);
% Truth markers are simulation diagnostics, not estimated TX assignments.
expectedHz = 2*target.velocityMps/cfg.lambdaM + ...
    cfg.txSubbands/(cfg.numSubbands*cfg.chirpPeriodS);
prf = 1/cfg.chirpPeriodS;
expectedHz = mod(expectedHz+prf/2,prf)-prf/2;
result.expectedTxApparentVelocityMps = expectedHz*cfg.lambdaM/2;
fprintf('Doppler cube: %d range bins x %d Doppler bins x %d RX\n',size(d.cube));
fprintf('TX1..4 expected apparent velocities (NOT decoded):');
fprintf(' %.4f',result.expectedTxApparentVelocityMps); fprintf(' m/s\n');
if showPlots
    figure('Name','Stage 3 - BPM and raw DDMA Doppler','Position',[100 100 1200 800]);
    tiledlayout(2,2);
    nexttile;
    imagesc(d.apparentVelocityAxisMps,result.range.rangeAxisM,d.relativePowerDb);
    axis xy; ylim([0 50]); clim([-60 0]); colorbar;
    xlabel('Apparent velocity (m/s), before DDMA decoding'); ylabel('Range (m)');
    title('BPM removed: raw DDMA range-Doppler power');
    nexttile;
    row = result.range.peakBin;
    p = d.shiftedPower(row,:);
    plot(d.apparentVelocityAxisMps,10*log10(max(p,realmin)/max(max(p),realmin)));
    hold on;
    for tx = 1:cfg.numTx
        xline(result.expectedTxApparentVelocityMps(tx),'--',sprintf('TX%d truth',tx));
    end
    grid on; ylim([-80 5]); xlabel('Apparent velocity (m/s)'); ylabel('Relative power (dB)');
    title(sprintf('Range %.3f m: four TX spectral copies',result.range.peakRangeM));
    nexttile;
    before = fftshift(fft(result.range.cube .* d.window,cfg.dopplerFftSize,2),2);
    beforePower = mean(abs(before).^2,3);
    commonReference = max(d.shiftedPower(:));
    imagesc(d.apparentVelocityAxisMps,result.range.rangeAxisM, ...
        10*log10(max(beforePower,realmin)/max(commonReference,realmin)));
    axis xy; ylim([0 50]); clim([-60 0]); colorbar;
    xlabel('Apparent velocity (m/s)'); ylabel('Range (m)');
    title('Before BPM removal (same power reference)');
    nexttile;
    stairs(0:cfg.numChirps-1,cfg.bpmCode); hold on;
    plot(0:cfg.numChirps-1,d.window,'LineWidth',1.3);
    ylim([-1.2 1.2]); grid on; xlabel('Chirp index'); ylabel('Weight');
    legend('Common BPM code','Slow-time window'); title('BPM and firmware window');
end
end
