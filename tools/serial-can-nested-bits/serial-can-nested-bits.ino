/* serial-can-nested-bits
 *  Sends 8 byte message with first nibble of data incrementing
 *  each loop.
 *  
 *  0x771 is the address for MPPT1.
 *  Use 0x987 to test bit unpacking functionality but make sure this
 *  doesn't disrupt any real data you have.
 *  
 *  t4sr, Jan 2022
 *  
 */

uint16_t can_id = 0x987;
uint8_t can_dlc = 8;
uint8_t payload[] = {0x0F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t counter = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  // ESC character, ID and DLC  
  Serial.write(0x7E);
  Serial.write((can_id >> 8) & 0xFF);
  Serial.write(can_id & 0xFF);
  Serial.write(can_dlc);

  payload[0] = 0x0F + (counter << 4) & 0xFF;

  // Data
  for (int i = 0; i<can_dlc; i++)  {
    Serial.write(payload[i]);
  }

  // CRC - not calculated so will fail.
  Serial.write(0xAB);
  Serial.write(0xCD);

  counter++;
  delay(1000);
}
