import serial, time, sys
# Try to find ESP32 ROM bootloader output (after ESP32 reset, no app)
# ESP32 ROM outputs "waiting for download" on reset if GPIO0 low
# Otherwise just runs app

# Reset to bootloader mode: GPIO0 (DTR?) low
ser = serial.Serial("COM9", 115200, timeout=0.3)
# Pulse-reset using DTR/RTS
ser.dtr = False
ser.rts = False
time.sleep(0.1)
ser.rts = True   # reset released
ser.dtr = False
time.sleep(0.1)
ser.close()

# Now try opening with rtscts=False and explicitly read
time.sleep(0.5)
ser = serial.Serial("COM9", 115200, timeout=0.5, rtscts=False, xonxoff=False)
ser.reset_input_buffer()
start = time.time()
buf = b""
while time.time() - start < 8:
    n = ser.in_waiting
    if n:
        buf += ser.read(n)
    time.sleep(0.05)
ser.close()
print(f"Got {len(buf)} bytes")
sys.stdout.buffer.write(buf[:3000])
