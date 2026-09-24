param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [ValidateRange(1, 3600)]
    [int]$Seconds = 120,

    [ValidateRange(1200, 3000000)]
    [int]$Baud = 460800,

    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1 always includes System.IO.Ports.  Re-launch there
# when this file is started from PowerShell 7.
if ($PSVersionTable.PSEdition -ne 'Desktop') {
    $windowsPowerShell = "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe"
    if (-not (Test-Path -LiteralPath $windowsPowerShell)) {
        throw 'Windows PowerShell 5.1 was not found.'
    }
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath `
        -Port $Port -Seconds $Seconds -Baud $Baud -Output $Output
    exit $LASTEXITCODE
}

$source = @'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;

public static class X2100PointMonitor
{
    private static readonly byte[] Magic = { 0x02, 0x01, 0x04, 0x03, 0x06, 0x05, 0x08, 0x07 };
    private const int HeaderBytes = 28;
    private const int MaxPacketBytes = 1024 * 1024;

    private static UInt16 U16(byte[] data, int offset)
    {
        return (UInt16)(data[offset] | (data[offset + 1] << 8));
    }

    private static UInt32 U32(byte[] data, int offset)
    {
        return (UInt32)(data[offset] |
            (data[offset + 1] << 8) |
            (data[offset + 2] << 16) |
            (data[offset + 3] << 24));
    }

    private static bool DescribePacket(byte[] packet, int length, out UInt32 frame,
                                       out int detections, out int tracks,
                                       out string tlvSummary)
    {
        frame = 0;
        detections = 0;
        tracks = 0;
        tlvSummary = "";
        if (length < HeaderBytes)
            return false;

        frame = U32(packet, 20);
        int tlvCount = packet[24];
        int cursor = HeaderBytes;
        string[] names = new string[tlvCount];
        for (int i = 0; i < tlvCount; ++i) {
            if (cursor + 8 > length)
                return false;
            UInt32 type = U32(packet, cursor);
            UInt32 payloadBytes = U32(packet, cursor + 4);
            cursor += 8;
            if (payloadBytes > (UInt32)(length - cursor))
                return false;

            if (type == 21 && payloadBytes >= 4) {
                detections = U16(packet, cursor);
                names[i] = "det=" + detections;
            }
            else if (type == 22 && payloadBytes >= 8) {
                tracks = packet[cursor + 3];
                names[i] = "trk=" + tracks;
            }
            else if (type == 24) {
                names[i] = "warn";
            }
            else {
                names[i] = "type" + type + "=" + payloadBytes;
            }
            cursor += (int)payloadBytes;
        }
        if (cursor != length)
            return false;
        tlvSummary = String.Join(",", names);
        return true;
    }

    public static int Run(string portName, int seconds, int baud, string outputPath)
    {
        using (SerialPort port = new SerialPort(portName, baud, Parity.None, 8, StopBits.One)) {
            port.Handshake = Handshake.None;
            port.DtrEnable = true;
            port.RtsEnable = false;
            port.ReadTimeout = 1000;
            port.ReadBufferSize = 4 * 1024 * 1024;
            port.Open();

            FileStream output = null;
            if (!String.IsNullOrEmpty(outputPath)) {
                string fullPath = Path.GetFullPath(outputPath);
                string directory = Path.GetDirectoryName(fullPath);
                if (!String.IsNullOrEmpty(directory))
                    Directory.CreateDirectory(directory);
                output = new FileStream(fullPath, FileMode.Create, FileAccess.Write,
                                        FileShare.Read, 1024 * 1024,
                                        FileOptions.SequentialScan);
            }

            byte[] input = new byte[64 * 1024];
            byte[] packet = new byte[MaxPacketBytes];
            int packetPos = 0;
            int magicPos = 0;
            int expectedLength = 0;
            int valid = 0;
            int invalid = 0;
            int gaps = 0;
            UInt32 previousFrame = 0;
            bool havePrevious = false;
            Stopwatch timer = Stopwatch.StartNew();
            Stopwatch idle = Stopwatch.StartNew();

            Console.WriteLine("Opened {0} at line coding {1}. Reading TLV packets for {2} seconds...",
                              portName, baud, seconds);
            try {
                while (timer.Elapsed.TotalSeconds < seconds) {
                    int count;
                    try {
                        count = port.Read(input, 0, input.Length);
                    }
                    catch (TimeoutException) {
                        if (idle.Elapsed.TotalSeconds >= 10) {
                            Console.WriteLine("No bytes for {0:F0} s; firmware may be blocked or this is the wrong COM port.",
                                              idle.Elapsed.TotalSeconds);
                            idle.Restart();
                        }
                        continue;
                    }
                    if (count <= 0)
                        continue;
                    idle.Restart();

                    for (int i = 0; i < count; ++i) {
                        byte value = input[i];
                        if (packetPos == 0) {
                            if (value == Magic[magicPos]) {
                                ++magicPos;
                                if (magicPos == Magic.Length) {
                                    Buffer.BlockCopy(Magic, 0, packet, 0, Magic.Length);
                                    packetPos = Magic.Length;
                                    magicPos = 0;
                                }
                            }
                            else {
                                magicPos = value == Magic[0] ? 1 : 0;
                            }
                            continue;
                        }

                        packet[packetPos++] = value;
                        if (packetPos == 16) {
                            expectedLength = (int)U32(packet, 12);
                            if (expectedLength < HeaderBytes || expectedLength > MaxPacketBytes) {
                                ++invalid;
                                packetPos = 0;
                                magicPos = 0;
                                expectedLength = 0;
                            }
                        }
                        if (expectedLength != 0 && packetPos == expectedLength) {
                            UInt32 frame;
                            int detections;
                            int tracks;
                            string tlvs;
                            if (DescribePacket(packet, expectedLength, out frame,
                                               out detections, out tracks, out tlvs)) {
                                ++valid;
                                if (havePrevious && frame <= previousFrame)
                                    ++gaps;
                                else if (havePrevious && frame > previousFrame + 2)
                                    ++gaps;
                                previousFrame = frame;
                                havePrevious = true;
                                if (output != null)
                                    output.Write(packet, 0, expectedLength);
                                Console.WriteLine("Packet {0}: frame={1}, bytes={2}, {3}",
                                                  valid, frame, expectedLength, tlvs);
                            }
                            else {
                                ++invalid;
                            }
                            packetPos = 0;
                            magicPos = 0;
                            expectedLength = 0;
                        }
                    }
                }
            }
            finally {
                if (output != null) {
                    output.Flush();
                    output.Dispose();
                }
            }

            Console.WriteLine("RESULT: valid={0}, invalid={1}, discontinuities={2}, elapsed={3:F1}s",
                              valid, invalid, gaps, timer.Elapsed.TotalSeconds);
            return valid == 0 ? 2 : (invalid == 0 ? 0 : 1);
        }
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp
exit [X2100PointMonitor]::Run($Port, $Seconds, $Baud, $Output)
