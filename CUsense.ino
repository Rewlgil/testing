#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ESP32httpUpdate.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "Free_Fonts.h"
#include <SHTC3.h>

#define mqtt_server "161.200.80.206"
#define current_version 1.00
#define versionINF "https://raw.githubusercontent.com/Rewlgil/testing/master/text.txt"

#define numreading 100
#define timeout 60000

SHTC3 tempSensor(Wire);
WiFiManager wifiManager;
TFT_eSPI tft = TFT_eSPI();
WiFiClient espClient;
PubSubClient client(espClient);

void configModeCallback (WiFiManager *mywifiManager){
  tft.println("Starting AP...");
  tft.print("AP name: ");
  tft.println(mywifiManager -> getConfigPortalSSID());
}
// for MQTT
char topic[22] = "pmsensor/";
char doc_char[130];
String mac;
char mac_array[13];

uint32_t last_read_temp = 0, last_send_time = 0;
bool publish_state = true, server_state = true;

void setup() 
{
  delay(2000);
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLUE); tft.setTextSize(1); tft.setFreeFont(FSB12);
  tft.setCursor(10, 20);
  tft.println("Setup");
  tft.setTextSize(1); tft.setFreeFont(FS9);
  tft.println("Connecting to Wi-Fi...");
  wifiManager.setAPCallback(configModeCallback);
  if(! wifiManager.autoConnect("AutoConnectAP")) {
     Serial.println("failed to connect and hit timeout");
     tft.println("Failed to connect");
     tft.println("Restarting...");
     delay(3000);
     ESP.restart();
  }
  Serial.println("WiFi connected");
  tft.print("Connected to: "); tft.print(wifiManager.getSSID());
  mac = WiFi.macAddress();
  tft.print("     MAC Address: "); tft.println(mac);
  checkUpdate();
  //MQTT
  client.setServer(mqtt_server, 1883);
  mac.replace(":", "");
  for (uint8_t i = 0; i < 13; i++)
    mac_array[i] = mac[i];
  strcat(topic, mac_array);
  Serial.print("topic: ");Serial.println(topic);
  //
  delay(5000);
  tft.fillScreen(TFT_BLACK);
  Serial2.begin(9600);  // (Rx, TX) = (16, 17)
  Wire.begin();
  tft.setFreeFont(FS18);
  tft.setTextColor(TFT_PINK);   tft.drawString("Sensor Value",  10, 10, GFXFF);
  tft.setTextColor(TFT_CYAN);   tft.drawString("Server Value", 240, 10, GFXFF);

  tft.setTextColor(TFT_ORANGE); tft.setFreeFont(FS12);
  tft.drawString("Particulate Matter  (ug/m3)", 10, 50,  GFXFF);
  tft.drawString("Temperature & Humidity",   10, 190, GFXFF);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);  tft.setFreeFont(FF2);
  tft.drawString("PM1.0:", 10,  90, GFXFF);
  tft.drawString("PM2.5:", 10, 120, GFXFF);
  tft.drawString("PM10 :", 10, 150, GFXFF);

  tft.drawString("PM1.0:", 240,  90, GFXFF);
  tft.drawString("PM2.5:", 240, 120, GFXFF);
  tft.drawString("PM10 :", 240, 150, GFXFF);

  tft.drawString("Temp:       C", 10,  230, GFXFF);
  tft.drawString("Humid:      %", 10,  260, GFXFF);

  tft.drawString("Temp:       C", 240, 230, GFXFF);
  tft.drawString("Humid:      %", 240, 260, GFXFF);
  
  tft.fillRect(0, 300, 480, 20, TFT_LIGHTGREY);
  tft.setTextColor(TFT_BLUE);   tft.setFreeFont(FS9);
  tft.drawString("Firmware: ",        5, 305);  tft.drawFloat(current_version, 2, 80, 305);
  tft.drawString("Server state: ",  135, 305);
  tft.drawString("Publish state: ", 300, 305);
  last_send_time = millis();
}
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

float Temp,Humi;

struct average_data {
  float PM_10, PM_25, PM_100;
  float Temp, Humi;
};
struct average_data avg;

template<class T>
class MovingAverage
{
  public:
    void giveAVGValue(T getdata);
    void getAVGValue(float *avg_value);
  private:
    T value[numreading] = {0};
    T sum = 0;
    uint16_t index = 0;
    boolean first_time = true;
};
template<class T>
void MovingAverage<T>::giveAVGValue(T getdata)
{
  sum -= value[index];
  value[index] = getdata;
  sum += value[index];
//******print value for debug***********************
//  for (uint16_t i = 0; i < numreading; i++)
//  {
//    Serial.print(value[i]);   Serial.print("\t");
//    if ((i % 20) == 19)       Serial.print("\n");
//  }
//  Serial.print("\n");
//**************************************************
  index++;
  if (index >= numreading)  {index = 0; first_time = false;}
}

template<class T>
void MovingAverage<T>::getAVGValue(float *avg_value)
{
  if (first_time == true)
    *avg_value = (float)sum / index;
  else
    *avg_value = (float)sum / numreading;
//******print value for debug***********************
  for (uint16_t i = 0; i < numreading; i++)
  {
    Serial.print(value[i]);   Serial.print("\t");
    if ((i % 20) == 19)       Serial.print("\n");
  }
  Serial.print("\n");
//**************************************************
}
MovingAverage<uint32_t> pm10;
MovingAverage<uint32_t> pm25;
MovingAverage<uint32_t> pm100;
MovingAverage<float> temp;
MovingAverage<float> humi;

void loop() {
  if (!client.connected()) {
    server_state = false;
    displaySystemMSG();
    reconnectMQTT();
  }
  if ( Serial2.available() && readPMSdata() ) {
    pm10.giveAVGValue(data.pm10_standard);
    pm25.giveAVGValue(data.pm25_standard);
    pm100.giveAVGValue(data.pm100_standard);
//    Serial.print("pm1.0 = ");   Serial.println(data.pm10_standard);
//    Serial.print("pm2.5 = ");   Serial.println(data.pm25_standard);
//    Serial.print("pm10  = ");   Serial.println(data.pm100_standard);
    setLiveScreen();
  }
  if ((millis() - last_read_temp) >= 1000) {
    tempSensor.begin(true);
    tempSensor.sample();
    Temp = tempSensor.readTempC();
    Humi = tempSensor.readHumidity();
    temp.giveAVGValue(Temp);
    humi.giveAVGValue(Humi);
//    Serial.printf("Temp = %.2f C\tHumi = %.2f %%\n", Temp, Humi);
    last_read_temp = millis();
  }
  if ((millis() - last_send_time) >= timeout)
  {
    //get AVG value
    pm10.getAVGValue(&avg.PM_10);
    pm25.getAVGValue(&avg.PM_25);
    pm100.getAVGValue(&avg.PM_100);
    temp.getAVGValue(&avg.Temp);
    humi.getAVGValue(&avg.Humi);
    avg.Temp = round(avg.Temp * 100) / 100;
    avg.Humi = round(avg.Humi * 100) / 100;
    //send data to server
    StaticJsonDocument<150> doc;
    doc["id"]   = mac_array;
    doc["pm1"]  = round(avg.PM_10);
    doc["pm25"] = round(avg.PM_25);
    doc["pm10"] = round(avg.PM_100);
    doc["Temp"] = avg.Temp;
    doc["Humid"] = avg.Humi;
    Serial.print("\n");
    serializeJsonPretty(doc, Serial);
    Serial.print("\n");
    serializeJsonPretty(doc, doc_char);
    publish_state = client.publish(topic, doc_char);
    displaySystemMSG();
    setServerScreen();
    last_send_time = millis();
  }
  
  client.loop();
}

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

//uint16_t AQI25(uint16_t pm, uint32_t *color){
//  uint8_t i_max, i_min, x_max, x_min;
//  uint16_t aqi;
//  if      (pm >= 0  && pm <= 25) { i_min = 0;   i_max = 25;  x_min = 0;  x_max = 25; *color = TFT_SKYBLUE;}
//  else if (pm >= 26 && pm <= 37) { i_min = 26;  i_max = 50;  x_min = 26; x_max = 37; *color = tft.color565(0,250,100);}
//  else if (pm >= 38 && pm <= 50) { i_min = 51;  i_max = 100; x_min = 38; x_max = 50; *color = tft.color565(250,250,0);}
//  else if (pm >= 51 && pm <= 90) { i_min = 101; i_max = 200; x_min = 51; x_max = 90; *color = tft.color565(255,100,0);}
//  else if (pm >= 91)             { i_min = 110; i_max = 111; x_min = 0;  x_max = 1;  *color = TFT_RED;}
//
//  aqi = round(((float)(i_max - i_min) / (x_max - x_min)) * (float(pm) - x_min) + i_min);
//  return aqi;
//}

void checkUpdate(){
  String inf;
  Serial.println("checking for update...");
  tft.setTextColor(TFT_BLUE); tft.println("Checking for update...");
  HTTPClient httpClient;
  httpClient.begin( versionINF );
  int httpCode = httpClient.GET();
  if( httpCode == 200 ) {
    String json_code = httpClient.getString();
    Serial.print("JSON Code: "); Serial.println(json_code);
    StaticJsonDocument<140> code;
    DeserializationError error = deserializeJson(code, json_code);
    if (error) {
      Serial.print("deserializeJson() failed with code ");
      Serial.println(error.c_str());
      tft.setTextColor(TFT_RED); tft.print("deserializeJson failed with code "); tft.println(error.c_str());
      return;
    }
    float available_version = code["version"];
    String code_url = code["code_url"];
    Serial.printf("available version: %.2f\ncurrent version: %.2f\n", available_version, current_version);
    tft.setTextColor(TFT_GREEN);
    tft.print("available version: "); tft.println(available_version, 2);
    tft.print("current version: "); tft.println(current_version, 2);
    Serial.print("code_url: "); Serial.println(code_url);
    tft.print("code_url: "); tft.println(code_url);
    if(round(current_version * 100) < round(available_version * 100)){
      Serial.println("Updating...");
      tft.setTextColor(TFT_BLUE); tft.println("Updating...");
      t_httpUpdate_return ret = ESPhttpUpdate.update(code_url);

      switch(ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
            tft.setTextColor(TFT_RED);
            tft.print("HTTP_UPDATE_FAILD Error "); tft.println(ESPhttpUpdate.getLastError());
            tft.println(ESPhttpUpdate.getLastErrorString().c_str());  delay(3000);  ESP.restart();
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            tft.setTextColor(TFT_RED);  tft.println("HTTP_UPDATE_NO_UPDATES");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK");
            tft.setTextColor(TFT_GREEN);  tft.println("HTTP_UPDATE_OK");
            break;
      }
      return;
    }
    else{
      Serial.println("Already on latest version");
      tft.setTextColor(TFT_GREEN);  tft.println("Already on latest version");
      return;
      }
  }
  else{
    Serial.printf("Firmware version check failed, got HTTP response code %d\n", httpCode); 
    tft.setTextColor(TFT_RED);  tft.print("Firmware version check failed, got HTTP response code ");  tft.println(httpCode);
    return;
    }
  httpClient.end();
}

void setLiveScreen(void){
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setFreeFont(FF2);
  tft.setTextSize(1);                     tft.setTextDatum(TL_DATUM);
  tft.drawString("      ", 90,  90, GFXFF);
  tft.drawString("      ", 90, 120, GFXFF);
  tft.drawString("      ", 90, 150, GFXFF);
  tft.drawString("       ", 90, 230, GFXFF);
  tft.drawString("       ", 90, 260, GFXFF);
  
  tft.setTextColor(TFT_WHITE);
  tft.drawNumber(data.pm10_standard,  100,  90, GFXFF);
  tft.drawNumber(data.pm25_standard,  100, 120, GFXFF);
  tft.drawNumber(data.pm100_standard, 100, 150, GFXFF);
  tft.drawFloat(Temp, 2, 95, 230, GFXFF);
  tft.drawFloat(Humi, 2, 95, 260, GFXFF);
}

void setServerScreen(void){
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setFreeFont(FF2);
  tft.setTextSize(1);                     tft.setTextDatum(TL_DATUM);
  tft.drawString("      ", 320,  90, GFXFF);
  tft.drawString("      ", 320, 120, GFXFF);
  tft.drawString("      ", 320, 150, GFXFF);
  tft.drawString("       ", 320, 230, GFXFF);
  tft.drawString("       ", 320, 260, GFXFF);
  
  tft.setTextColor(TFT_WHITE);
  tft.drawNumber(round(avg.PM_10),  325,  90, GFXFF);
  tft.drawNumber(round(avg.PM_25),  325, 120, GFXFF);
  tft.drawNumber(round(avg.PM_100), 325, 150, GFXFF);
  tft.drawFloat(avg.Temp, 2, 325, 230, GFXFF);
  tft.drawFloat(avg.Humi, 2, 325, 260, GFXFF);
}

//void setAVGScreen(uint16_t PMdata){
//  static uint16_t aqi, old_aqi = 0;
//  static uint32_t color = 0, old_color = 0;
//  aqi = AQI25(PMdata, &color);  //calculate AQI value
////      Serial.printf("PM = %d\t AQI = %d\n", data.pm25_standard, aqi);
////  if (aqi != old_aqi){
////    if (color != old_color){
//        tft.fillRoundRect(10, 20, 150, 140, 20, color);
//        tft.setFreeFont(FF6); tft.setTextColor(TFT_WHITE);  tft.setTextSize(1);
//        tft.drawCentreString("PM2.5 AQI", 90, 35, GFXFF);
////        old_color = color;
////    }
////      Serial.print(aqi);  Serial.print("\t"); Serial.println(color, HEX);
//      tft.setFreeFont(FF7);      tft.setTextSize(2);  tft.setTextDatum(MC_DATUM);
//      tft.setTextColor(color);       tft.drawNumber(old_aqi, 85, 100, GFXFF);  //clear aqi value
//      tft.setTextColor(TFT_WHITE);   tft.drawNumber(aqi, 85, 100, GFXFF);      //set new aqi value
////    old_aqi = aqi;
////  }
//}

void reconnectMQTT(){
  static uint32_t last_attemp = 0;
  while( (!client.connected() && ((millis() - last_attemp) > 5000))){
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client")){
      Serial.println("connected");
      server_state = true;
      displaySystemMSG();
    }
    else {
      Serial.print("failed, code = ");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      last_attemp = millis();
      server_state = false;
      displaySystemMSG();
      return;
    }
  }
}

void displaySystemMSG(void){
  static bool last_server_state = !server_state;
  static bool last_publish_state = !publish_state;
  tft.setFreeFont(FS9);
  if (last_server_state != server_state){
    if(server_state) {tft.setTextColor(TFT_DARKGREEN, TFT_LIGHTGREY); tft.drawString("OK         ", 230, 305);}
    else             {tft.setTextColor(TFT_RED,       TFT_LIGHTGREY); tft.drawString("FAILED",  230, 305);} tft.setTextColor(TFT_BLUE);
    last_server_state = server_state;
  }
  if (last_publish_state != publish_state){
    if(publish_state) {tft.setTextColor(TFT_DARKGREEN, TFT_LIGHTGREY); tft.drawString("OK         ", 405, 305);}
    else              {tft.setTextColor(TFT_RED,       TFT_LIGHTGREY); tft.drawString("FAILED",  405, 305);}
    last_publish_state = publish_state;
  }
}
