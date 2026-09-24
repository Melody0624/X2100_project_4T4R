projectDir = 'D:\downloads\X2100_project-main\X2100_project-latest\matlab';
filePath = fullfile(projectDir, 'Record_20260814_161551_adc.dat');
addpath(projectDir);

cfg = x2100_default_config();
frame = x2100.io.readFrame(filePath, 1, cfg);
rangeProcessed = x2100.dsp.processRangeFrame(frame, cfg);
doppler = x2100.dsp.dopplerFft(rangeProcessed.rangeCube, cfg);
ddma = x2100.dsp.ddmaDecode(doppler.noncoherentMagnitude, cfg);
cfar = x2100.detection.firmwareCfar2d(ddma.rdMapDb, cfg);
detections = x2100.detection.extractDetections(ddma.rdMapDb, cfar, ddma, cfg);

fprintf(['CFAR_COUNTS doppler_detection=%u range_detection=%u final=%u ' ...
    'is_peak=%u extracted=%u\n'], nnz(cfar.dopplerDetection), ...
    nnz(cfar.rangeDetection), nnz(cfar.detectionMask), ...
    nnz(cfar.isPeakMask), numel(detections));
activeZero = find(cfar.activeDopplerLines) - 1;
fprintf('CFAR_ACTIVE_LINES=');
fprintf('%u ', activeZero);
fprintf('\n');
fprintf('CFAR_SUMS doppler_threshold=%.9g range_threshold=%.9g\n', ...
    sum(cfar.dopplerThresholdDb, 'all'), ...
    sum(cfar.rangeThresholdDb, 'all'));

selectedPairsZero = [ ...
    0 0; 0 7; 0 15; 0 31; ...
    7 0; 7 7; 7 15; 7 31; ...
    8 0; 8 7; 8 15; 8 31; ...
    31 3; 63 7; 127 15; 191 23; 255 31];
fprintf('CFAR_SELECTED=');
for k = 1:size(selectedPairsZero, 1)
    rangeBin = selectedPairsZero(k, 1);
    dopplerBin = selectedPairsZero(k, 2);
    index = sub2ind(size(ddma.rdMapDb), rangeBin + 1, dopplerBin + 1);
    fprintf('{%u,%u,%.9g,%.9g,%u,%u,%.9g,%u,%u,%u,%u} ', ...
        rangeBin, dopplerBin, ddma.rdMapDb(index), ...
        cfar.dopplerThresholdDb(index), cfar.dopplerPeak(index), ...
        cfar.dopplerDetection(index), cfar.rangeThresholdDb(index), ...
        cfar.rangePeak(index), cfar.rangeDetection(index), ...
        cfar.detectionMask(index), cfar.isPeakMask(index));
end
fprintf('\n');

fprintf('CFAR_DETECTIONS=');
[rangeIndices, dopplerIndices] = find(cfar.detectionMask);
% Firmware output order is range outer, Doppler inner; MATLAB find already
% returns range first, then column, so sort explicitly by range and Doppler.
pairs = sortrows([rangeIndices - 1, dopplerIndices - 1], [1 2]);
for k = 1:size(pairs, 1)
    r = pairs(k, 1);
    d = pairs(k, 2);
    index = sub2ind(size(ddma.rdMapDb), r + 1, d + 1);
    snr = ddma.rdMapDb(index) - cfar.dopplerThresholdDb(index) + ...
        cfg.stage4.dopplerCfar.thresholdDb;
    fprintf('{%u,%u,%.9g,%.9g,%u,%u} ', r, d, ...
        ddma.rdMapDb(index), snr, cfar.isPeakMask(index), ...
        ddma.bestSubband(index));
end
fprintf('\n');
