if __name__ == '__main__':  # Warn if trying to run this as a script
    print("\n*********************************************")
    print("      This is where the functions live")
    print(" Run log-to-data.py or live-telem.py instead")
    print("*********************************************\n")
    quit()

import datetime
import json
import struct
from can import Message
from pathlib import Path
from influxdb import InfluxDBClient
from crccheck.crc import Crc16Modbus

"""
Options
"""

useMultiplier = True    # Multipliers may be specified in the CAN configuration file. FALSE = disabled.

CANFILE = 'config/recv_conf.json'

# influx configuration - edit these
ifuser = "<username>"
ifpass = "<yourpassword>"
ifdb   = "home"
ifhost = "127.0.0.1"
ifport = 8086

ifclient = InfluxDBClient(ifhost,ifport,ifuser,ifpass,ifdb)

"""
FUNCTION DEFINITIONS
"""

def check_crc(can_bytes):
    # Calculate CRC for the "byte_buffer" of the Arduino code:
    # > can_id_0
    # > can_id_1
    # > msg.can_dlc
    # > msg.data[i]
    # > ...
    # > msg.data[msg.can_dlc + 3]

    data = can_bytes[0:-2]      # All except the last two
    crc_rcvd = can_bytes[-2:]   # Received CRC code
    crc_rcvd = int.from_bytes(crc_rcvd, "big")  # Convert to int

    crc_calc = Crc16Modbus.calc(data)
    
    if crc_calc == crc_rcvd:
        return True
    else:
        return False

def bytes2canmsg(can_bytes, can_timestamp, msg_chan):
    can_id = int.from_bytes(can_bytes[0:2], "big") 
    can_dlc = can_bytes[2]              # Returns integer value
    can_data = can_bytes[3:3+can_dlc]   # Returns bytearray
    message = Message(is_extended_id=False, arbitration_id=can_id, data=can_data, timestamp=can_timestamp)
    message.channel = msg_chan
    return message

def parse_msg(message, last_GPS_time, output_type, logfolder): #, time_types
    with open(CANFILE) as json_file: # There is already a global version so just use that
        lookup = json.load(json_file)
        # Convert ID to a string in the form "0xABC1"
        can_id = "0x%0.2X" % message.arbitration_id
        if can_id in lookup:
            #print("ID %s found" %can_id)
            # Maybe compare apparent size to DLC and error if disagree. Might have to do before otherwise too late
            structure = lookup[can_id][0]["structure"] # The structure of a message with this id. returns type list
            offset = 0
            ndx = 0     # Index so we know where we are. Removes issue of multiple identical entries.

            # Go through all the data in the message
            for dataType in structure:

                # Find variable name from lookup and append array in appropriate dictionary
                var_name = lookup[can_id][0]["fields"][ndx]
                var_source = lookup[can_id][0]["source"]

                # First deal with any nested data. Removes need to go back on offset later.
                if var_name.split("_")[-1] == 'Y':  # Looking for _Y in "fields"
                    if "nested_data" in lookup[can_id][0]:
                        nested_data = lookup[can_id][0]["nested_data"]
                        if var_name in nested_data:
                            #print("Nested data found: %s found" %var_name)
                            nd_ndx = 0
                            
                            nd_structure = lookup[can_id][0]["nested_data"][var_name]["nd_structure"]

                            for nd_type in nd_structure:

                                n = nd_ndx//8   # Local byte offset

                                if nd_type[0] == 'X':           # Don't care flag
                                    nd_ndx += int(nd_type[1])   # Skip specified num of bits
                                
                                elif nd_type == 'b1':
                                    nd_byte = message.data[offset + n]  # Dealt with on a BYTEWISE basis
                                    pos = 7 - nd_ndx    # Start from MSB
                                    nd_bit = (nd_byte & 2**pos) >> pos # AND byte with position and recover 0/1

                                    nd_name = lookup[can_id][0]["nested_data"][var_name]["nd_fields"][nd_ndx]

                                    store_result(last_GPS_time, nd_bit, nd_name, var_source, output_type, logfolder)

                                    nd_ndx += 1
                                
                                else:
                                    print("Invalid nd_type in nd_structure for %s" %can_id)
                    else:
                        print("Warning: field name was flagged with _Y")
                        print("         nested data not found for %s" %can_id)

                # Main data:
                # Send to the right function for the right data
                types = {
                    "float32": up_float,
                    "float32LE": up_floatLE,
                    "int32": up_int32,
                    "uint32": up_uint32,
                    "int16": up_int16,
                    "uint16": up_uint16,
                    "int8": up_int8,
                    "uint8": up_uint8,
                    "char": up_char,
                    "uint16ten": up_uint16ten,
                }
                dataTypeClean = dataType.split("_")[0]  # Remove anything after the _
                value, offset = types[dataTypeClean](message.data, offset)

                """
                STRUCT UNPACK DOES ALL OF THIS^ FOR YOU :(
                    a = struct.unpack(">hHHBB", can_data)
                    pCurr, PInstV, PSumV, SoC, rState = a
                COULD JUST HAVE ">hHHBB" IN CONFIG FILE.
                ALTHOUGH CAN'T USE CUSTOM FUNCS AS EASILY
                """

                # Apply the multiplier here if we want to
                if "multiplier" in lookup[can_id][0] and useMultiplier is True:
                    factor = lookup[can_id][0]["multiplier"][ndx]
                    if isinstance(factor, str):
                        print("Warning: multiplier for %s is a string" %var_name)
                        factor = float(factor)
                    value = value * factor

                ndx += 1

                # When a date or time message arrives, update the timestamp we're using
                if can_id == "0xF6":
                    ## String time
                    TimeList =last_GPS_time.split(':')
                    HH = int(TimeList[0])
                    mm = int(TimeList[1])
                    ss = int(TimeList[2])

                    if var_name == "hour":
                        HH = value
                    elif var_name == "minute":
                        mm = value
                    elif var_name == "second":
                        ss = value
                    
                    last_GPS_time = f'{HH:d}:{mm:02d}:{ss:02d}'

                if can_id == "0xF8":    # Latitude can message
                    if var_name == "latitude":
                        latitude = value
                    elif var_name == "lat":
                        lat = value
                        # "lat" is the 2nd to arrive, so convert and store
                        latitude_deg = dddmm2deg(latitude, lat)
                        store_result(last_GPS_time, latitude_deg, "latitude_deg", var_source, output_type, logfolder)

                if can_id == "0xF9":    # Longitude can message
                    if var_name == "longitude":
                        longitude = value
                    elif var_name == "lon":
                        lon = value
                        # "lat" is the 2nd to arrive, so convert and store
                        longitude_deg = dddmm2deg(longitude, lon)
                        store_result(last_GPS_time, longitude_deg, "longitude_deg", var_source, output_type, logfolder)


                # Need to think about how to use other time too for live logging. maybe from CAN message timestamp
                store_result(last_GPS_time, value, var_name, var_source, output_type, logfolder)#, time_type)

        else:
            print("ID %s not found" %can_id)

        return last_GPS_time

def store_result(T, value, var_name, var_source, output_type, logfolder):

    if output_type == 'csv':
        # Save to csv
        #file_object = open('data/' + var_source + '/' + var_name + '.txt', 'a')
        directory = 'data/' + logfolder + '/' + var_source
        Path(directory).mkdir(parents=True, exist_ok=True) # parents=True : will create parent dirs if not present
        file_object = open(directory + '/' + var_name + '.csv', 'a')
        file_object.write(T + ',' + str(value) + '\n')
        file_object.close()

    elif output_type == 'influx':
        # Save to database
        time = datetime.datetime.utcnow()   # This is acceptable for live logging - doesn't work for logs
        #print(time)

        body = [
            {
                "measurement": var_source,
                "time": time,

                "fields": {
                    var_name: value
                }
            }
        ]

        influx_success = ifclient.write_points(body)    # Hopefully means program continues if unsuccessful
        if influx_success is False:
            print("Error writing to Influx for %s/%s" %var_source %var_name)

        #print(body)

    #elif output_type == 'both':

    else:
        print('INVALID output_type')



"""
Unpack bytes from CAN messages into appropriate data types
List of arguments for datatypes available at:
    https://docs.python.org/3/library/struct.html#format-characters
"""
def up_float(data, offset):
    [x] = struct.unpack('>f', data[offset:offset+4])
    # Could use: struct.unpack_from(format, /, buffer, offset=offset)
    offset += 4
    return x, offset

def up_floatLE(data, offset):  # unpack the bytes from the other side
    [x] = struct.unpack('<f', data[offset:offset + 4])
    # Could use: struct.unpack_from(format, /, buffer, offset=offset)
    offset += 4
    return x, offset

def up_int32(data, offset):
    [x] = struct.unpack('>i', data[offset:offset+4])
    offset += 4
    return x, offset

def up_uint32(data, offset):
    [x] = struct.unpack('>I', data[offset:offset+4])
    offset += 4
    return x, offset

def up_int16(data, offset):
    [x] = struct.unpack('>h', data[offset:offset+2])
    offset += 2
    return x, offset

def up_uint16(data, offset):
    [x] = struct.unpack('>H', data[offset:offset+2])
    offset += 2
    return x, offset

def up_int8(data, offset):
    [x] = struct.unpack('>b', data[offset:offset+1])
    offset += 1
    return x, offset

def up_uint8(data, offset):
    [x] = struct.unpack('>B', data[offset:offset+1])
    offset += 1
    return x, offset

def up_char(data, offset):
    [x] = struct.unpack('>c', data[offset:offset+1])   # Doesn't really do particularly much I don't think...
    offset += 1
    return x, offset

# Custom functions to remove nested data
def up_uint16ten(data, offset):
    # Silly conversion to int and back so we can AND:
    DATA = int.from_bytes(data[offset:offset+2], byteorder='big', signed=False)
    MASKED = DATA & 0b0000001111111111
    masked = MASKED.to_bytes(2, 'big')
    [x] = struct.unpack('>H', masked)
    offset += 2
    #print("{0:b}".format(x))
    return x, offset

"""
End of unpacking functions
"""

# Converts DDDMM.MMMM GPS coordinates to decimal degrees
def dddmm2deg(value, direction):
    DD = value//100
    MM = value - 100*DD
    degrees = DD + MM/60
    if direction == "W" or direction == "S":
        degrees = -degrees
    return degrees