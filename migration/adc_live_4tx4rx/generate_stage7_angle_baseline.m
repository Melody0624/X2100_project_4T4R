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
angle = x2100.detection.estimateAngles( ...
    detections, doppler.dopplerCube, ddma, cfg, cfg.stage4.angleCalibration);

fprintf('ANGLE_WINDOW=');
fprintf('%.9g ', angle.angleWindow);
fprintf('\n');
fprintf('ANGLE_AXIS_SELECTED=');
axisBins = [0 1 2 31 32 62 63 64 65 95 126 127];
for bin = axisBins
    fprintf('{%u,%.9g} ', bin, angle.angleAxisDeg(bin + 1));
end
fprintf('\n');

spectrumBins = [0 1 2 3 31 32 63 64 95 126 127];
fprintf('ANGLE_DETECTIONS=%u\n', numel(angle.detections));
for k = 1:numel(angle.detections)
    det = angle.detections(k);
    spectrum = angle.angleSpectra(k, :);
    virtual = angle.virtualAntennas(k, :);
    fprintf(['ANGLE_DET%u r=%u d=%u sb=%u peak=%u azimuth=%.9g ' ...
        'peakmag=%.9g sum=%.9g energy=%.9g\n'], ...
        k - 1, det.rangeBin, det.subDopplerBin, det.selectedSubband, ...
        det.anglePeakBin, det.azimuthDeg, spectrum(det.anglePeakBin + 1), ...
        sum(spectrum), sum(spectrum.^2));
    fprintf('ANGLE_VIRTUAL%u=', k - 1);
    for n = 1:numel(virtual)
        fprintf('{%.9g,%.9g} ', real(virtual(n)), imag(virtual(n)));
    end
    fprintf('\n');
    fprintf('ANGLE_SPECTRUM%u=', k - 1);
    for bin = spectrumBins
        fprintf('{%u,%.9g} ', bin, spectrum(bin + 1));
    end
    fprintf('\n');
end
