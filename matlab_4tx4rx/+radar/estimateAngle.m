function out = estimateAngle(samples,cfg)
%ESTIMATEANGLE Apply complex correction multipliers, then estimate azimuth.
% Half-wavelength ULA: FFT + parabolic interpolation in spatial frequency.
% Other 1D positions: conventional beamforming on the configured scan grid.
validateattributes(samples,{'double','single'},{'vector','numel',16,'finite'});
validateattributes(cfg.calibration,{'double','single'},{'vector','numel',16,'finite'});
validateattributes(cfg.lambdaM,{'numeric'},{'scalar','finite','positive'});
validateattributes(cfg.txPositionsM,{'numeric'},{'vector','numel',4,'real','finite'});
validateattributes(cfg.rxPositionsM,{'numeric'},{'vector','numel',4,'real','finite'});
position=reshape(cfg.rxPositionsM(:)+cfg.txPositionsM(:).',16,1);
out.correctedSamples=double(samples(:)).*double(cfg.calibration(:));
if ~any(abs(out.correctedSamples)>0)
    error('radar:angle:Empty','Angle is undefined for an all-zero aperture.');
end
window=0.5*(1-cos(2*pi*(1:16).'/(16+1)));
z=out.correctedSamples.*window;
if max(abs(diff(position)-cfg.lambdaM/2))<cfg.lambdaM*1e-8
    validateattributes(cfg.angle.fftSize,{'numeric'},{'scalar','integer','>=',16});
    N=cfg.angle.fftSize;
    assert(mod(N,2)==0,'Angle FFT size must be even.');
    out.method='half-wave-ULA-FFT';
    magnitude=abs(fftshift(fft(z,N)));
    spatial=(-N/2:N/2-1).'/N;
    out.axisDeg=asind(2*spatial);
    [~,peak]=max(magnitude);
    delta=0;
    % Do not interpolate across the -90/+90 wrap boundary.
    if peak>1 && peak<N
        triplet=magnitude(peak-1:peak+1);
        denom=triplet(1)-2*triplet(2)+triplet(3);
        if denom<0, delta=max(-0.5,min(0.5,0.5*(triplet(1)-triplet(3))/denom)); end
    end
    out.azimuthDeg=asind(max(-1,min(1,2*(spatial(peak)+delta/N))));
else
    validateattributes(cfg.angle.scanAxisDeg,{'numeric'}, ...
        {'vector','real','finite','nonempty','>=',-90,'<=',90});
    out.method='position-beamforming';
    out.axisDeg=cfg.angle.scanAxisDeg(:);
    steering=exp(1i*2*pi/cfg.lambdaM*(position-position(1))*sind(out.axisDeg.'));
    magnitude=abs(steering'*z);
    [~,peak]=max(magnitude);
    out.azimuthDeg=out.axisDeg(peak);
end
out.relativePowerDb=20*log10(max(magnitude,realmin)/max(magnitude));
end
