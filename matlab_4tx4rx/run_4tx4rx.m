function run=run_4tx4rx(source,cfg,frameIndices)
%RUN_4TX4RX Process a targets struct (one synthetic frame) or an ADC DAT path.
% File mode requires explicit cfg; DAT headers cannot identify 2TX versus 4TX.
% Returns compact per-frame results. For intermediate cubes use processFrame.
if nargin<2 || isempty(cfg)
    if ischar(source) || isstring(source)
        error('radar:run:Config','File replay requires explicit acquisition-matched cfg.');
    end
    cfg=config_4tx4rx();
end
if nargin<3, frameIndices=1; end
validateattributes(frameIndices,{'numeric'},{'vector','integer','positive','nonempty'});
run.cfg=cfg;
run.synthetic=isstruct(source);
run.waveformVerified=false;
run.frames=cell(numel(frameIndices),1);
run.frameNumberGapAfter=[];
previous=[];
if run.synthetic && ~isequal(frameIndices,1)
    error('radar:run:Simulation','Targets input generates one frame; use frameIndices=1.');
end
for n=1:numel(frameIndices)
    if run.synthetic
        [adc,~,info]=radar.simulateAdc(cfg,source);
        number=0; fileIndex=1; run.source='synthetic';
        run.truth=info.targets;
    else
        frame=radar.readAdcFrame(source,frameIndices(n),cfg);
        adc=frame.adc; number=frame.frameNumber; fileIndex=frame.fileIndex;
        run.source=char(source);
    end
    result=radar.processFrame(adc,cfg);
    summary=struct('fileIndex',fileIndex,'frameNumber',number, ...
        'elapsedS',result.elapsedS,'detections',result.detection.detections, ...
        'pointCloud',result.pointCloud,'overflowCount',result.detection.overflowCount);
    run.frames{n}=summary;
    if ~isempty(previous) && mod(number-previous,2^32)~=1
        run.frameNumberGapAfter(end+1)=n-1; %#ok<AGROW>
    end
    previous=number;
end
end
