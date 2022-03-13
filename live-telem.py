import serial
import time
import telemfunctions as tf

SERIALPORT = '/dev/ttyUSB0' # Raspberry Pi
# SERIALPORT = '/dev/cu.usbmodem14101' # macOS
BAUD = 115200   # For use with the Xbee
#BAUD = 9600     # If debugging and using SoftwareSerial on another Arduino (eg Uno) to send the data, must use a lower baud as it can't cope with high datarates

OUTPUT = 'csv'  # 'csv' or 'influx' output

max_frame = 13    # Preset out max entire 'frame' length. Will update with correct value when we get it from the DLC
new_data = False
can_bytes = bytearray()
can_len = max_frame
index = 0
recv_in_progress = False
start_marker = b'\x7E'

# To make tf.parse_msg() happy
HH = 0
mm = 0
ss = 0
last_GPS_time = f'{HH:d}:{mm:02d}:{ss:02d}'

# If logging to csv for some reason, use date and time for folder name
timestr = time.strftime("%Y%m%d-%H%M%S")    # eg. 20220124-172945

ser = serial.Serial(SERIALPORT, BAUD)    # If it's not working, check this

while True:
    if ser.in_waiting:
        this_byte = ser.read()  # Read all bytes in
        #print(this_byte)

        if recv_in_progress == True:
            can_bytes.append(ord(this_byte))    # Store this_byte in can_bytes. Silly conversion to and from byte/int

            index = index + 1

            if index == 3:
                # If we're at the 3rd position, this is the DLC
                # Update can_len with this info, but as an integer
                data_length = ord(this_byte)
                # Remember 2 bytes for ID, 1 for DLC, 2 for CRC:
                can_len = data_length + 2 + 1 + 2

            if index >= can_len:
                # We've reached the end of our 'frame'
                recv_in_progress = False
                the_bytes = can_bytes
                new_data = True

                # Clear stuff ready for the next message
                index = 0
                can_bytes = bytearray() # Empty byte array

                # Check CRC
                crc_ok = tf.check_crc(the_bytes)
                #print('CRC check: %s' %crc_ok)
                if not crc_ok:
                    id_int = int.from_bytes(the_bytes[0:2], "big")
                    id_hex = "0x%0.2X" % id_int
                    print('CRC Mismatch on %s' %id_hex)
                    #new_data = False    # Don't process

        elif this_byte == start_marker:
            # When we spot the start_marker, flip the flag
            recv_in_progress = True


    # Keep cycling through the above until we get new_data
    if new_data:
        #time = datetime.utcnow()
        new_data = False            # Data only stops being new when it's used
        local_time = time.time()    # There's some work to be done to align with GPS time
        msg = tf.bytes2canmsg(the_bytes, local_time, "Live")
        print(msg)
        last_GPS_time = tf.parse_msg(msg, last_GPS_time, output_type=OUTPUT, logfolder=timestr)#, time_type='gps')