function out = detectAndEstimate(ddma,rangeAxisM,cfg)
%DETECTANDESTIMATE CFAR and DDMA validity intersection, then per-cell AoA.
% No truth inputs, installation rotation, tracking or empirical filtering.
out.cfar=radar.cfar2d(ddma.amplitudeDb,cfg);
if ~isequal(size(ddma.valid),size(out.cfar.mask)) || ...
        ~isequal(size(ddma.virtualCube),[size(out.cfar.mask) 16]) || ...
        numel(rangeAxisM)~=size(out.cfar.mask,1)
    error('radar:detection:Size','Inconsistent DDMA or range dimensions.');
end
validateattributes(cfg.cfar.maxDetections,{'numeric'},{'scalar','integer','positive'});
out.acceptedMask=out.cfar.mask & ddma.valid;
[rows,cols]=find(out.acceptedMask);
linear=sub2ind(size(out.acceptedMask),rows,cols);
[~,order]=sort(ddma.amplitude(linear),'descend');
out.overflowCount=max(0,numel(order)-cfg.cfar.maxDetections);
order=order(1:min(numel(order),cfg.cfar.maxDetections));
template=struct('rangeRow',0,'foldedColumn',0,'rangeM',0,'velocityMps',0, ...
    'azimuthDeg',0,'amplitudeDb',0,'cfarMarginDb',0,'ddmaQuality',0);
out.detections=repmat(template,0,1);
out.angles=cell(numel(order),1);
out.outputMask=false(size(out.acceptedMask));
for n=1:numel(order)
    r=rows(order(n)); k=cols(order(n));
    angle=radar.estimateAngle(reshape(ddma.virtualCube(r,k,:),16,1),cfg);
    detection=template;
    detection.rangeRow=r; detection.foldedColumn=k;
    detection.rangeM=rangeAxisM(r); detection.velocityMps=ddma.velocityMps(r,k);
    detection.azimuthDeg=angle.azimuthDeg;
    detection.amplitudeDb=ddma.amplitudeDb(r,k);
    detection.cfarMarginDb=out.cfar.marginDb(r,k);
    detection.ddmaQuality=ddma.quality(r,k);
    out.detections(n,1)=detection;
    out.angles{n}=angle;
    out.outputMask(r,k)=true;
end
end
