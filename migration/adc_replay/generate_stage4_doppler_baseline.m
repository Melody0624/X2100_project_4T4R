projectDir = 'D:\downloads\X2100_project-main\X2100_project-latest\matlab';
filePath = fullfile(projectDir, 'Record_20260814_161551_adc.dat');
addpath(projectDir);

cfg = x2100_default_config();
frame = x2100.io.readFrame(filePath, 1, cfg);
rangeProcessed = x2100.dsp.processRangeFrame(frame, cfg);
doppler = x2100.dsp.dopplerFft(rangeProcessed.rangeCube, cfg);

signatureRangeZero = [0 8 7 8];
selectedDopplerZero = [0 1 2 3 7 15 31 47 63 95 126 127];

fprintf('FRAME_NUMBER=%u\n', frame.header.frameNumber);
fprintf('SELECTED_DOPPLER_ZERO=');
fprintf('%u ', selectedDopplerZero);
fprintf('\n');

for rx = 1:cfg.adc.numRx
    cube = doppler.dopplerCube(:, :, rx);
    energy = sum(abs(cube).^2, 'all');
    [peakMagnitude, linearIndex] = max(abs(cube), [], 'all');
    [peakRange, peakDoppler] = ind2sub(size(cube), linearIndex);
    fprintf('RX%d signature_range=%u peak=(range=%u,doppler=%u,mag=%.9g) energy=%.9g\n', ...
        rx - 1, signatureRangeZero(rx), peakRange - 1, peakDoppler - 1, ...
        peakMagnitude, energy);
    fprintf('RX%d selected=', rx - 1);
    values = cube(signatureRangeZero(rx) + 1, selectedDopplerZero + 1);
    for k = 1:numel(values)
        fprintf('{%.9g,%.9g} ', real(values(k)), imag(values(k)));
    end
    fprintf('\n');
end

noncoherent = doppler.noncoherentMagnitude;
[peakMagnitude, linearIndex] = max(noncoherent, [], 'all');
[peakRange, peakDoppler] = ind2sub(size(noncoherent), linearIndex);
fprintf('NONCOHERENT peak=(range=%u,doppler=%u,mag=%.9g) sum=%.9g energy=%.9g\n', ...
    peakRange - 1, peakDoppler - 1, peakMagnitude, ...
    sum(noncoherent, 'all'), sum(noncoherent.^2, 'all'));

selectedPairsZero = [ ...
    0 0; 0 32; 0 64; 0 96; ...
    7 0; 7 32; 7 64; 7 96; ...
    8 0; 8 32; 8 64; 8 96; ...
    31 7; 63 15; 127 31; 191 63; 255 127];
fprintf('NONCOHERENT_SELECTED=');
for k = 1:size(selectedPairsZero, 1)
    rangeBin = selectedPairsZero(k, 1);
    dopplerBin = selectedPairsZero(k, 2);
    fprintf('{%u,%u,%.9g} ', rangeBin, dopplerBin, ...
        noncoherent(rangeBin + 1, dopplerBin + 1));
end
fprintf('\n');

window = x2100.preprocess.firmwareHanningWindow(cfg.adc.numChirps);
fprintf('HANNING edge=(%.9g,%.9g) center=(%.9g,%.9g) sum=%.9g\n', ...
    window(1), window(end), window(64), window(65), sum(window));
