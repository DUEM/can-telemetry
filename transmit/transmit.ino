#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GPS.h>
#include <ArduinoJson.h>
#include <mcp2515.h>
#include <util/crc16.h>

#define MCP_SS 10
#define SD_SS 12
#define SLEEP_INT 33
#define FLAG_INT 22
#define GPSSerial Serial1
#define XBeeSL Serial2

#define GPSECHO  false
#define DEBUG

#define STAT_GOOD   0xFF
#define STAT_BAD    0xAA
#define STAT_DEFAULTS   0xBB
#define STAT_UNKNOWN    0x00

#ifdef DEBUG
 #define DEBUG_PRINT(x)  Serial.print (x)
 #define DEBUG_PRINTLN(x)  Serial.println (x)
 #define DEBUG_WRITE(x)  Serial.write (x)
 #define DEBUG_PRINTHEX(x) Serial.print (x, HEX)
#else
 #define DEBUG_PRINT(x)
 #define DEBUG_PRINTLN(x)
 #define DEBUG_WRITE(x)
 #define DEBUG_PRINTHEX(x)
#endif



/* USEFUL DOCUMENTATION *//*
    > Toggle debug: https://forum.arduino.cc/index.php?topic=46900.0
    > SPI SS usage: http://www.learningaboutelectronics.com/Articles/Multiple-SPI-devices-to-an-arduino-microcontroller.php
    > MCP2515 setup: https://circuitdigest.com/microcontroller-projects/arduino-can-tutorial-interfacing-mcp2515-can-bus-module-with-arduino
    > String/char pointers?: https://www.arduino.cc/reference/en/language/variables/data-types/string/
    > Semi-functional SD logging: https://forum.arduino.cc/index.php?topic=228346.15
    > AVR CRC: http://www.nongnu.org/avr-libc/user-manual/group__util__crc.html#ga37b2f691ebbd917e36e40b096f78d996
*/

// CAN transceiver using pin MCP_SS
MCP2515 mcp2515(MCP_SS);

// GPS connected to GPS_SL serial port
Adafruit_GPS GPS(&GPSSerial);

struct can_frame canMsg;    // Where the received CAN message will be stored
struct can_frame canMsgSys; // System generated CAN frames - no need for the different frames, just helps keeping track of data
struct can_frame sysStatus; // Reserved for system status messages

uint8_t my_bytes[sizeof(float)];    // For conversion of floats to bytes
uint8_t byte_buffer[11];            // For CAN message in byte format to be transmitted (excludes crc). 11 is the max size = 2+1+dlc

// File containing system settings. If this doesn't exists, make our own?
const char *conf_filename = "config.txt";
char log_filename[12] = "YYMMDD00.txt"; // Filename is max 12 characters long
File dataFile; // Where we log data

// Structure for our configuration info
struct conf {
    unsigned int gps_update;
    unsigned int sd_update;
    unsigned int status_update;
    unsigned int mppt_update;
    long time_fix;
    int serialCanMsg;
    double value0; 
    double value1;
};

conf config; //...and the info lives in config

bool car_on; // = !safestate
bool flag_status;

// Timers for updates
uint32_t timer = millis();
uint32_t sd_timer = millis();
uint32_t status_timer = millis();
uint32_t mppt_timer = millis();

void setup() {
    /* Start the serial ports */
    Serial.begin(115200);
    XBeeSL.begin(115200);
    GPS.begin(9600);
    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
    DEBUG_PRINTLN("Setting up");

    pinMode(10, OUTPUT); // Default CS pin must be set o/p even if not being used.
    pinMode(SLEEP_INT, INPUT_PULLUP);
    pinMode(FLAG_INT, INPUT_PULLUP);
    
    /* Start the CAN transceivers */
    mcp2515.reset();
    // mcp2515.setBitrate(CAN_125KBPS); //? 
    mcp2515.setBitrate(CAN_125KBPS,MCP_8MHZ);
    mcp2515.setNormalMode();

    /* System status messages sent as 0x111 */
    sysStatus.can_id = 0x111;
    sysStatus.can_dlc = 8;  // Shouldn't need to clear buffer: for (int i=0;i<8;i++)  { sysStatus.data[i] = 0x00; }
    updateStatus(0, STAT_GOOD);  // Position 0 is power. Send status that setup is happening.
    sendMessage(sysStatus);

    /* Start the SD card */
    if (!SD.begin(SD_SS)) {
        DEBUG_PRINTLN("SD card failed, or not present");
        updateStatus(1, STAT_BAD);    // SD card failed, move on and use default settings.
        sendMessage(sysStatus);
        set_defaults(config);
    }
    else if (SD.begin(SD_SS)){
        DEBUG_PRINTLN("SD card initialised");
        updateStatus(1, STAT_GOOD);    // SD card success
        sendMessage(sysStatus);

        // Load mode and pop it in config
        load_config(conf_filename);
    }

    /* Check current date and time from GPS */ // - this could do with a whole load of squishing
    DEBUG_PRINT("Checking GPS...");
    timer = millis();
    while (1) {
        char c = GPS.read();
        //if (c) DEBUG_PRINT(c);
        if (GPS.newNMEAreceived()) {
            //DEBUG_PRINT(GPS.lastNMEA());
            if (GPS.parse(GPS.lastNMEA())) {
                if (!(GPS.year == 0) && !(GPS.year == 80)) { // Before we think life is good, make sure we've actually go the date right - sometimes will read as 0 even when we have time or get it wrong and send 80
                    DEBUG_PRINTLN(">>> Time acquired <<<");
                    updateStatus(2, STAT_GOOD);    // GPS time acquired
                    sendMessage(sysStatus);
                    print_datetimefix();
                    break;
                }
                else {
                    //DEBUG_PRINTLN("Time not yet acquired:");
                    //print_datetimefix();
                }
            }
            else {
                DEBUG_PRINTLN("parsing failed");
            }
        }
        if (millis() - timer > config.time_fix) {
            DEBUG_PRINTLN("timeout for time acquisition exceeded.");
            break;
        }
    }

    // Create timestamped log file name
    log_start_up(log_filename); // Needs to happen after GPS
}

void loop() {
    powerStatus();  // Check power status
    flagStatus();   // Check flag
    readCAN();      // Read incoming CAN message and treat accordingly
    doGPS();        // Update GPS
    pollSensor();   // Poll additional sensors

    /* Flush SD file at interval defined in config file */
    if ((millis() - sd_timer) > config.sd_update) {
        sd_timer = millis();
        dataFile.flush();
        DEBUG_PRINTLN("SD Flush");
    }

    /* Send system status update at desired interval */
    if ((millis() - status_timer) > config.status_update) {
        status_timer = millis();
        sendMessage(sysStatus);
    }

    /* Send empty can to MPPT to request data */
    if (millis() - mppt_timer > config.mppt_update) {
        mppt_timer = millis();
        pollMPPT();
        DEBUG_PRINTLN("POLL MPPT");
    }
}

void powerStatus() {
    /* Check status.
     * Update and send if there has been a change.
     * Flush SD to ensure event recorded
     * Code proceeds as normal; BMS messages can still be received when safestate.
     */
    if (car_on != digitalRead(SLEEP_INT)) {
        car_on = !car_on;
        updateStatus(0, car_on);
        sendMessage(sysStatus);
        dataFile.flush();
        DEBUG_PRINT("State of car: ");
        DEBUG_PRINTLN(car_on);
    }
}

void flagStatus() {
    /* Driver can change the flag to put a marker in the data.
     * Only update and send on a change.
     */
    if (flag_status != digitalRead(FLAG_INT)) {
        flag_status = !flag_status;
        updateStatus(4, digitalRead(FLAG_INT));
        sendMessage(sysStatus);
        DEBUG_PRINT("Flag: ");
        DEBUG_PRINTLN(flag_status);
    }
}

void readCAN () {
    /* This is using the poll read method.
     * Interrupt method is available: https://github.com/autowp/arduino-mcp2515#receive-data
     * 
     * It seems the MCP2515 stores the previous CAN messages in onboard buffers until the information is read.
     */
    if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
        sendMessage(canMsg);
    }
}

void updateStatus(int pos, uint8_t val) {
    // Have as a function so we can add functionality like LEDs.
    sysStatus.data[pos] = val;
}


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
    
    if (config.serialCanMsg == 1) {
        Serial.println("");
    }
    dataFile.println();
}

void out_byte(uint8_t b) {
    /* Send and log byte. Send to serial if set in config file */
    XBeeSL.write(b);

    if (config.serialCanMsg == 1) {
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
    }
}

int gencrc(unsigned char dlc) {
    uint16_t crc = 0xFFFF, i;
    for (i = 0; i < dlc+3; i++) {
        crc = _crc16_update(crc, byte_buffer[i]);
    }
    return crc;
}

void doGPS() {
    char c = GPS.read();
    if (millis() - timer > config.gps_update) {
        timer = millis(); // reset the timer
        if (GPS.newNMEAreceived() && GPS.parse(GPS.lastNMEA())) {
            //print_datetimefix();
            //print_location();
            gps2canMsgs();  // Convert, send and log time, location etc from GPS
        }
        else {
            //DEBUG_PRINTLN("Don't Do Stuff"); 
        }
    }
}

void gps2canMsgs() {
    // Time + fix
    canMsgSys.can_id  = 0x0F6;  /// Maybe configure address from SD card?
    canMsgSys.can_dlc = 8;

    canMsgSys.data[0] = GPS.hour;
    canMsgSys.data[1] = GPS.minute;
    canMsgSys.data[2] = GPS.seconds;
    canMsgSys.data[3] = GPS.year;
    canMsgSys.data[4] = GPS.month;
    canMsgSys.data[5] = GPS.day;
    canMsgSys.data[6] = GPS.fix;
    canMsgSys.data[7] = GPS.fixquality;
    sendMessage(canMsgSys);

    // Speed + angle
    canMsgSys.can_id  = 0x0F7;
    canMsgSys.can_dlc = 8;

    *(float*)(my_bytes) = GPS.speed;    // Convert GPS.speed from float to four bytes
    canMsgSys.data[0] = my_bytes[3];    // Add each byte to the CAN message
    canMsgSys.data[1] = my_bytes[2];
    canMsgSys.data[2] = my_bytes[1];
    canMsgSys.data[3] = my_bytes[0];
    
    *(float*)(my_bytes) = GPS.angle;
    canMsgSys.data[4] = my_bytes[3];
    canMsgSys.data[5] = my_bytes[2];
    canMsgSys.data[6] = my_bytes[1];
    canMsgSys.data[7] = my_bytes[0];

    sendMessage(canMsgSys);

    // Latitude
    canMsgSys.can_id  = 0x0F8;
    canMsgSys.can_dlc = 5;

    *(float*)(my_bytes) = GPS.latitude;
    canMsgSys.data[0] = my_bytes[3];
    canMsgSys.data[1] = my_bytes[2];
    canMsgSys.data[2] = my_bytes[1];
    canMsgSys.data[3] = my_bytes[0];
    canMsgSys.data[4] = GPS.lat;
    
    sendMessage(canMsgSys);

    // Longitude
    canMsgSys.can_id  = 0x0F9;
    canMsgSys.can_dlc = 5;

    *(float*)(my_bytes) = GPS.longitude;
    canMsgSys.data[0] = my_bytes[3];
    canMsgSys.data[1] = my_bytes[2];
    canMsgSys.data[2] = my_bytes[1];
    canMsgSys.data[3] = my_bytes[0];
    canMsgSys.data[4] = GPS.lon;
    
    sendMessage(canMsgSys);

    // Altitude + satellites
    canMsgSys.can_id  = 0x0FA;
    canMsgSys.can_dlc = 5;

    *(float*)(my_bytes) = GPS.altitude;
    canMsgSys.data[0] = my_bytes[3];
    canMsgSys.data[1] = my_bytes[2];
    canMsgSys.data[2] = my_bytes[1];
    canMsgSys.data[3] = my_bytes[0];
    canMsgSys.data[4] = GPS.satellites;
    
    sendMessage(canMsgSys);

}

void pollMPPT() {
    /* Sends a blank message to the MPPTs so they send info
     * Not sure why but polling both request AND reply addr gives a reply
     */

    // 8-byte message, all of value 0x00
    canMsgSys.can_dlc = 8;
    for (int i = 0; i<sizeof(canMsgSys.data); i++)  {
        canMsgSys.data[i] = 0x00;
    }

    // Send poll and read reply
    canMsgSys.can_id = 0x711;
    mcp2515.sendMessage(&canMsgSys);
    canMsgSys.can_id = 0x771;
    mcp2515.sendMessage(&canMsgSys);
    readCAN();

    canMsgSys.can_id = 0x712;
    mcp2515.sendMessage(&canMsgSys);
    canMsgSys.can_id = 0x772;
    mcp2515.sendMessage(&canMsgSys);
    readCAN();
}



void load_config(const char *filename) {
    // Open file for reading
    File configFile = SD.open(filename);
    DEBUG_PRINT("Opening: ");
    DEBUG_PRINTLN(filename);
    //char configFile[] = "{\"speedtype\":\"kmh\",\"mode\":1,\"somevalues\":[1.1,1.23456]}";

    // Size of the stuff
    DynamicJsonDocument doc(192); // Tool to calculate this value: https://arduinojson.org/v6/assistant/

    // Parse the file
    DeserializationError error = deserializeJson(doc, configFile);

    if (error) {
        updateStatus(3, STAT_DEFAULTS);  // Cry if we have an error
        sendMessage(sysStatus);
        DEBUG_PRINT(F("deserializeJson() failed: "));
        DEBUG_PRINTLN(error.f_str());
        // But also do what we can and use some defaults
        set_defaults(config);
        return;
    }

    updateStatus(3, STAT_GOOD);  // No error
    sendMessage(sysStatus);

    // Put the read values into our config structure
    config.gps_update = doc["gps_update"];  DEBUG_PRINT("GPS UPDATE:\t");   DEBUG_PRINTLN(config.gps_update);
    config.time_fix = doc["time_fix"];      DEBUG_PRINT("TIME FIX:\t");     DEBUG_PRINTLN(config.time_fix);
    config.sd_update = doc["sd_update"];    DEBUG_PRINT("SD UPDATE:\t");    DEBUG_PRINTLN(config.sd_update);
    config.status_update = doc["status_update"];    DEBUG_PRINT("STATUS UPDATE:\t");    DEBUG_PRINTLN(config.status_update);
    config.gps_update = doc["mppt_update"]; DEBUG_PRINT("MPPT UPDATE:\t");  DEBUG_PRINTLN(config.mppt_update);
    config.serialCanMsg = doc["serialCanMsg"];      DEBUG_PRINT("SERIAL CAN:\t");       DEBUG_PRINTLN(config.serialCanMsg);
    config.value0 = doc["spare"][0];   DEBUG_PRINT("VAL0:\t");   DEBUG_PRINTLN(config.value0);
    config.value1 = doc["spare"][1];   DEBUG_PRINT("VAL1:\t");   DEBUG_PRINTLN(config.value1);

    DEBUG_PRINTLN("Configuration set from SD, closing file");
    // Close the file; we can only have one open at a time
    configFile.close();

    // If wanted to do a bigger file: https://arduinojson.org/v6/how-to/deserialize-a-very-large-document/
}

void set_defaults(conf &config) {
    /* We run this if for whatever reason we can't read config.txt
        a)  Either can't initialise SD card, or
        b)  can't find file or deserialize it.
        c)  At the moment if a field doesn't exist, it's incorrectly read as zero.
     */
    config.gps_update = 2000; // 2000ms update on position and time
    config.sd_update = 2000;
    config.status_update = 500;
    config.time_fix = 1000;
    config.mppt_update = 1000;
    config.serialCanMsg = 1;    // Default send CAN messages on serial
    config.value0 = -1;
    config.value1 = -1;
    DEBUG_PRINTLN("Using defaults");
    /// Actually probably better to set these at the start and overwrite IF we can. ie if we can't we just use the original value
    /// However, I think a null field just overwrites as zero...
}

void log_start_up(char *filename){
    // filename is "dataYYMMDD00.csv"
    // https://learn.adafruit.com/adafruit-data-logger-shield/using-the-real-time-clock-3

    // Get date. If GPS signal is not available this will default to 00/00/00
    int8_t YY = GPS.year;
    int8_t MM = GPS.month;
    int8_t DD = GPS.day;

    // Replace YYMMDD with date
    filename[0] = YY/10 + '0';
    filename[1] = YY%10 + '0';
    filename[2] = MM/10 + '0';
    filename[3] = MM%10 + '0';
    filename[4] = DD/10 + '0';
    filename[5] = DD%10 + '0';

    // If we've already recorded today, +1 to trailing number
    for (uint8_t i = 0; i < 100; i++) {
        filename[10-4] = i/10 + '0';
        filename[11-4] = i%10 + '0';
        if (! SD.exists(filename)) {
            dataFile = SD.open(filename, FILE_WRITE); // Only create a new file which doesn't exist
            if (dataFile) {
                DEBUG_PRINT("Opening file: ");  DEBUG_PRINTLN(filename);
                // Log our current configuration in some form
                //dataFile.println("Logging to file");
                dataFile.println("ESC ID0 ID1 DLC B0 B1 B2 B3 B4 B5 B6 B7 CRC0 CRC1");
                dataFile.println("");
                //dataFile.flush();
                updateStatus(3, 100+i);   // Config file opened with i=i
                sendMessage(sysStatus);
            }
            // if the file isn't open, pop up an error:
            else {
                DEBUG_PRINT("Could not open file: ");   DEBUG_PRINTLN(filename);
                /// Some sort of backup filename here?
                // will only log in loop if dataFile==True
                updateStatus(3, 90);   // Config file not opened
                sendMessage(sysStatus);
            
            }
            return;
        }
    }
}

void print_datetimefix() {
    Serial.print("\tDate: ");
    Serial.print(GPS.day, DEC); Serial.print('/');
    Serial.print(GPS.month, DEC); Serial.print("/20");
    Serial.println(GPS.year, DEC);
    // Time
    Serial.print("\tTime: ");
    if (GPS.hour < 10) { Serial.print('0'); }
    Serial.print(GPS.hour, DEC); Serial.print(':');
    if (GPS.minute < 10) { Serial.print('0'); }
    Serial.print(GPS.minute, DEC); Serial.print(':');
    if (GPS.seconds < 10) { Serial.print('0'); }
    Serial.println(GPS.seconds, DEC);
    // Fix information
    Serial.print("\tFix: "); Serial.print((int)GPS.fix);
    Serial.print(" quality: "); Serial.println((int)GPS.fixquality);
}

void print_location() {
    Serial.print("Location: ");
    Serial.print(GPS.latitude, 4); Serial.print(GPS.lat);
    Serial.print(", ");
    Serial.print(GPS.longitude, 4); Serial.println(GPS.lon);

    Serial.print("Speed (knots): "); Serial.println(GPS.speed);
    Serial.print("Angle: "); Serial.println(GPS.angle);
    Serial.print("Altitude: "); Serial.println(GPS.altitude);
    Serial.print("Satellites: "); Serial.println((int)GPS.satellites);
}

void pollSensor() {
    // Add additional sensors here

    /** Additional data stream template **/
    /*
    if (millis() - timer > config.ds1_ud) {
        DEBUG_PRINTLN("Data stream group 1 is being read and transmitted.");
        ds1_timer = millis(); // reset the timer
        int ds1val_a;
        ds1val_a = obtain_ds1val_a();
        canMsg.can_id = 0x123;
        canMsg.can_dlc = sizeof(ds1val_a);
        canMsg.data[0] = (ds1val_a >> 32) & 0xFF;
        canMsg.data[1] = (ds1val_a >> 16) & 0xFF;
        canMsg.data[2] = (ds1val_a >> 8) & 0xFF;
        canMsg.data[3] = (ds1val_a >> 0) & 0xFF;
    }

    if (millis() - timer > config.ds2_ud) { // Currently both use canMsg - be careful we don't overwrite
        DEBUG_PRINTLN("Data stream group 2 is being read and transmitted.");
        ds2_timer = millis(); // reset the timer
        int ds2val_a;
        ds2val_a = obtain_ds2val_a();
        canMsg.can_id = 0x124;
        canMsg.can_dlc = sizeof(ds2val_a);
        canMsg.data[0] = (ds2val_a >> 32) & 0xFF
        canMsg.data[1] = (ds2val_a >> 16) & 0xFF;
        canMsg.data[2] = (ds2val_a >> 8) & 0xFF;
        canMsg.data[3] = (ds2val_a >> 0) & 0xFF;
    }
    */
}

/*
void save_config(const char *filename, conf &config) { // We might want to
    // Save current config
    // https://arduinojson.org/v5/example/config/
}
*/

/*  This would be a nice function to have working: report SD info on startup
void sd_info(){
    // Check number of files on SD card and storage usage: https://www.arduino.cc/en/Tutorial/LibraryExamples/CardInfo
    SdVolume volume;
    SdFile root;
    if (!volume.init(card)) {
        DEBUG_PRINTLN("Could not find FAT16/FAT32 partition.\nMake sure you've formatted the card");
        return;
    }
    uint32_t volumesize;
    volumesize = volume.blocksPerCluster();    // clusters are collections of blocks
    volumesize *= volume.clusterCount();       // we'll have a lot of clusters
    volumesize /= 2;                           // SD card blocks are always 512 bytes (2 blocks are 1KB)
    // Report these via XBee and DEBUG_SERIAL if DEBUG==1
}
*/
