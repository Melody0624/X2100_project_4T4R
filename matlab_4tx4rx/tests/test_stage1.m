function tests = test_stage1
tests = functiontests(localfunctions);
end

function setupOnce(testCase)
root = fileparts(fileparts(mfilename('fullpath')));
testCase.applyFixture(matlab.unittest.fixtures.PathFixture(root));
end

function testWordEncodingAndDeterminism(testCase)
r = demo_stage1(false);
verifySize(testCase, r.adc, [506 128 4]);
verifyClass(testCase, r.rawWords, 'uint16');
verifyEqual(testCase, bitand(r.rawWords,uint16(15)), zeros(size(r.rawWords),'uint16'));
verifyEqual(testCase, r.info.clippedFraction, 0);
[adc, words] = radar.simulateAdc(r.cfg,r.info.targets);
verifyEqual(testCase, adc,r.adc);
verifyEqual(testCase, words,r.rawWords);
end

function testEmptyAndClipping(testCase)
cfg = config_4tx4rx(); cfg.noiseStdCounts = 0;
[adc, words] = radar.simulateAdc(cfg,struct([]));
verifyEqual(testCase, nnz(adc),0);
verifyTrue(testCase,all(words(:)==32768));
t = struct('rangeM',0,'velocityMps',0,'azimuthDeg',0,'amplitudeCounts',2000);
[~,~,info] = radar.simulateAdc(cfg,t);
verifyGreaterThan(testCase,info.clippedFraction,0);
end

function testKnownBeatAndSpatialPhase(testCase)
cfg = config_4tx4rx(); cfg.noiseStdCounts = 0;
cfg.bpmCode(:) = 1;
% At chirp zero, DDMA phases vanish. Compare a closed-form TX array sum.
t = struct('rangeM',10,'velocityMps',0,'azimuthDeg',30,'amplitudeCounts',100);
[adc,~,~] = radar.simulateAdc(cfg,t);
sample = (0:cfg.numSamples-1).';
fr = 2*cfg.slopeHzPerS*t.rangeM/cfg.c;
for rx = 1:4
    steering = exp(1i*2*pi/cfg.lambdaM * ...
        (cfg.txPositionsM + cfg.rxPositionsM(rx))*sind(t.azimuthDeg));
    expected = real(100*sum(steering)*exp(1i*2*pi*fr*sample/cfg.sampleRateHz));
    verifyEqual(testCase,adc(:,1,rx),round(expected),'AbsTol',1);
end
verifyEqual(testCase,cfg.rangeBinM,0.399703411,'AbsTol',1e-8);
end
