function cfg = config_4tx4rx()
%CONFIG_4TX4RX Ideal simulation profile; not a verified RF/antenna profile.
cfg.c = 299792458;
cfg.numTx = 4;
cfg.numRx = 4;
cfg.numSamples = 506;
cfg.numChirps = 128;
cfg.sampleRateHz = 26.665e6;
cfg.centerFrequencyHz = 76.5e9;
cfg.slopeHzPerS = 19.531e12;
cfg.chirpPeriodS = 26e-6;
cfg.rangeFftSize = 512;
cfg.dopplerFftSize = 128;
cfg.numSubbands = 8;
cfg.txSubbands = 0:3;
cfg.lambdaM = cfg.c / cfg.centerFrequencyHz;
% TX-major virtual order: TX1/RX1..4, TX2/RX1..4, ...
cfg.rxPositionsM = (0:3) * cfg.lambdaM / 2;
cfg.txPositionsM = (0:3) * 4 * cfg.lambdaM / 2;
cfg.calibration = ones(16, 1);
cfg.rangeBinM = cfg.c * cfg.sampleRateHz / (2 * cfg.slopeHzPerS * cfg.rangeFftSize);
cfg.rangeResolutionM = cfg.c * cfg.sampleRateHz / (2 * cfg.slopeHzPerS * cfg.numSamples);
cfg.velocityBinMps = cfg.lambdaM / (2 * cfg.chirpPeriodS * cfg.dopplerFftSize);
cfg.noiseStdCounts = 2;
cfg.seed = 2100;
cfg.adcOffset = 2048;
cfg.adcMax = 4095;
cfg.adcShift = 4;
cfg.bpmCode = double(radar.bpmCode(cfg.numChirps));
% Engineering defaults from migration/adc_live_4tx4rx_bpm_ddma/radar_config.h.
% Amplitude ratios, not power ratios or calibrated false-alarm thresholds.
cfg.ddma.minimumAmplitude = 1e-9;
cfg.ddma.minimumTxToPeak = 0.04;
cfg.ddma.minimumWinnerRatio = 1.5;
cfg.ddma.minimumEmptyContrast = 2;
cfg.cfar.doppler = struct('training',4,'guard',2,'thresholdDb',6,'mode','CASO');
cfg.cfar.range = struct('training',4,'guard',2,'thresholdDb',3,'mode','CAGO');
cfg.cfar.maxDetections = 256;
cfg.angle.fftSize = 128;
cfg.angle.scanAxisDeg = -90:0.1:90;
cfg.framePeriodS = 50.328e-3;
cfg.pointCloud.installAngleDeg = 0;
cfg.pointCloud.translationM = [0 0];
cfg.pointCloud.minRangeM = 0.5;
cfg.pointCloud.maxRangeM = Inf;
cfg.pointCloud.maxAbsAzimuthDeg = 60;
cfg.io.magic = uint8([2 1 4 3 6 5 8 7]);
cfg.io.maxSaturatedFraction = 0.01;
end
