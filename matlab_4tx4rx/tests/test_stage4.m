function tests = test_stage4
tests = functiontests(localfunctions);
end
function setupOnce(testCase)
testCase.applyFixture(matlab.unittest.fixtures.PathFixture( ...
    fileparts(fileparts(mfilename('fullpath')))));
end

function testResolverStatuses(testCase)
cfg = config_4tx4rx(); p=cfg.ddma;
patterns = [zeros(1,8); 10 10 10 10 0 0 0 0; ones(1,8); ...
    10 10 10 10 0 6 0 0; 10 .1 10 10 0 0 0 0; NaN zeros(1,7)];
for i=1:6
    d=radar.resolveBands(patterns(i,:),p);
    verifyEqual(testCase,double(d.status),i-1);
end
d=radar.resolveBands(ones(1,8),p);
verifyEqual(testCase,d.candidates,uint8(255));
p.minimumWinnerRatio=1;
d=radar.resolveBands(ones(1,8),p);
verifyEqual(testCase,d.status,uint8(5));
end

function testEveryVelocityBinAndChannelOrder(testCase)
cfg = config_4tx4rx();
cube=complex(zeros(256,128,4));
% One isolated row per physical signed bin; arbitrary distinct channel phasors.
z=exp(1i*(0:15)*0.17);
for b=0:127
    for tx=1:4
        cube(b+1,mod(b+(tx-1)*16,128)+1,:) = reshape(z((tx-1)*4+(1:4)),1,1,4);
    end
end
d=radar.decodeDdma(cube,cfg);
verifyEqual(testCase,nnz(d.valid),128);
for b=0:127
    k=mod(b,16)+1;
    verifyTrue(testCase,d.valid(b+1,k));
    verifyEqual(testCase,d.signedBin(b+1,k),mod(b+64,128)-64);
    verifyEqual(testCase,d.velocityMps(b+1,k),(mod(b+64,128)-64)*cfg.velocityBinMps,'AbsTol',1e-12);
    verifyEqual(testCase,reshape(d.virtualCube(b+1,k,:),1,16),z,'AbsTol',1e-12);
    verifyEqual(testCase,reshape(d.txDopplerBins(b+1,k,:),1,4),mod(b+(0:3)*16,128));
end
end

function testAdcPipelineAndPhase(testCase)
cfg=config_4tx4rx(); cfg.noiseStdCounts=0;
for velocity=[5 -7 25 -30]
    target=struct('rangeM',20,'velocityMps',velocity,'azimuthDeg',15,'amplitudeCounts',100);
    adc=radar.simulateAdc(cfg,target);
    r=radar.processRange(adc,cfg); f=radar.processDoppler(r.cube,cfg);
    d=radar.decodeDdma(f.cube,cfg);
    metric=d.amplitude; metric(~d.valid)=-Inf;
    [best,index]=max(metric(:)); verifyTrue(testCase,isfinite(best));
    [row,k]=ind2sub(size(metric),index);
    verifyLessThanOrEqual(testCase,abs(d.velocityMps(row,k)-velocity),cfg.velocityBinMps/2);
    verifyLessThanOrEqual(testCase,abs(r.rangeAxisM(row)-20),cfg.rangeBinM/2);
    z=reshape(d.virtualCube(row,k,:),16,1);
    position=reshape(cfg.rxPositionsM(:)+cfg.txPositionsM(:).',16,1);
    ideal=exp(1i*2*pi/cfg.lambdaM*(position-position(1))*sind(15));
    verifyLessThan(testCase,max(abs(angle((z/z(1))./ideal))),0.01);
end
end

function testOverlapRejectedWithEnergyRetained(testCase)
cfg=config_4tx4rx();
cube=complex(zeros(256,128,4));
% Equal targets at the same folded bin, one subband apart: five occupied bands.
for base=[7 23]
    for tx=0:3
        bin=mod(base+16*tx,128)+1;
        cube(51,bin,:)=cube(51,bin,:)+1;
    end
end
d=radar.decodeDdma(cube,cfg);
verifyEqual(testCase,d.status(51,8),uint8(2));
verifyFalse(testCase,d.valid(51,8));
verifyTrue(testCase,isnan(d.velocityMps(51,8)));
verifyTrue(testCase,all(isnan(d.virtualCube(51,8,:)),'all'));
verifyGreaterThan(testCase,d.amplitude(51,8),0);
verifyGreaterThan(testCase,nnz(bitget(d.candidates(51,8),1:8)),1);
end

function testTwoTargetsAtDifferentRanges(testCase)
cfg=config_4tx4rx(); cfg.noiseStdCounts=0;
t(1)=struct('rangeM',12,'velocityMps',-7,'azimuthDeg',-10,'amplitudeCounts',100);
t(2)=struct('rangeM',30,'velocityMps',18,'azimuthDeg',20,'amplitudeCounts',80);
adc=radar.simulateAdc(cfg,t);
r=radar.processRange(adc,cfg); f=radar.processDoppler(r.cube,cfg);
d=radar.decodeDdma(f.cube,cfg);
for target=t
    rows=find(abs(r.rangeAxisM-target.rangeM)<1);
    metric=d.amplitude(rows,:); metric(~d.valid(rows,:))=-Inf;
    [best,index]=max(metric(:)); verifyTrue(testCase,isfinite(best));
    [local,k]=ind2sub(size(metric),index); row=rows(local);
    verifyLessThanOrEqual(testCase,abs(d.velocityMps(row,k)-target.velocityMps),cfg.velocityBinMps/2);
end
end

function testEmptyAndBadProfile(testCase)
cfg=config_4tx4rx(); cube=zeros(256,128,4);
d=radar.decodeDdma(cube,cfg);
verifyEqual(testCase,nnz(d.valid),0);
verifyTrue(testCase,all(isnan(d.velocityMps(:))));
verifyTrue(testCase,all(isfinite(d.amplitudeDb(:))));
cfg.txSubbands=[0 2 4 6];
verifyError(testCase,@() radar.decodeDdma(cube,cfg),'radar:ddma:Profile');
end
