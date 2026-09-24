from pathlib import Path
import hashlib
import struct

project = Path("/home/melody/Manhattan_Project/freertos")
frame = (project / "vendor/testdata/adc_frame_0001.dat").read_bytes()
firmware = (project / "rtos-with-spl.bin").read_bytes()

print("HEADER_HEX=" + frame[:36].hex(" "))
fields = [struct.unpack_from("<I", frame, offset)[0]
          for offset in (12, 20, 28, 32)]
print("LE32_OFF12=%d" % fields[0])
print("LE32_OFF20=%d" % fields[1])
print("TLV_TYPE=%d" % fields[2])
print("TLV_LEN=%d" % fields[3])
print("EMBEDDED_FRAME_COUNT=%d" % firmware.count(frame))
print("OUTPUT_SHA256=" + hashlib.sha256(firmware).hexdigest())
