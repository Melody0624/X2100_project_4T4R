function tests = test_stage2
tests = functiontests(localfunctions);
end

function setupOnce(testCase)
testCase.applyFixture(matlab.unittest.fixtures.PathFixture( ...
    fileparts(fileparts(mfilename('fullpath')))));
end

function testKnownRange(testCase)
r = demo_stage2(false);
verifySize(testCase,r.range.cube,[256 128 4]);
verifyLessThanOrEqual(testCase,abs(r.range.peakRangeM-20),r.cfg.rangeBinM/2);
verifyGreaterThan(testCase,norm(imag(r.range.cube(:))),0);
verifyEqual(testCase,r.range.rangeAxisM(2),r.cfg.rangeBinM,'AbsTol',1e-12);
end

function testDcAndEmpty(testCase)
cfg = config_4tx4rx();
adc = repmat(reshape(1:512,1,128,4),506,1,1);
r = radar.processRange(adc,cfg);
verifyEqual(testCase,nnz(r.cube),0);
verifyFalse(testCase,r.hasSignal);
verifyTrue(testCase,isnan(r.peakRangeM));
verifyTrue(testCase,all(isfinite(r.relativePowerDb)));
end

function testTwoRangesAndDcInvariance(testCase)
cfg = config_4tx4rx(); cfg.noiseStdCounts = 0;
targets(1) = struct('rangeM',12,'velocityMps',-3,'azimuthDeg',-10,'amplitudeCounts',100);
targets(2) = struct('rangeM',30,'velocityMps',5,'azimuthDeg',20,'amplitudeCounts',100);
[adc,~,info] = radar.simulateAdc(cfg,targets);
verifyEqual(testCase,info.clippedFraction,0);
r = radar.processRange(adc,cfg);
rOffset = radar.processRange(adc+200,cfg);
verifyEqual(testCase,rOffset.cube,r.cube,'AbsTol',1e-9);
for distance = [12 30]
    ids = find(abs(r.rangeAxisM-distance)<2);
    [~,local] = max(r.meanPower(ids));
    verifyLessThanOrEqual(testCase,abs(r.rangeAxisM(ids(local))-distance),cfg.rangeBinM/2);
end
end

function testPhasePreserved(testCase)
cfg = config_4tx4rx();
n = (0:cfg.numSamples-1).';
tone = 100*cos(2*pi*50*n/cfg.rangeFftSize+0.7);
r = radar.processRange(repmat(tone,1,128,4),cfg);
verifyEqual(testCase,angle(r.cube(51,1,1)),0.7,'AbsTol',1e-4);
verifyError(testCase,@() radar.processRange(zeros(506,128,3),cfg),'radar:range:AdcSize');
end
