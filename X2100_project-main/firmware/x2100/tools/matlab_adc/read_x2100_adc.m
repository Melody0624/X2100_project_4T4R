function frame = read_x2100_adc(filename, frameIndex)
%READ_X2100_ADC Read one raw X2100/Cheetah ADC frame saved by frames_save.c.
%
% frame = read_x2100_adc(filename, frameIndex)
%
% ADC output layout is [sample, chirp, rx] = [506, 128, 4]. Values are
% converted exactly as the firmware does: bitshift(raw_uint16, -4) - 2048.

arguments
    filename (1, :) char
    frameIndex (1, 1) double {mustBeInteger, mustBePositive} = 1
end

cfg.numSamples = 506;
cfg.numChirps = 128;
cfg.numRx = 4;
cfg.chirpHeaderBytes = 32;
cfg.adcBytes = cfg.numChirps * ...
    (cfg.chirpHeaderBytes + cfg.numSamples * cfg.numRx * 2);
cfg.packetHeaderBytes = 28;
cfg.tlvHeaderBytes = 8;
cfg.magic = uint16([hex2dec('0102'), hex2dec('0304'), ...
                    hex2dec('0506'), hex2dec('0708')]);

fid = fopen(filename, 'rb', 'ieee-le');
if fid < 0
    error('X2100:OpenFailed', 'Cannot open file: %s', filename);
end
cleaner = onCleanup(@() fclose(fid)); %#ok<NASGU>

for currentFrame = 1:frameIndex
    frameStart = ftell(fid);
    magic = fread(fid, 4, 'uint16=>uint16').';
    if numel(magic) ~= 4
        error('X2100:FrameMissing', ...
            'Frame %d is not present or is incomplete.', currentFrame);
    end
    if ~isequal(magic, cfg.magic)
        error('X2100:BadMagic', ...
            'Bad magic at byte %d. This is not an X2100 raw packet.', frameStart);
    end

    header.version = readScalar(fid, 'uint32=>uint32', 'version');
    header.totalPacketLen = readScalar(fid, 'uint32=>uint32', 'packet length');
    header.platform = readScalar(fid, 'uint8=>uint8', 'platform');
    mustRead(fid, 3, 'uint8=>uint8', 'reserved header bytes');
    header.frameNumber = readScalar(fid, 'uint32=>uint32', 'frame number');
    header.numTLVs = readScalar(fid, 'uint8=>uint8', 'TLV count');
    mustRead(fid, 3, 'uint8=>uint8', 'reserved header bytes');

    if header.numTLVs ~= 1
        error('X2100:UnexpectedTLVCount', ...
            'Frame %d contains %d TLVs; raw capture must contain one.', ...
            currentFrame, header.numTLVs);
    end

    tlv.type = readScalar(fid, 'uint32=>uint32', 'TLV type');
    tlv.length = readScalar(fid, 'uint32=>uint32', 'TLV length');
    extraBytes = double(tlv.length) - cfg.adcBytes;
    expectedPacketLen = cfg.packetHeaderBytes + cfg.tlvHeaderBytes + ...
        double(tlv.length);

    if double(header.totalPacketLen) ~= expectedPacketLen
        error('X2100:BadPacketLength', ...
            'Packet length is %d, expected %d from its TLV.', ...
            header.totalPacketLen, expectedPacketLen);
    end
    if extraBytes < 0 || mod(extraBytes, 4) ~= 0
        error('X2100:BadADCSize', ...
            'TLV length %d cannot contain a %d-byte ADC frame.', ...
            tlv.length, cfg.adcBytes);
    end

    angleData = mustRead(fid, extraBytes / 4, 'int32=>int32', ...
        'optional angle data').';
    adc = zeros(cfg.numSamples, cfg.numChirps, cfg.numRx, 'double');
    chirpHeader = zeros(cfg.chirpHeaderBytes / 2, cfg.numChirps, 'uint16');

    for chirp = 1:cfg.numChirps
        chirpHeader(:, chirp) = mustRead(fid, cfg.chirpHeaderBytes / 2, ...
            'uint16=>uint16', 'chirp header');
        % fread([numRx, numSamples]) preserves the sample-major/RX-fastest
        % interleave produced by the Cheetah CSI interface.
        raw = mustRead(fid, cfg.numRx * cfg.numSamples, ...
            'uint16=>uint16', 'ADC samples');
        raw = reshape(raw, cfg.numRx, cfg.numSamples).';
        converted = double(bitshift(raw, -4)) - 2048.0;
        adc(:, chirp, :) = reshape(converted, ...
            cfg.numSamples, 1, cfg.numRx);
    end

    nextFrame = frameStart + double(header.totalPacketLen);
    if ftell(fid) ~= nextFrame
        error('X2100:ParserOffset', ...
            'Parser ended at byte %d, packet ends at byte %d.', ...
            ftell(fid), nextFrame);
    end

    if currentFrame == frameIndex
        frame.adc = adc;
        frame.chirpHeader = chirpHeader;
        frame.angleData = angleData;
        frame.header = header;
        frame.tlv = tlv;
        frame.config = cfg;
        frame.fileOffset = frameStart;
    end
end
end

function value = readScalar(fid, precision, fieldName)
value = mustRead(fid, 1, precision, fieldName);
end

function data = mustRead(fid, count, precision, fieldName)
if count == 0
    data = zeros(0, 1);
    return;
end
[data, actual] = fread(fid, count, precision);
if actual ~= count
    error('X2100:UnexpectedEOF', ...
        'Unexpected end of file while reading %s (%d/%d values).', ...
        fieldName, actual, count);
end
end
