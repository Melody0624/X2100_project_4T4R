function tests = test_stage3
tests = functiontests(localfunctions);
end
function setupOnce(testCase)
testCase.applyFixture(matlab.unittest.fixtures.PathFixture( ...
    fileparts(fileparts(mfilename('fullpath')))));
end

function testSignedToneAndPhase(testCase)
cfg = config_4tx4rx();
m = 0:127;
for bin = [0 9 -11 -64 63]
    tone = exp(1i*(2*pi*bin*m/128+0.4));
    input = repmat(tone.*cfg.bpmCode.',256,1,4);
    out = radar.processDoppler(input,cfg);
    [~,peak] = max(out.power(1,:));
    verifyEqual(testCase,peak,mod(bin,128)+1);
    verifyEqual(testCase,out.cube(1,peak,1),sum(out.window)*exp(1i*0.4),'AbsTol',1e-10);
    [~,shiftedPeak] = max(out.shiftedPower(1,:));
    verifyEqual(testCase,out.apparentVelocityAxisMps(shiftedPeak),bin*cfg.velocityBinMps,'AbsTol',1e-12);
end
end

function testFourTxEndToEnd(testCase)
cfg = config_4tx4rx(); cfg.noiseStdCounts = 0;
for baseBin = [0 9 -11 60]
    target = struct('rangeM',20,'velocityMps',baseBin*cfg.velocityBinMps, ...
        'azimuthDeg',15,'amplitudeCounts',100);
    adc = radar.simulateAdc(cfg,target);
    r = radar.processRange(adc,cfg);
    out = radar.processDoppler(r.cube,cfg);
    p = out.power(r.peakBin,:);
    expected = sort(mod(baseBin+(0:3)*16,128)+1);
    [~,order] = sort(p,'descend');
    verifyEqual(testCase,sort(order(1:4)),expected);
    badCfg = cfg; badCfg.bpmCode = circshift(cfg.bpmCode,1);
    bad = radar.processDoppler(r.cube,badCfg);
    verifyGreaterThan(testCase,sum(p(expected)),5*sum(bad.power(r.peakBin,expected)));
end
end

function testEmptyAndValidation(testCase)
cfg = config_4tx4rx();
input = zeros(256,128,4);
out = radar.processDoppler(input,cfg);
verifyFalse(testCase,out.hasSignal);
verifyTrue(testCase,all(isfinite(out.relativePowerDb(:))));
verifyEqual(testCase,nnz(out.cube),0);
verifyError(testCase,@() radar.processDoppler(input(:,:,1),cfg),'radar:doppler:CubeSize');
cfg.bpmCode(1) = NaN;
verifyError(testCase,@() radar.processDoppler(input,cfg),'radar:doppler:BpmCode');
end
