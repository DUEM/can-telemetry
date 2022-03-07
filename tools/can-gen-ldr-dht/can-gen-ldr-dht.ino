#include "DHT.h"
#include <SPI.h>
#include <mcp2515.h>

/* can-gen-ldr-dht
 *  Quick test device for solar car telemetry.
 *  Measures temperature, humidity and LDR brightness.
 *  Transmits this over CAN with heat index too.
 *  
 *  t4sr, Sept 2021
 *  
 */

#define DHTPIN 2
#define LDRPIN A0


#define DHTTYPE DHT22
/* Choose correct DHTTYPE:
 *  DHT11:  DHT 11
 *  DHT22:  DHT 22 (AM2302), AM2321
 *  DHT21:  DHT 21 (AM2301)
 */

int ldr;

struct can_frame canMsg1;
struct can_frame canMsg2;
MCP2515 mcp2515(10);

uint8_t my_bytes[sizeof(float)];

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(9600);
  Serial.println("enviroCAN.ino");
  dht.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_125KBPS);
  mcp2515.setNormalMode();

  canMsg1.can_id  = 0x1B1;
  canMsg1.can_dlc = 8;

  canMsg2.can_id  = 0x1B2;
  canMsg2.can_dlc = 8;
  
  Serial.println();
  Serial.println("Temp (°C)\tHumd (%)\tHI(°C)\t\tLDR (/1024)");
}

void loop() {

  delay(2000);
  
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) { // Check for failed reading. If so exit early and try again
    Serial.println("DHT reading failed");
    //return;
  }

  float hic = dht.computeHeatIndex(t, h, false);  // isFahreheit = false

  ldr = analogRead(LDRPIN);

  Serial.print(" ");
  Serial.print(t);
  Serial.print("\t\t ");
  Serial.print(h);
  Serial.print("\t\t ");
  Serial.print(hic);
  Serial.print("\t\t ");
  Serial.print(ldr);
  Serial.print("\n");

  *(float*)(my_bytes) = t;
  canMsg1.data[0] = my_bytes[3];
  canMsg1.data[1] = my_bytes[2];
  canMsg1.data[2] = my_bytes[1];
  canMsg1.data[3] = my_bytes[0];
  *(float*)(my_bytes) = h;
  canMsg1.data[4] = my_bytes[3];
  canMsg1.data[5] = my_bytes[2];
  canMsg1.data[6] = my_bytes[1];
  canMsg1.data[7] = my_bytes[0];

  mcp2515.sendMessage(&canMsg1);
  
  *(float*)(my_bytes) = hic;
  canMsg2.data[0] = my_bytes[3];
  canMsg2.data[1] = my_bytes[2];
  canMsg2.data[2] = my_bytes[1];
  canMsg2.data[3] = my_bytes[0];
  canMsg2.data[4] = (ldr >> 32) & 0xFF;
  canMsg2.data[5] = (ldr >> 16) & 0xFF; 
  canMsg2.data[6] = (ldr >> 8) & 0xFF; 
  canMsg2.data[7] = (ldr >> 0) & 0xFF; 

  mcp2515.sendMessage(&canMsg2);
}
