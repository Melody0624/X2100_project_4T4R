function result = demo_stage4(showPlots)
%DEMO_STAGE4 DDMA diagnostics; no CFAR or angle estimation yet.
if nargin<1, showPlots=true; end
result = demo_stage3(false);
cfg = result.cfg;
result.ddma = radar.decodeDdma(result.doppler.cube,cfg);
d = result.ddma;
% Pick the strongest accepted cell for diagnostics, not a thresholded detection.
metric = d.amplitude; metric(~d.valid)=-Inf;
[best,index] = max(metric(:));
result.ddmaPeak = [];
if isfinite(best)
    [r,k] = ind2sub(size(metric),index);
    result.ddmaPeak = struct('rangeRow',r,'foldedColumn',k, ...
        'rangeM',result.range.rangeAxisM(r),'velocityMps',d.velocityMps(r,k), ...
        'virtualSamples',reshape(d.virtualCube(r,k,:),16,1));
    fprintf('Strongest accepted cell: range %.4f m, velocity %.4f m/s (truth %.4f m/s)\n', ...
        result.ddmaPeak.rangeM,result.ddmaPeak.velocityMps,result.info.targets(1).velocityMps);
else
    fprintf('No accepted DDMA cells.\n');
end
if showPlots
    figure('Name','Stage 4 - DDMA diagnostic cells','Position',[100 100 1200 800]);
    tiledlayout(2,2);
    nexttile;
    imagesc(d.foldedBinIndices,result.range.rangeAxisM,d.amplitudeDb-max(d.amplitudeDb(:)));
    axis xy; ylim([0 50]); clim([-60 0]); colorbar;
    xlabel('Folded Doppler bin'); ylabel('Range (m)'); title('Folded amplitude, including rejected cells (dB)');
    nexttile;
    imagesc(d.foldedBinIndices,result.range.rangeAxisM,double(d.status));
    axis xy; ylim([0 50]); clim([-0.5 5.5]);
    colorbar('Ticks',0:5,'TickLabels',d.statusNames);
    xlabel('Folded Doppler bin'); ylabel('Range (m)'); title('DDMA status (not target detections)');
    nexttile;
    if ~isempty(result.ddmaPeak)
        bins = (k-1)+(0:7)*(cfg.dopplerFftSize/8);
        bands = sum(abs(result.doppler.cube(r,bins+1,:)),3);
        bar(0:7,bands/max(bands));
    end
    xlabel('Subband index'); ylabel('Normalized RX magnitude sum');
    title('Eight subbands at strongest accepted cell'); grid on;
    nexttile;
    if ~isempty(result.ddmaPeak)
        z = result.ddmaPeak.virtualSamples;
        plot(0:15,unwrap(angle(z/z(1))),'o-'); hold on;
        positions = result.info.virtualPositionsM;
        truth = 2*pi/cfg.lambdaM*(positions-positions(1))*sind(result.info.targets(1).azimuthDeg);
        plot(0:15,truth,'--'); legend('Extracted phase','Ideal single-target truth');
    end
    xlabel('Virtual channel (TX-major)'); ylabel('Relative phase (rad)');
    title('16 channels: phase retained, no calibration applied'); grid on;
end
end
