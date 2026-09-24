# MT-4T4R USB sender v2 test

This build keeps the fast boot changes and uses one 512-byte USB chunk per call
in an independent sender task. Host output targets every processed frame,
about 19.87 Hz. This sender is not stable on the observed Windows setup:
the device continued processing past frame 1300 while `[HOST] sent` stayed at
18 and `dropped` grew. The original sender waits indefinitely for a USB IN
request, so `errors=0` does not rule out a blocked sender.

## 1. Flash

Use USBCloner with:

`live_full_experimental/MT-4T4R_live_full_experimental_SFC_NOR_PRESERVE_CONFIG.cfg`

The CFG preserves the calibration/configuration area.

## 2. Check the UART2 diagnostic port

Open UART2 at 115200 baud.  After MotorCycle Tools opens the USB CDC COM port,
the periodic `[HOST]` line should show `sent` increasing. If `sent` stays flat
while `produced` and `dropped` rise, the sender is stalled even when
`errors=0`.

`dropped` can increase when the PC is slower than the approximately 19.87 Hz
display output. The sender intentionally retains the newest complete frame;
this does not stop ADC processing or tracking.

## 3. Isolate firmware transport from MotorCycle Tools

Close MotorCycle Tools so it releases the USB CDC COM port, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "D:\downloads\X2100_project-main\tools\monitor_x2100_points.ps1" `
  -Port COM5 -Seconds 120 -Baud 460800 `
  -Output "D:\downloads\X2100_project-main\Record_4T4R_points.dat"
```

Replace `COM5` with the USB CDC COM number.  Do not use the UART2 diagnostic
COM port.  A healthy run prints continuously increasing packet and frame
numbers for the full 120 seconds and ends with `invalid=0`.

- If this monitor runs continuously but MotorCycle Tools stops, the remaining
  fault is in the PC application's serial read/display path.
- If both stop and the UART `[HOST] sent=` counter also stops, capture the last
  five `[HOST]` lines. This supports a device/USB transport stall, although it
  does not by itself distinguish the USB controller from the PC host driver.
