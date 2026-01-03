import serial, time

PORT = "/dev/serial/by-id/usb-Raspberry_Pi_Pico_E6611C08CB754E22-if00"  # adjust as needed
BAUD = 2000000   # ignored for CDC, but still set it

ser = serial.Serial(PORT, BAUD)

total = 0
start = time.time()

while True:
    data = ser.read(128)   # READ BIG CHUNKS
    total += len(data)
    now = time.time()
    if now - start >= 1.0:
        print(f"{total / 1024:.1f} KB/s")
        total = 0
        start = now

