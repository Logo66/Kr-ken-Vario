import serial
import time

s = serial.Serial("COM5", 115200, timeout=3, dsrdtr=True, rtscts=False)
s.dtr = True
s.rts = False
time.sleep(1)

print("Port open. Reading for 12 seconds...")
for i in range(6):
    w = s.in_waiting
    if w > 0:
        data = s.read(w)
        text = data.decode("utf-8", errors="replace")
        print("Round %d (%d bytes): %s" % (i, w, text))
    else:
        print("Round %d: 0 bytes" % i)
    time.sleep(2)

s.close()
print("Done")
