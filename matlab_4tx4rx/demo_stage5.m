function result=demo_stage5(showPlots)
%DEMO_STAGE5 Synthetic ADC through CFAR and azimuth estimation.
if nargin<1, showPlots=true; end
result=demo_stage4(false);
result.detection=radar.detectAndEstimate(result.ddma,result.range.rangeAxisM,result.cfg);
det=result.detection.detections;
fprintf('CFAR + DDMA: %d detections, overflow %d\n',numel(det),result.detection.overflowCount);
disp(struct2table(det));
if showPlots
    figure('Name','Stage 5 - CFAR and azimuth (synthetic)','Position',[100 100 1200 800]);
    tiledlayout(2,2);
    nexttile;
    imagesc(result.ddma.foldedBinIndices,result.range.rangeAxisM, ...
        result.ddma.amplitudeDb-max(result.ddma.amplitudeDb(:)));
    axis xy; ylim([0 50]); clim([-60 0]); colorbar; hold on;
    if ~isempty(det), plot([det.foldedColumn]-1,[det.rangeM],'rx','LineWidth',1.5); end
    xlabel('Folded Doppler bin'); ylabel('Range (m)'); title('CFAR + DDMA accepted detections');
    nexttile;
    if ~isempty(det)
        k=det(1).foldedColumn;
        plot(result.range.rangeAxisM,result.ddma.amplitudeDb(:,k)); hold on;
        plot(result.range.rangeAxisM,result.detection.cfar.rangeThresholdDb(:,k),'--');
        plot(result.range.rangeAxisM,result.detection.cfar.dopplerThresholdDb(:,k),':');
        legend('Amplitude','Range threshold','Doppler threshold');
    end
    xlim([0 50]); grid on; xlabel('Range (m)'); ylabel('Unnormalized amplitude (dB)');
    title('Thresholds at strongest detection column');
    nexttile; hold on;
    for n=1:min(numel(det),8)
        a=result.detection.angles{n}; plot(a.axisDeg,max(a.relativePowerDb,-60));
    end
    for t=result.info.targets, xline(t.azimuthDeg,'--','Truth'); end
    xlim([-90 90]); ylim([-60 5]); grid on;
    xlabel('Azimuth (deg)'); ylabel('Relative power (dB)'); title('Angle spectra (up to eight detections)');
    nexttile; hold on;
    if ~isempty(det), scatter([det.velocityMps],[det.rangeM],50,[det.azimuthDeg],'filled'); end
    for t=result.info.targets, plot(t.velocityMps,t.rangeM,'kx','MarkerSize',10); end
    colorbar; clim([-60 60]); grid on; ylim([0 50]);
    velocityLimit=result.cfg.lambdaM/(4*result.cfg.chirpPeriodS);
    xlim([-velocityLimit velocityLimit]);
    xlabel('Decoded velocity (m/s)'); ylabel('Range (m)'); title('Detections colored by azimuth; crosses = truth');
end
end
