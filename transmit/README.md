# Transmission Telemetry
Deals with:
* CAN Bus sniffing
* GPS data
* Local data logging
* Transmission of data to receiver implementation

## Requirements
### Hardware
#### Main inputs/outputs

| Part    | Pins    | C definition |
| ------- | ------- | ------------ |
| MCP2515 | SPI, SS&rarr;10 | `MCP_SS` |
| &mu;SD Module | SPI, SS&rarr;12  | `SD_SD` |
| Xbee    | Serial2 | `XBeeSL`     |
| GPS     | Serial1 | `GPSSerial`  |

#### Additional inputs
* `SLEEP_INT` on 33 (12V&rarr;5V potential divider)
* `FLAG_INT` on 21 with `INPUT_PULLUP`

### Software
The following should already be included in the Arduino IDE:
* `<SoftwareSerial.h>`
* `<SPI.h>`
* `<SD.h>`
* `<util/crc16.h>`

The following need to be installed:
* `<Adafruit_GPS.h>`
* `<ArduinoJson.h>`
* `<mcp2515.h>`

## Features

### Configuration
The following parameters are defined by the `config.txt` file on the microSD card. If reading or parsing the file is unsuccessful, default values are used.

| Variable name | Default Value | Description   |
| ------------- | ------------- | ------------- |
| `gps_update`  | 2000 ms       | Interval at which GPS data is sent and logged |
| `sd_update`   | 2000 ms       | Interval at which SD card is flushed          |
| `status_update` | 500 ms      | Interval at which system status is sent and logged |
| `time_fix`    | 10000 ms      | Time out to acquire GPS fix                   |
| `mppt_update` | 1000 ms       | MPPT poll update rate                         |

The `spare` field is also included but not used.

The intention of this system is to allow parameters to be changed or features enabled/disabled without reprogramming the microcontroller. 

> **NB:** When making a change to `config.txt`, it is important to ensure the json buffer is sufficient. This can be calculated using [this tool](https://arduinojson.org/v6/assistant/) and is specified in the `load_config()` function with `DynamicJsonDocument doc(140)`.


### System Status Message
The system periodically sends and logs a CAN message (id `0x111`) containing information about the state of the system. The system status is stored in an 8 byte character array `sysStatus.data[]`. As of 2021-11-03, the structure is the following:


| Position      | Status Name   | Variable Name | Significance of Value                 |
| ------------- | ------------- | ------------- | ------------------------------------- |
| 0             | Power         | `stat_pwr`    | On: `0x1`, Sleep: `0x0`, Setup: `0xFF`|
| 1             | SD            | `stat_sd`     | Failed: `0xAA`, Success: `0xFF`       |
| 2             | GPS           | `stat_gps`    | Time acquired: `0xFF`                 |
| 3             | Configuration | `stat_conf`   | Defaults: `0xBB`, Read from SD: `0xFF`, `100+i`: log number, Log failed: `90`|
| 4             | Flag          | `stat_flag`   | Value assigned                        |
| 5             | *spare*       |               |                                       |
| 6             | *spare*       |               |                                       |
| 7             | *spare*       |               |                                       |

The data is sent and logged as a CAN message on address `0x111` at an interval of `config.status_update`. The status message is also sent on some rising or falling edges e.g. of `FLAG_INT`, `SLEEP_INT` and `dataFile`.

### CAN Style Messages
Data is send and stored as CAN style messages. No decoding is performed onboard; this is the job of the receiver. The `sendMessage()` function deals with both transmission and logging. In this way, with the exception of the line return in the log file to aid readability, the data in the log file is equivalent to what has been transmitted.

### Cyclic Redundancy Check
A CRC is calculated to allow detection of errors which may have occurred in transmission.