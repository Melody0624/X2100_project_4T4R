param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [string]$Output = (Join-Path (Get-Location) ("Record_{0}_adc.dat" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))),

    [ValidateRange(1, 10000)]
    [int]$Frames = 100,

    [ValidateRange(1, 600)]
    [int]$OpenTimeoutSeconds = 60,

    [ValidateRange(1, 600)]
    [int]$IdleTimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'

# Use Windows PowerShell 5.1 because its .NET Framework runtime includes
# System.IO.Ports. PowerShell 7 installations may not include that assembly.
if ($PSVersionTable.PSEdition -ne 'Desktop') {
    $windowsPowerShell = "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe"
    if (-not (Test-Path -LiteralPath $windowsPowerShell)) {
        throw 'Windows PowerShell 5.1 was not found.'
    }
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath `
        -Port $Port -Output $Output -Frames $Frames `
        -OpenTimeoutSeconds $OpenTimeoutSeconds -IdleTimeoutSeconds $IdleTimeoutSeconds
    exit $LASTEXITCODE
}

$source = @'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Threading;

public static class X2100AdcCapture
{
    private static readonly byte[] Magic = { 0x02, 0x01, 0x04, 0x03, 0x06, 0x05, 0x08, 0x07 };
    private const int HeaderBytes = 28;
    private const int PrefixBytes = 36;
    private const int ExpectedPayloadBytes = 522240;
    private const int ExpectedPacketBytes = PrefixBytes + ExpectedPayloadBytes;

    private static UInt32 U32(byte[] data, int offset)
    {
        return (UInt32)(data[offset] |
            (data[offset + 1] << 8) |
            (data[offset + 2] << 16) |
            (data[offset + 3] << 24));
    }

    private static SerialPort WaitForPort(string portName, int timeoutSeconds)
    {
        DateTime deadline = DateTime.UtcNow.AddSeconds(timeoutSeconds);
        while (DateTime.UtcNow < deadline) {
            SerialPort candidate = null;
            try {
                candidate = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One);
                candidate.Handshake = Handshake.None;
                candidate.DtrEnable = true;
                candidate.RtsEnable = false;
                candidate.ReadTimeout = 1000;
                // Keep enough kernel-side buffering for more than 30 raw frames.
                // This prevents synchronous disk activity from starving usbser.sys.
                candidate.ReadBufferSize = 16 * 1024 * 1024;
                candidate.Open();
                return candidate;
            }
            catch {
                if (candidate != null)
                    candidate.Dispose();
                Thread.Sleep(250);
            }
        }
        return null;
    }

    public static int Run(string portName, string outputPath, int targetFrames,
                          int openTimeoutSeconds, int idleTimeoutSeconds)
    {
        SerialPort port = null;
        Console.WriteLine("Waiting for {0}. Power-cycle the board in normal boot mode now.", portName);
        port = WaitForPort(portName, openTimeoutSeconds);
        if (port == null || !port.IsOpen)
            throw new IOException("Unable to open " + portName +
                ". Check the USB CDC COM number and close MotorCycle Tools/MobaXterm on that COM port.");

        string fullOutput = Path.GetFullPath(outputPath);
        string directory = Path.GetDirectoryName(fullOutput);
        if (!String.IsNullOrEmpty(directory))
            Directory.CreateDirectory(directory);

        byte[] input = new byte[1024 * 1024];
        byte[] packet = new byte[ExpectedPacketBytes];
        int packetPos = 0;
        int magicPos = 0;
        int expectedPacketLength = 0;
        int frames = 0;
        UInt32 lastFrameNumber = 0;
        long bytesWithoutAdcMagic = 0;
        DateTime lastData = DateTime.UtcNow;

        Console.WriteLine("Opened {0} as USB CDC (115200 line coding). Reading binary ADC packets...", portName);
        try {
            using (FileStream output = new FileStream(fullOutput, FileMode.Create, FileAccess.Write,
                                                       FileShare.Read, 1024 * 1024,
                                                       FileOptions.SequentialScan)) {
                while (frames < targetFrames) {
                    int count;
                    try {
                        count = port.Read(input, 0, input.Length);
                    }
                    catch (TimeoutException) {
                        double idleSeconds = (DateTime.UtcNow - lastData).TotalSeconds;
                        if (lastFrameNumber >= (UInt32)targetFrames && idleSeconds >= 3)
                            throw new IOException(String.Format(
                                "The firmware finished at frame {0}, but only {1}/{2} complete packets reached the PC. " +
                                "Power-cycle and repeat the capture; the partial DAT remains structurally valid.",
                                lastFrameNumber, frames, targetFrames));
                        if (idleSeconds >= idleTimeoutSeconds)
                            throw new IOException("No ADC bytes received for " + idleTimeoutSeconds +
                                " seconds. Leave this script running and power-cycle the board.");
                        continue;
                    }
                    catch (IOException) {
                        if (frames != 0)
                            throw;
                        try { port.Close(); } catch { }
                        port.Dispose();
                        port = null;
                        packetPos = 0;
                        magicPos = 0;
                        expectedPacketLength = 0;
                        bytesWithoutAdcMagic = 0;
                        Console.WriteLine("COM disconnected during reset; waiting for {0} to reappear...", portName);
                        port = WaitForPort(portName, openTimeoutSeconds);
                        if (port == null)
                            throw new IOException("USB CDC did not reappear as " + portName + ".");
                        Console.WriteLine("Reopened {0}; waiting for ADC packets...", portName);
                        lastData = DateTime.UtcNow;
                        continue;
                    }
                    if (count <= 0)
                        continue;
                    lastData = DateTime.UtcNow;
                    if (frames == 0 && packetPos == 0)
                        bytesWithoutAdcMagic += count;

                    for (int i = 0; i < count && frames < targetFrames; ++i) {
                        byte value = input[i];
                        if (packetPos == 0) {
                            if (value == Magic[magicPos]) {
                                ++magicPos;
                                if (magicPos == Magic.Length) {
                                    Buffer.BlockCopy(Magic, 0, packet, 0, Magic.Length);
                                    packetPos = Magic.Length;
                                    magicPos = 0;
                                    bytesWithoutAdcMagic = 0;
                                }
                            }
                            else {
                                magicPos = value == Magic[0] ? 1 : 0;
                            }
                            continue;
                        }

                        packet[packetPos++] = value;
                        if (packetPos == 16) {
                            expectedPacketLength = (int)U32(packet, 12);
                            if (expectedPacketLength != ExpectedPacketBytes) {
                                packetPos = 0;
                                magicPos = 0;
                                expectedPacketLength = 0;
                            }
                        }
                        if (packetPos == PrefixBytes) {
                            UInt32 tlvType = U32(packet, HeaderBytes);
                            UInt32 tlvLength = U32(packet, HeaderBytes + 4);
                            if (tlvType != 13 || tlvLength != ExpectedPayloadBytes) {
                                packetPos = 0;
                                magicPos = 0;
                                expectedPacketLength = 0;
                            }
                        }
                        if (expectedPacketLength != 0 && packetPos == expectedPacketLength) {
                            UInt32 frameNumber = U32(packet, 20);
                            output.Write(packet, 0, expectedPacketLength);
                            ++frames;
                            lastFrameNumber = frameNumber;
                            Console.WriteLine("Captured {0}/{1}: frame={2}, bytes={3}",
                                              frames, targetFrames, frameNumber,
                                              expectedPacketLength);
                            packetPos = 0;
                            magicPos = 0;
                            expectedPacketLength = 0;
                        }
                    }
                    if (frames == 0 && packetPos == 0 && bytesWithoutAdcMagic >= 4096)
                        throw new InvalidDataException(
                            "This COM port is producing data but no ADC TLV header was found. " +
                            "It is probably the UART2 debug port. Select the USB CDC port " +
                            "with VID 0525 / PID A4A7 instead.");
                }
            }
        }
        finally {
            if (port != null) {
                try { port.Close(); } catch { }
                port.Dispose();
            }
        }

        long expectedFileBytes = (long)targetFrames * ExpectedPacketBytes;
        long actualFileBytes = new FileInfo(fullOutput).Length;
        if (actualFileBytes != expectedFileBytes)
            throw new InvalidDataException(String.Format(
                "Output size mismatch: expected {0}, got {1}", expectedFileBytes, actualFileBytes));
        Console.WriteLine("COMPLETE: {0} frames, {1} bytes", frames, actualFileBytes);
        Console.WriteLine("Saved: {0}", fullOutput);
        return 0;
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp
$resolvedOutput = [System.IO.Path]::GetFullPath($Output)
exit [X2100AdcCapture]::Run($Port, $resolvedOutput, $Frames,
    $OpenTimeoutSeconds, $IdleTimeoutSeconds)
