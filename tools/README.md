# Tools
For testing functionality of specific systems/features.

### [CAN Message Generator](can-gen-ldr-dht/can-gen-ldr-dht.ino)
* Quick test device for almost entire dataflow (CAN bus&#8594;transmit&#8594;receive&#8594;database).
* Measures temperature, humidity and LDR brightness. Transmits this over CAN along with heat index.

### [Serial CAN Spoofer](serial-can-spoof/serial-can-spoof.ino)
* Sends a test 'can-style' serial message at a set interval.
* The first byte (8 bit unsigned integer) increases by one every iteration, and the second decreases.
*  Serial.write() is used to send actual bytes instead of its ASCII equivalent. This can't be easily seen in the Arduino serial monitor, so use [CoolTerm](http://freeware.the-meiers.org), XCTU or something that can display the HEX values of received bytes.

### [Nested Bit Tester](serial-can-nested-bits/serial-can-nested-bits.ino)
* Some messages include 'nested bits'.
* This was used to verify the operation of the nested bits receiver unpacking functions.
* Sends 8 byte message with first nibble of data incrementing each loop to the `0x987` test address.