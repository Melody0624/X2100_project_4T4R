function fig = plot_4rx_adc_dat(filePath, frameIndex, chirpIndex, outputPath)
%PLOT_4RX_ADC_DAT Plot one chirp from the four physical RX ADC channels.
%   plot_4rx_adc_dat() plots frame 1, chirp 1 of the sample capture.
%   plot_4rx_adc_dat(FILE, FRAME, CHIRP, PNG) also saves the figure.

if nargin < 1 || isempty(filePath)
    filePath = ['D:/xwechat/xwechat_files/wxid_3f8betvfbj0322_c74a/' ...
        'msg/file/2026-08/海开宝/20260728/' ...
        'MotorCycle_Tools release[2.4.1]/data/Record_2026_09_29/' ...
        'Record_20260929_173038_adc.dat'];
end
if nargin < 2 || isempty(frameIndex), frameIndex = 1; end
if nargin < 3 || isempty(chirpIndex), chirpIndex = 1; end
if nargin < 4, outputPath = ''; end

cfg = config_4tx4rx();
validateattributes(chirpIndex, {'numeric'}, ...
    {'scalar', 'integer', '>=', 1, '<=', cfg.numChirps});
frame = radar.readAdcFrame(filePath, frameIndex, cfg);

fig = figure('Color', 'w', 'Name', '4RX ADC');
layout = tiledlayout(fig, 2, 2, 'TileSpacing', 'compact');
for rx = 1:cfg.numRx
    nexttile(layout);
    plot(0:cfg.numSamples-1, frame.adc(:, chirpIndex, rx));
    grid on;
    title(sprintf('RX%d', rx - 1));
    xlabel('ADC sample');
    ylabel('ADC code');
    xlim([0 cfg.numSamples-1]);
end
title(layout, sprintf('Frame %u (file index %d), chirp %d: physical RX ADC', ...
    frame.frameNumber, frameIndex, chirpIndex));
if ~isempty(outputPath)
    exportgraphics(fig, outputPath, 'Resolution', 180);
end
end
