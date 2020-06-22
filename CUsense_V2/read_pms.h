struct pms5003data {
  uint16_t framelen;
  uint16_t pm10_standard, pm25_standard, pm100_standard;
  uint16_t pm10_env, pm25_env, pm100_env;
  uint16_t particles_03um, particles_05um, particles_10um, 
           particles_25um, particles_50um, particles_100um;
  uint16_t unused;
  uint16_t checksum;
};
struct pms5003data data;

boolean readPMSdata(void) {
  if (! Serial2.available())  return false;
  // Read a byte at a time until we get to the special '0x42' start-byte
  if (Serial2.peek() != 0x42) {
    Serial2.read();
    return false;
  }
  // Now read all 32 bytes
  if (Serial2.available() < 32) return false;
  uint8_t buffer[32];
  uint16_t sum = 0;
  Serial2.readBytes(buffer, 32);
  // get checksum ready
  for (uint8_t i=0; i<30; i++) {
    sum += buffer[i];
  }
  /* debugging
  for (uint8_t i=2; i<32; i++) {
  Serial.print("0x"); Serial.print(buffer[i], HEX); Serial.print(", ");
  }
  Serial.println();
  */
  // The data comes in endian'd, this solves it so it works on all platforms
  uint16_t buffer_u16[15];
  for (uint8_t i=0; i<15; i++) {
    buffer_u16[i] = buffer[2 + i*2 + 1];
    buffer_u16[i] += (buffer[2 + i*2] << 8);
  }
  // put it into a nice struct :)
  memcpy((void *)&data, (void *)buffer_u16, 30);
  if (sum != data.checksum) {
    Serial.println("Checksum failure");
    return false;
  }
  // success!
  return true;
}
