function out = resolveBands(bands, policy)
%RESOLVEBANDS Conservative cyclic four-active/eight-total subband template.
% Mirrors C ddma_resolve decisions in double precision, not bit-for-bit.
% status: 0 empty, 1 resolved, 2 ambiguous, 3 contaminated, 4 weak, 5 invalid.
% anchor and candidate bit positions are ZERO based; quality is not probability.
out = struct('status',uint8(5),'anchor',0,'candidates',uint8(0), ...
    'best',0,'runnerUp',0,'emptyMax',0,'amplitude',0,'quality',0);
p = [policy.minimumAmplitude policy.minimumTxToPeak ...
    policy.minimumWinnerRatio policy.minimumEmptyContrast];
if numel(p)~=4 || ~isreal(p) || any(~isfinite(p)) || p(1)<=0 || ...
        p(2)<=0 || p(2)>1 || p(3)<=1 || p(4)<=1
    return;
end
if numel(bands)~=8 || ~isreal(bands) || any(~isfinite(bands(:))) || any(bands(:)<0)
    return;
end
bands = double(bands(:).');
peak = max(bands);
if peak<=p(1), out.status=uint8(0); return; end
scores = zeros(1,8);
for anchor=0:7
    scores(anchor+1)=min(bands(mod(anchor+(0:3),8)+1));
end
[out.best,winner] = max(scores);
out.anchor = winner-1;
out.runnerUp = max(scores([1:winner-1 winner+1:8]));
for anchor=0:7
    if scores(anchor+1)>p(1) && scores(anchor+1)*p(3)>=out.best
        out.candidates = bitset(out.candidates,anchor+1);
    end
end
active = mod(out.anchor+(0:3),8)+1;
empty = mod(out.anchor+(4:7),8)+1;
out.amplitude = sum(bands(active));
out.emptyMax = max(bands(empty));
if ~isfinite(out.amplitude), return; end
if out.best<=p(1) || out.best<peak*p(2)
    out.status=uint8(4);
elseif out.best<=out.runnerUp*p(3)
    out.status=uint8(2);
elseif out.best<=out.emptyMax*p(4)
    out.status=uint8(3);
else
    out.status=uint8(1);
    out.quality=floor(max(0,min(100,100*(1-max(out.runnerUp,out.emptyMax)/out.best))));
end
end
