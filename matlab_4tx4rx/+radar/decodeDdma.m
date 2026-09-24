function out = decodeDdma(cube,cfg)
%DECODEDDMA Unshifted Doppler cube -> folded map and virtual-channel samples.
% One single-target hypothesis per range/folded-Doppler cell, NOT detections.
validateattributes(cube,{'double','single'},{'finite','nonempty'});
if ~isequal(size(cube),[cfg.rangeFftSize/2 cfg.dopplerFftSize cfg.numRx])
    error('radar:ddma:CubeSize','Expected unshifted [range,Doppler,RX] cube.');
end
if cfg.numTx~=4 || cfg.numRx~=4 || cfg.numSubbands~=8 || ...
        ~isequal(cfg.txSubbands(:).',0:3) || mod(cfg.dopplerFftSize,8)~=0
    error('radar:ddma:Profile','Decoder requires four consecutive TX offsets 0:3 and eight subbands.');
end
check = radar.resolveBands(zeros(1,8),cfg.ddma);
if check.status==5, error('radar:ddma:Policy','Invalid DDMA policy.'); end
validateattributes(cfg.chirpPeriodS,{'numeric'},{'scalar','finite','positive'});
validateattributes(cfg.lambdaM,{'numeric'},{'scalar','finite','positive'});
R = size(cube,1); N = cfg.dopplerFftSize; K = N/8;
% Sum RX magnitudes, matching firmware noncoherent amplitude convention.
amplitude = sum(abs(double(cube)),3);
out.statusNames = {'empty','resolved','ambiguous','contaminated','weak','invalid'};
out.status = zeros(R,K,'uint8');
out.candidates = zeros(R,K,'uint8');
out.anchor = zeros(R,K);
out.best = zeros(R,K); out.runnerUp = zeros(R,K);
out.emptyMax = zeros(R,K); out.quality = zeros(R,K);
out.amplitude = zeros(R,K);
out.velocityMps = nan(R,K); out.signedBin = nan(R,K);
out.txDopplerBins = nan(R,K,4);
out.virtualCube = complex(nan(R,K,16),nan(R,K,16));
for folded=0:K-1
    bins = folded+(0:7)*K+1;
    for range=1:R
        decision = radar.resolveBands(amplitude(range,bins),cfg.ddma);
        out.status(range,folded+1) = decision.status;
        out.candidates(range,folded+1) = decision.candidates;
        out.anchor(range,folded+1) = decision.anchor;
        out.best(range,folded+1) = decision.best;
        out.runnerUp(range,folded+1) = decision.runnerUp;
        out.emptyMax(range,folded+1) = decision.emptyMax;
        out.quality(range,folded+1) = decision.quality;
        % Rejected energy retained for future CFAR background estimation.
        out.amplitude(range,folded+1) = sum(amplitude(range,bins(mod(decision.anchor+(0:3),8)+1)));
        if decision.status~=1, continue; end
        base = folded+decision.anchor*K;
        signed = mod(base+N/2,N)-N/2;
        out.signedBin(range,folded+1) = signed;
        out.velocityMps(range,folded+1) = signed*cfg.lambdaM/(2*N*cfg.chirpPeriodS);
        for tx=1:4
            txBin = mod(base+(tx-1)*K,N);
            out.txDopplerBins(range,folded+1,tx) = txBin;
            channels = (tx-1)*4+(1:4);
            out.virtualCube(range,folded+1,channels) = reshape(cube(range,txBin+1,:),1,1,4);
        end
    end
end
out.valid = out.status==1;
out.foldedBinIndices = 0:K-1;
out.amplitudeDb = 20*log10(max(out.amplitude,realmin));
% All index values stored in fields are zero based except MATLAB subscripts.
% Calibration is deliberately deferred to angle estimation.
end
