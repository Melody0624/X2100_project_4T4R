function out = processDoppler(rangeCube, cfg)
%PROCESSDOPPLER Common BPM removal followed by slow-time window and FFT.
% cube uses unshifted Doppler bins 0..N-1; shiftedCube is for display only.
% DDMA modulation remains: apparent velocity is NOT decoded target velocity.
validateattributes(rangeCube, {'double','single'}, {'finite','nonempty'});
if ~isequal(size(rangeCube),[cfg.rangeFftSize/2 cfg.numChirps cfg.numRx])
    error('radar:doppler:CubeSize','Expected [rangeFftSize/2,numChirps,numRx].');
end
if numel(cfg.bpmCode) ~= cfg.numChirps || ...
        ~isreal(cfg.bpmCode) || any(abs(cfg.bpmCode(:)) ~= 1)
    error('radar:doppler:BpmCode','BPM must contain one real +1/-1 per chirp.');
end
validateattributes(cfg.dopplerFftSize, {'numeric'}, ...
    {'scalar','integer','>=',cfg.numChirps});
assert(mod(cfg.dopplerFftSize,2)==0,'Doppler FFT size must be even.');
validateattributes(cfg.chirpPeriodS, {'numeric'}, {'scalar','finite','positive'});
validateattributes(cfg.lambdaM, {'numeric'}, {'scalar','finite','positive'});
out.despread = double(rangeCube) .* reshape(double(cfg.bpmCode),1,[],1);
% Match the firmware window formula, using double precision here.
% No slow-time mean removal: stationary targets are retained.
out.window = 0.5*(1-cos(2*pi*(1:cfg.numChirps)/(cfg.numChirps+1)));
out.windowed = out.despread .* out.window;
out.cube = fft(out.windowed,cfg.dopplerFftSize,2);
out.shiftedCube = fftshift(out.cube,2);
out.power = mean(abs(out.cube).^2,3);
out.shiftedPower = fftshift(out.power,2);
out.relativePowerDb = 10*log10(max(out.shiftedPower,realmin) / ...
    max(max(out.shiftedPower(:)),realmin));
out.frequencyAxisHz = (-cfg.dopplerFftSize/2:cfg.dopplerFftSize/2-1) / ...
    (cfg.dopplerFftSize*cfg.chirpPeriodS);
out.apparentVelocityAxisMps = out.frequencyAxisHz*cfg.lambdaM/2;
out.unshiftedBinIndices = 0:cfg.dopplerFftSize-1;
out.hasSignal = any(out.power(:)>0);
end
