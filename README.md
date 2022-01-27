# CAN Telemetry
## [Transmit](transmit/TRANSMIT.md)
* Sniffs CAN messages and sends over UART
* Adds GPS and system status data as a CAN message
* Logs sniffed and generated (undecoded) CAN messages to SD card, with bytes represented in ASCII as hexadecimal

## [Receive](receive/)
* Decodes CAN messages as defined by a `json` CAN lookup file
* Stores as `csv` files or publishes to an InfluxDB server
* Messages can either be processed from logged SD files or from a serial stream
