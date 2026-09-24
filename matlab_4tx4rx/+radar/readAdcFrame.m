function frame=readAdcFrame(filePath,frameIndex,cfg)
%READADCFRAME Strict existing ADC-only DAT format, not a generic TLV parser.
% 28-byte packet header, one 8-byte TLV (type 13, payload-only length),
% 32-byte chirp headers, RX-interleaved little-endian ADC words.
% Packet metadata does NOT identify TX coding; caller must match RF config.
validateattributes(frameIndex,{'numeric'},{'scalar','integer','positive'});
payloadBytes=cfg.numChirps*(32+cfg.numSamples*cfg.numRx*2);
packetBytes=28+8+payloadBytes;
fid=fopen(filePath,'rb');
if fid<0, error('radar:io:Open','Cannot open ADC file.'); end
cleanup=onCleanup(@() fclose(fid)); %#ok<NASGU>
fseek(fid,0,'eof'); fileBytes=ftell(fid);
if fileBytes==0 || mod(fileBytes,packetBytes)~=0
    error('radar:io:FileLength','Expected complete fixed ADC-only packets, with no trailing bytes.');
end
frameCount=fileBytes/packetBytes;
if frameIndex>frameCount, error('radar:io:FrameIndex','Frame index exceeds file boundary.'); end
if fseek(fid,(frameIndex-1)*packetBytes,'bof')~=0, error('radar:io:Seek','Seek failed.'); end
bytes=fread(fid,packetBytes,'*uint8');
if numel(bytes)~=packetBytes, error('radar:io:ShortRead','Incomplete packet.'); end
if ~isequal(bytes(1:8).',cfg.io.magic)
    error('radar:io:Magic','Invalid packet magic.');
end
if u32(bytes(13:16))~=packetBytes || bytes(25)~=1 || ...
        u32(bytes(29:32))~=13 || u32(bytes(33:36))~=payloadBytes
    error('radar:io:Header','Packet length/TLV type/count/length does not match ADC-only profile.');
end
payload=reshape(bytes(37:end),32+cfg.numSamples*cfg.numRx*2,cfg.numChirps);
frame.chirpHeaders=payload(1:32,:); % Preserve opaque fields; semantics unknown.
b=payload(33:end,:);
words=uint16(b(1:2:end,:))+bitshift(uint16(b(2:2:end,:)),8);
frame.rawWords=permute(reshape(words,cfg.numRx,cfg.numSamples,cfg.numChirps),[2 3 1]);
if any(bitand(frame.rawWords(:),uint16(15))~=0)
    error('radar:io:AdcLowBits','ADC words contain nonzero low four bits.');
end
codes=bitshift(frame.rawWords,-4);
frame.saturatedFraction=nnz(codes==0 | codes==4095)/numel(codes);
validateattributes(cfg.io.maxSaturatedFraction,{'numeric'},{'scalar','finite','>=',0,'<=',1});
if frame.saturatedFraction>cfg.io.maxSaturatedFraction
    error('radar:io:Saturation','ADC saturation fraction exceeds the configured limit.');
end
frame.adc=double(codes)-2048;
frame.fileIndex=frameIndex; frame.frameCount=frameCount;
frame.frameNumber=u32(bytes(21:24));
frame.source=char(filePath);
frame.waveformVerified=false;
end
function value=u32(b)
value=double(b(1))+256*double(b(2))+65536*double(b(3))+16777216*double(b(4));
end
