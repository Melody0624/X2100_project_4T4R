function tests=test_stage6
tests=functiontests(localfunctions);
end
function setupOnce(testCase)
testCase.applyFixture(matlab.unittest.fixtures.PathFixture(fileparts(fileparts(mfilename('fullpath')))));
end
function setup(testCase)
fixture=testCase.applyFixture(matlab.unittest.fixtures.TemporaryFolderFixture);
testCase.TestData.folder=fixture.Folder;
end

function testCoordinatesAndFiltering(testCase)
cfg=config_4tx4rx(); cfg.pointCloud.installAngleDeg=90; cfg.pointCloud.translationM=[1 2];
d=struct('rangeM',10,'velocityMps',-3,'azimuthDeg',0,'amplitudeDb',40,'cfarMarginDb',6,'ddmaQuality',99);
out=radar.makePointCloud(d,cfg);
verifyEqual(testCase,[out.points.worldXM out.points.worldYM],[1 12],'AbsTol',1e-12);
verifyEqual(testCase,out.points.radialVelocityMps,-3);
d(2)=d; d(2).azimuthDeg=70;
d(3)=d(1); d(3).rangeM=0.1;
out=radar.makePointCloud(d,cfg);
verifyEqual(testCase,out.filteredCount,2);
verifyEqual(testCase,out.keepMask,[true;false;false]);
end

function testIndependentWordLayout(testCase)
cfg=config_4tx4rx();
% Different values on every sample/chirp/RX axis detect layout permutations.
codes=uint16(mod((0:505)'+reshape(0:127,1,128)*3+reshape(0:3,1,1,4)*211,3000)+500);
words=bitshift(codes,4);
path=fullfile(testCase.TestData.folder,'layout.dat');
writeBytes(path,packet(words,123));
frame=radar.readAdcFrame(path,1,cfg);
verifyEqual(testCase,frame.rawWords,words);
verifyEqual(testCase,frame.adc,double(codes)-2048);
verifyEqual(testCase,frame.frameNumber,123);
verifyEqual(testCase,frame.chirpHeaders,repmat(uint8((0:31)'),1,128));
end

function testMalformedFiles(testCase)
cfg=config_4tx4rx(); words=repmat(uint16(32768),506,128,4);
b=packet(words,0); path=fullfile(testCase.TestData.folder,'invalid.dat');
bad=b; bad(1)=0; writeBytes(path,bad);
verifyError(testCase,@() radar.readAdcFrame(path,1,cfg),'radar:io:Magic');
bad=b; bad(29)=14; writeBytes(path,bad);
verifyError(testCase,@() radar.readAdcFrame(path,1,cfg),'radar:io:Header');
writeBytes(path,b(1:end-1));
verifyError(testCase,@() radar.readAdcFrame(path,1,cfg),'radar:io:FileLength');
bad=b; bad(69)=1; writeBytes(path,bad);
verifyError(testCase,@() radar.readAdcFrame(path,1,cfg),'radar:io:AdcLowBits');
writeBytes(path,packet(zeros(506,128,4,'uint16'),0));
verifyError(testCase,@() radar.readAdcFrame(path,1,cfg),'radar:io:Saturation');
writeBytes(path,b);
verifyError(testCase,@() radar.readAdcFrame(path,2,cfg),'radar:io:FrameIndex');
verifyError(testCase,@() run_4tx4rx(path),'radar:run:Config');
end

function testReplayMatchesSimulationAndFrameGap(testCase)
cfg=config_4tx4rx();
t=struct('rangeM',20,'velocityMps',5,'azimuthDeg',15,'amplitudeCounts',100);
[~,words]=radar.simulateAdc(cfg,t);
path=fullfile(testCase.TestData.folder,'replay.dat');
writeBytes(path,[packet(words,10);packet(words,12)]);
sim=run_4tx4rx(t,cfg);
replay=run_4tx4rx(path,cfg,1:2);
verifyEqual(testCase,replay.frames{1}.pointCloud,sim.frames{1}.pointCloud);
verifyEqual(testCase,replay.frames{2}.pointCloud,sim.frames{1}.pointCloud);
verifyEqual(testCase,replay.frameNumberGapAfter,1);
verifyFalse(testCase,replay.synthetic);
verifyFalse(testCase,replay.waveformVerified);
writeBytes(path,[packet(words,2^32-1);packet(words,0)]);
replay=run_4tx4rx(path,cfg,1:2);
verifyEmpty(testCase,replay.frameNumberGapAfter);
end

function testExportAndEmptyFrame(testCase)
cfg=config_4tx4rx(); cfg.noiseStdCounts=0;
run=run_4tx4rx(struct([]),cfg);
verifyEmpty(testCase,run.frames{1}.pointCloud.points);
files=radar.exportRun(run,fullfile(testCase.TestData.folder,'empty'));
saved=load(files.mat);
verifyEqual(testCase,saved.run,run);
doc=jsondecode(fileread(files.json));
verifyTrue(testCase,doc.synthetic);
verifyEmpty(testCase,doc.frames.pointCloud.points);
end

function testDemoTargets(testCase)
run=demo_stage6(false);
points=run.frames{1}.pointCloud.points;
verifyGreaterThanOrEqual(testCase,numel(points),2);
for target=run.truth
    match=abs([points.rangeM]-target.rangeM)<run.cfg.rangeBinM/2 & ...
        abs([points.radialVelocityMps]-target.velocityMps)<run.cfg.velocityBinMps/2;
    verifyTrue(testCase,any(match));
end
end

function b=packet(words,number)
% Test fixture only: framing known synthetic words, not a hardware capture.
header=zeros(28,1,'uint8'); header(1:8)=uint8([2 1 4 3 6 5 8 7]);
header(13:16)=le32(522276); header(21:24)=le32(number); header(25)=1;
payload=zeros(4080,128,'uint8');
payload(1:32,:)=repmat(uint8((0:31)'),1,128);
for chirp=1:128
    w=reshape(squeeze(words(:,chirp,:)).',[],1);
    payload(33:2:end,chirp)=uint8(bitand(w,255));
    payload(34:2:end,chirp)=uint8(bitshift(w,-8));
end
b=[header;le32(13);le32(522240);payload(:)];
end
function bytes=le32(value)
bytes=uint8(mod(floor(double(value)./256.^(0:3)),256)).';
end
function writeBytes(path,bytes)
fid=fopen(path,'wb'); cleanup=onCleanup(@() fclose(fid)); %#ok<NASGU>
fwrite(fid,bytes,'uint8');
end
