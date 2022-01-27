import csv
import time
import telemfunctions as tf

dir = "logs/"
filename = "sample.TXT"
recv_in_progress = False
foldername = filename.split(".")[0] # Remove .txt for a folder name for this log file
#foldername = 'output_folder' # For testing

HH = 0
mm = 0
ss = 0
last_GPS_time = f'{HH:d}:{mm:02d}:{ss:02d}'

print("Process started")
start = time.time()

# Open and read log file
with open(dir + filename, newline='') as csv_file:
    csv_reader = csv.reader(csv_file, delimiter=" ")    # "Bytes" are separated by spaces
    systime = time.time() # Just to start with... need to pull from the received GPS data
    n_rows = 0
    
    for row in csv_reader:
        # Need to check so we don't ask for row[0] on and empty row
        if len(row) != 0:

            for this_byte_str in row:   # Cycle through each "byte" in each row  
                if recv_in_progress:
                
                    # Crude check to see if we've got something hex-like
                    if this_byte_str.isalnum() is not True:
                        print("non-alphanumeric character reached")
                        recv_in_progress = False
                        break
                    """This check could be better to ensure conversion to an int doesn't fail"""
                    
                    # Convert this_byte to its integer value
                    this_byte_val = int(this_byte_str, 16)

                    # Append to byte array
                    can_bytes.append(this_byte_val)

                    index = index + 1

                    if index == 3:
                        # DLC is at position 3. Update length info
                        data_length = this_byte_val
                        # 2 bytes for ID, 1 for DLC, 2 for CRC plus data_length
                        can_len = data_length + 2 + 1 + 2

                    if index >= can_len:
                        # We've reached the end of our 'frame'
                        recv_in_progress = False
                        #last_can_len = index
                        index = 0
                        new_data = True

                        # Check CRC
                        '''if tf.check_crc(can_bytes) is True:
                            print('crcok')
                        else:
                            print('CRC mismatch')'''

                        # Per row actions
                        msg = tf.bytes2canmsg(can_bytes, systime, "Logs")
                        #msg = tf.bytes2canmsg(can_bytes, last_GPS_time, "Logs")
                        #print(msg)
                        last_GPS_time = tf.parse_msg(msg, last_GPS_time, output_type='csv', logfolder=foldername) #, time_type='gps')
                        break

                elif row[0] == "7E":
                    index = 0
                    can_bytes = bytearray()
                    recv_in_progress = True
                    can_len = 13 # Max frame length

        n_rows += 1

        if n_rows%1000 == 0:
            print("\t%i rows complete" %n_rows)

elapsed = time.time() - start
print("Total %i rows\n" %n_rows)
print("Processed completed in %.2f seconds\n" %elapsed)