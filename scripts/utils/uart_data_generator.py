import serial
import struct
import time
import threading

ser = serial.Serial("COM4", 115200, timeout=0.1)

def sender():
    seq = 0
    while True:
        ser.write(struct.pack("<I", seq))
        seq += 1
        time.sleep(0.01)

def receiver():
    while True:
        data = ser.read(4)
        if data:
            print("echo:", struct.unpack("<I", data)[0])

threading.Thread(target=sender, daemon=True).start()
threading.Thread(target=receiver, daemon=True).start()

input("Press Enter to stop...\n")