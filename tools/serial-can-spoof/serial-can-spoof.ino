#include <mcp2515.h>
#include <util/crc16.h>

/* serial-can-spoof
 *  Sends a test 'can-style' serial message at a set interval
 *  The first byte (8 bit unsigned integer) increases by one every
 *  iteration, and the second decreases.
 *  
 *  Serial.write() is used to send actual bytes instead of its ASCII
 *  equivalent. This can't be easily seen in the Arduino serial
 *  monitor, so use CoolTerm, XCTU or something that can display the
 *  HEX values of received bytes.
 *  
 *  t4sr, Mar 2022
 *  
 */

uint32_t timer = millis();
uint32_t message_update = 500;

struct can_frame canMsg;

uint8_t byte_buffer[11];  // For CAN message in byte format to be transmitted (excludes crc). 11 is the max size = 2+1+dlc

void setup() {
    Serial.begin(115200);

    canMsg.can_id = 0x123;
    canMsg.can_dlc = 2;
    canMsg.data[0] = 10;
    canMsg.data[1] = 20;
}

void loop() {
    if ((millis() - timer) > message_update) {
        timer = millis();
        sendMessage(canMsg);
        canMsg.data[0]++;
        canMsg.data[1]--;
    }
}


/***** Functions from main transmit program *****/
/* (incompatible bits commented out)            */
void sendMessage(struct can_frame msg) {
    /* Sends CAN style message to XBee and logs to SD card.
     * By having in one function means we can be sure everything is robust to lack of SD card for example.
     */
    
    // Escape character delimter to spot the data
    out_byte(0x7E);
    
    // Split CAN ID into 2 bytes in order to .write()
    uint8_t can_id_b0 = (msg.can_id >> 8) & 0xFF;   // Remember, 11 bit CAN ID so 2047 is the max (0x7FF)
    uint8_t can_id_b1 = msg.can_id & 0xFF;
    
    out_byte(can_id_b0);
    out_byte(can_id_b1);
    out_byte(msg.can_dlc);

    // Add CAN ID to byte_buffer for later use in CRC calculation
    byte_buffer[0] = can_id_b0;
    byte_buffer[1] = can_id_b1;
    byte_buffer[2] = msg.can_dlc;

    // Send data to XBee plus add to byte_buffer
    for (int i = 0; i<msg.can_dlc; i++)  {
        out_byte(msg.data[i]);
        byte_buffer[i+3] = msg.data[i];
    }

    int16_t crc = gencrc(msg.can_dlc);

    out_byte((crc >> 8) & 0xFF);    // Bit shift and transmit CRC
    out_byte(crc & 0xFF);
    
    /*if (config.serialCanMsg == 1) {
        Serial.println("");
    }
    dataFile.println();*/
}

void out_byte(uint8_t b) {
    /* Send and log byte. Send to serial if set in config file */
    //XBeeSL.write(b);
    Serial.write(b);

    /*if (config.serialCanMsg == 1) {
        Serial.print(b, HEX);
        Serial.print(" ");
    }

    if (dataFile) {
        dataFile.print(b, HEX);
        dataFile.print(" ");
        if (sysStatus.data[1] == STAT_BAD) {
            updateStatus(1, STAT_GOOD);
            sendMessage(sysStatus);
        }
    }
    else if (sysStatus.data[1] == STAT_GOOD) {
        updateStatus(1, STAT_BAD);
        sendMessage(sysStatus);
    }*/
}

int gencrc(unsigned char dlc) {
    uint16_t crc = 0xFFFF, i;
    for (i = 0; i < dlc+3; i++) {
        crc = _crc16_update(crc, byte_buffer[i]);
    }
    return crc;
}
