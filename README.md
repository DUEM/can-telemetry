# CAN Telemetry
Acquisition of telemetry data from a remote CAN network.

## [Transmit](transmit/)
* Sniffs CAN messages and sends over UART
* Adds GPS and system status data as a CAN message
* Logs sniffed and generated (undecoded) CAN messages to SD card, with bytes represented in ASCII as hexadecimal

## Receive
This is included in this directory to make it easier to run in vscode.

* Decodes CAN messages as defined by a `json` CAN lookup file
* Stores as `csv` files or publishes to an InfluxDB server
* Messages can either be processed from logged SD files or from a serial stream

# Receiver Telemetry
Deals with the live reception of telemetry and the processing of logs of the following style:

| Contents  | Size |
| --------- | ---- |
| `0x7E`    | 1 byte |
| *CAN ID*  | 2 bytes (max 11-bit value) |
| *DLC*     | 1 byte |
| *DATA*    | *DLC* bytes |
| *CRC*     | 2 bytes |

## Files
To receive live telemetry, use [live-telem.py](live-telem.py). Receives raw bytes from a serial port.

To process logged telemetry data, use [log-to-data.py](log-to-data.py). Receives ASCII characters in a hexadecimal form. Eg. `1A` represents `0x1A` (hex) = `26` (int).

Both programs rely on functions stored in [telem-functions.py](telem-functions.py).

## Usage

### Processing multiple log files
[log-to-data.py](log-to-data.py) will append to an output file in the same location. However, by default the folder the output is written to corresponds to the name of the log file. To append to a single file uncomment `foldername = output_folder` and choose a foldername.

### Time
Currently, time is recorded according to the `output_type`:
* Recording to `csv` will use `last_gps_time`
* Recording to `influx` will the time of receipt/parsing (ie Raspberry Pi system time). 

It is anticipated that `influx` will be used for live car runs and `csv` for converting SD logs. Note that if converting SD logs to `influx`, the timestamps will be incorrect in that they will correspond to the time at which the program is run.

In any case, the system time is stored in the CAN message timestamp.


## Notes/Future Work
1. At the moment `parse_msg` decodes the CAN message *and* stores it as and when a field is found. Perhaps would be better to return a json list or something of all the fields and values in the message to then be stored later in the code. (eg the Influx `body` json)
1. The arguments for `parse_msg` and `store_result` could do with tidying up, although this could be dealt with by sorting the above point.
1. Ensure cross compatibility between `last_gps_time` and the Raspberry Pi system time so either can be stored in Influx of CSV.
1. Would be nice to record each time `check_crc` fails to Influx or CSV.
