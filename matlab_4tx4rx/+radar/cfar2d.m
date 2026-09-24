function out = cfar2d(amplitudeDb,cfg)
%CFAR2D Cyclic Doppler CASO / range CAGO on the unmasked folded dB map.
% Firmware convention: average dB values, not linear power. No Pfa claim.
validateattributes(amplitudeDb,{'double','single'},{'2d','real','finite','nonempty'});
if ~isequal(size(amplitudeDb),[cfg.rangeFftSize/2 cfg.dopplerFftSize/cfg.numSubbands])
    error('radar:cfar:Size','Unexpected folded RD map dimensions.');
end
map=double(amplitudeDb);
out.dopplerThresholdDb=threshold(map,cfg.cfar.doppler,2);
out.rangeThresholdDb=threshold(map,cfg.cfar.range,1);
% Deterministic plateau tie-break; exclude physical range edge bins.
dopPeak=map>=circshift(map,[0 1]) & map>circshift(map,[0 -1]);
rngPeak=map>=circshift(map,[1 0]) & map>circshift(map,[-1 0]);
rngPeak([1 end],:)=false;
out.dopplerMask=map>out.dopplerThresholdDb & dopPeak;
out.rangeMask=map>out.rangeThresholdDb & rngPeak;
out.mask=out.dopplerMask & out.rangeMask;
out.marginDb=min(map-out.dopplerThresholdDb,map-out.rangeThresholdDb);
end

function value=threshold(map,p,dim)
validateattributes(p.training,{'numeric'},{'scalar','integer','positive'});
validateattributes(p.guard,{'numeric'},{'scalar','integer','nonnegative'});
validateattributes(p.thresholdDb,{'numeric'},{'scalar','finite','positive'});
if 2*(p.training+p.guard)+1>size(map,dim)
    error('radar:cfar:Window','Training and guard windows must not overlap cyclically.');
end
left=zeros(size(map)); right=left;
for offset=p.guard+(1:p.training)
    shift=[0 0]; shift(dim)=offset;
    left=left+circshift(map,shift);
    right=right+circshift(map,-shift);
end
switch upper(p.mode)
    case 'CASO', noise=min(left,right)/p.training;
    case 'CAGO', noise=max(left,right)/p.training;
    case 'CA', noise=(left+right)/(2*p.training);
    otherwise, error('radar:cfar:Mode','Unknown CFAR mode.');
end
value=noise+p.thresholdDb;
end
