function out=makePointCloud(detections,cfg)
%MAKEPOINTCLOUD Radar x=forward, y=positive azimuth; installation is CCW.
% Range/FOV filtering is in radar coordinates, before rigid transformation.
% Radial velocity is retained; it is not a 2D object velocity estimate.
p=cfg.pointCloud;
validateattributes(p.installAngleDeg,{'numeric'},{'scalar','real','finite'});
validateattributes(p.translationM,{'numeric'},{'vector','numel',2,'real','finite'});
validateattributes(p.minRangeM,{'numeric'},{'scalar','nonnegative','finite'});
validateattributes(p.maxRangeM,{'numeric'},{'scalar','>=',p.minRangeM});
validateattributes(p.maxAbsAzimuthDeg,{'numeric'},{'scalar','finite','>=',0,'<=',90});
template=struct('rangeM',0,'radialVelocityMps',0,'azimuthDeg',0, ...
    'radarXM',0,'radarYM',0,'worldXM',0,'worldYM',0, ...
    'amplitudeDb',0,'cfarMarginDb',0,'ddmaQuality',0);
out.points=repmat(template,0,1);
out.keepMask=false(numel(detections),1);
rotation=[cosd(p.installAngleDeg) -sind(p.installAngleDeg); ...
    sind(p.installAngleDeg) cosd(p.installAngleDeg)];
for n=1:numel(detections)
    d=detections(n);
    if ~all(isfinite([d.rangeM d.azimuthDeg d.velocityMps])) || ...
            d.rangeM<p.minRangeM || d.rangeM>p.maxRangeM || abs(d.azimuthDeg)>p.maxAbsAzimuthDeg
        continue;
    end
    out.keepMask(n)=true;
    xy=d.rangeM*[cosd(d.azimuthDeg);sind(d.azimuthDeg)];
    world=rotation*xy+p.translationM(:);
    q=template;
    q.rangeM=d.rangeM; q.radialVelocityMps=d.velocityMps; q.azimuthDeg=d.azimuthDeg;
    q.radarXM=xy(1); q.radarYM=xy(2); q.worldXM=world(1); q.worldYM=world(2);
    q.amplitudeDb=d.amplitudeDb; q.cfarMarginDb=d.cfarMarginDb; q.ddmaQuality=d.ddmaQuality;
    out.points(end+1,1)=q; %#ok<AGROW>
end
out.filteredCount=numel(detections)-numel(out.points);
end
