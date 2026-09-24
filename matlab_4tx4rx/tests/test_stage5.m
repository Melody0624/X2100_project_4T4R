function tests=test_stage5
tests=functiontests(localfunctions);
end
function setupOnce(testCase)
testCase.applyFixture(matlab.unittest.fixtures.PathFixture( ...
    fileparts(fileparts(mfilename('fullpath')))));
end

function testCfarThresholdsAndEdges(testCase)
cfg=config_4tx4rx(); map=20*ones(256,16);
map(50,1)=40; map(1,8)=40;
c=radar.cfar2d(map,cfg);
verifyEqual(testCase,c.dopplerThresholdDb(50,1),26);
verifyEqual(testCase,c.rangeThresholdDb(50,1),23);
verifyTrue(testCase,c.mask(50,1));
verifyFalse(testCase,c.mask(1,8));
% Doppler CASO selects low side; range CAGO selects high side.
map=20*ones(256,16); map(50,2:5)=40; map(53:56,8)=40;
c=radar.cfar2d(map,cfg);
verifyEqual(testCase,c.dopplerThresholdDb(50,8),26);
verifyEqual(testCase,c.rangeThresholdDb(50,8),43);
cfg.cfar.doppler.training=7;
verifyError(testCase,@() radar.cfar2d(map,cfg),'radar:cfar:Window');
end

function testAngleSignAndCalibration(testCase)
cfg=config_4tx4rx();
p=reshape(cfg.rxPositionsM(:)+cfg.txPositionsM(:).',16,1);
gain=(0.7+(0:15)'/20).*exp(1i*(0:15)'*0.31);
cfg.calibration=1./gain;
for truth=[-55 -15 0 15 55]
    z=exp(1i*2*pi/cfg.lambdaM*p*sind(truth)).*gain;
    a=radar.estimateAngle(z,cfg);
    verifyEqual(testCase,a.method,'half-wave-ULA-FFT');
    verifyLessThan(testCase,abs(a.azimuthDeg-truth),0.1);
end
verifyError(testCase,@() radar.estimateAngle(zeros(16,1),cfg),'radar:angle:Empty');
end

function testIrregularArray(testCase)
cfg=config_4tx4rx(); cfg.txPositionsM(3)=cfg.txPositionsM(3)+0.13*cfg.lambdaM;
p=reshape(cfg.rxPositionsM(:)+cfg.txPositionsM(:).',16,1);
z=exp(1i*2*pi/cfg.lambdaM*p*sind(-23.4));
a=radar.estimateAngle(z,cfg);
verifyEqual(testCase,a.method,'position-beamforming');
verifyEqual(testCase,a.azimuthDeg,-23.4,'AbsTol',0.051);
end

function testSingleAndTwoTargets(testCase)
cfg=config_4tx4rx();
t(1)=struct('rangeM',12,'velocityMps',-7,'azimuthDeg',-20,'amplitudeCounts',120);
t(2)=struct('rangeM',30,'velocityMps',18,'azimuthDeg',25,'amplitudeCounts',100);
for count=1:2
    out=pipeline(cfg,t(1:count)); det=out.detections;
    for target=t(1:count)
        near=find(abs([det.rangeM]-target.rangeM)<=cfg.rangeBinM/2 & ...
            abs([det.velocityMps]-target.velocityMps)<=cfg.velocityBinMps/2);
        verifyNotEmpty(testCase,near);
        if ~isempty(near)
            verifyLessThan(testCase,abs(det(near(1)).azimuthDeg-target.azimuthDeg),0.3);
        end
    end
end
end

function testEmptyNoiseAndRejectedCells(testCase)
cfg=config_4tx4rx(); cfg.noiseStdCounts=0;
out=pipeline(cfg,struct([]));
verifyEmpty(testCase,out.detections);
cfg.noiseStdCounts=2;
out=pipeline(cfg,struct([]));
fprintf('Stage 5 fixed-seed noise-only detections: %d (not a measured Pfa)\n',numel(out.detections));
verifyTrue(testCase,all(isfinite([out.detections.azimuthDeg])));
% A strong CFAR peak rejected by DDMA must not become an output detection.
ddma=radar.decodeDdma(zeros(256,128,4),cfg);
ddma.amplitudeDb(:)=20; ddma.amplitudeDb(50,8)=80;
out=radar.detectAndEstimate(ddma,(0:255)'*cfg.rangeBinM,cfg);
verifyTrue(testCase,out.cfar.mask(50,8));
verifyEmpty(testCase,out.detections);
end

function testDetectionLimit(testCase)
cfg=config_4tx4rx(); cfg.cfar.maxDetections=1;
ddma=radar.decodeDdma(zeros(256,128,4),cfg);
ddma.amplitudeDb(:)=20;
for r=[40 80]
    ddma.amplitudeDb(r,8)=40+r/10; ddma.amplitude(r,8)=10^(ddma.amplitudeDb(r,8)/20);
    ddma.valid(r,8)=true; ddma.velocityMps(r,8)=0;
    ddma.virtualCube(r,8,:)=ones(1,1,16);
end
out=radar.detectAndEstimate(ddma,(0:255)'*cfg.rangeBinM,cfg);
verifyEqual(testCase,numel(out.detections),1);
verifyEqual(testCase,out.detections.rangeRow,80);
verifyEqual(testCase,out.overflowCount,1);
verifyEqual(testCase,nnz(out.outputMask),1);
end

function out=pipeline(cfg,targets)
adc=radar.simulateAdc(cfg,targets);
r=radar.processRange(adc,cfg);
f=radar.processDoppler(r.cube,cfg);
d=radar.decodeDdma(f.cube,cfg);
out=radar.detectAndEstimate(d,r.rangeAxisM,cfg);
end
