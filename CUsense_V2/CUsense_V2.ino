#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ESP32httpUpdate.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "Free_Fonts.h"
#include <SHTC3.h>
#include "SPIFFS.h"
#include "average.h"
#include "read_pms.h"
#include "time.h"
#include <JPEGDecoder.h>
#include "jpeg1.h"
#include "jpeg2.h"

//#define mqtt_server "161.200.80.206"
IPAddress mqtt_server(161, 200, 80, 206);
#define current_version 1.12
#define versionINF "https://raw.githubusercontent.com/Rewlgil/testing/master/text.txt"

#define set_screen_interval 60000
#define send_interval 300000      //(5 * 60 * 1000)

SHTC3 tempSensor(Wire);
WiFiManager wifiManager;
TFT_eSPI tft = TFT_eSPI();
WiFiClient espClient;
PubSubClient client(espClient);

// MQTT
char topic[22] = "pmsensor/";
char doc_char[130];
String mac;
char mac_array[13];
// ntp time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7*3600;
const int   daylightOffset_sec = 0;

uint32_t last_read_temp = 0, last_send_time = 0, last_set_screen = 0;
bool connection_state = true;

void setup() 
{
  SPIFFS.begin();
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  delay(2000);
  //setup screen
  tft.setTextColor(TFT_WHITE); tft.setTextSize(1); tft.setFreeFont(FSB12);
  tft.setCursor(10, 20);
  tft.println("Setup");
  tft.setTextSize(1); tft.setFreeFont(FS9);
  mac = WiFi.macAddress();
  mac.replace(":", "");
  for (uint8_t i = 0; i < 13; i++)
    mac_array[i] = mac[i];
  tft.print("MAC Address: "); tft.println(mac);
  tft.println("Connecting to Wi-Fi...");
  wifiManager.setTimeout(180);
  wifiManager.setAPCallback(configModeCallback);
  if(! wifiManager.autoConnect(mac_array)) {
     Serial.println("failed to connect and hit timeout");
     tft.println("Failed to connect");
     tft.println("Restarting...");
     delay(3000);
     ESP.restart();
  }
  Serial.println("WiFi connected");
  tft.print("Connected to: "); tft.println(WiFi.SSID());
  checkUpdate();
  //MQTT
  client.setServer(mqtt_server, 1883);
  strcat(topic, mac_array);
  Serial.print("topic: ");Serial.println(topic);
  delay(5000);
  //clear setup screen
  tft.fillScreen(TFT_BLACK);
  Serial2.begin(9600);  // (Rx, TX) = (16, 17)
  Wire.begin();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  //set template screen
  
  
  tft.drawFastHLine(0,   230, 350, TFT_WHITE);
  tft.drawFastVLine(175, 231,  89, TFT_WHITE);
  tft.drawFastVLine(350,   0, 320, TFT_WHITE);
  tft.fillRect(0, 20, 135, 90, tft.color565(0, 50, 100)); // Green
  tft.fillTriangle(135, 20, 135, 110, 200, 110, tft.color565(0, 50, 100)); // Green
  tft.fillRect(0, 110, 135, 90, tft.color565(255, 0, 255)); // Pink
  tft.fillTriangle(135, 200, 135, 110, 200, 110, tft.color565(255, 0, 255)); // Pink
  tft.fillCircle(240, 110, 100, tft.color565(200,   0,  0)); // Red
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.loadFont("DS-Digital-BoldItalic-50");
  tft.drawString("PM 2.5", 240, 60);
  tft.setTextDatum(TL_DATUM);
  tft.loadFont("DS-Digital-Italic-40");
  tft.drawString("PM 1", 5, 25);
  tft.drawString("PM 10", 5, 115);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("ug/m3", 340, 220);
  tft.unloadFont();
  drawArrayJpeg(thermometer, sizeof(thermometer),   5, 251);
  drawArrayJpeg(humidity,    sizeof(humidity),    180, 251);
  //set footers tab
  tft.fillRect(0, 300, 480, 20, TFT_LIGHTGREY);
  tft.setTextColor(TFT_BLUE);   tft.setFreeFont(FS9); tft.setTextDatum(TL_DATUM);
  tft.drawString("MAC: " + mac, 5, 305, GFXFF);
  tft.drawString("Firmware: ", 192, 305, GFXFF);  tft.drawFloat(current_version, 2, 267, 305, GFXFF);
  tft.drawString("Wi-Fi: ",  310, 305, GFXFF);
  setTime();
  last_send_time = millis();
  last_set_screen = millis();
}

float Temp,Humid;

struct average_data {
  float PM_10, PM_25, PM_100;
  float Temp, Humid;
};
struct average_data avg;

MovingAverage<uint32_t> pm10;
MovingAverage<uint32_t> pm25;
MovingAverage<uint32_t> pm100;
MovingAverage<float> temp;
MovingAverage<float> humid;

void loop() {
  if (!client.connected()) {
    connection_state = false;
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
  }
  if ((millis() - last_read_temp) >= 1000) {
    setTime();
    tempSensor.begin(true);
    tempSensor.sample();
    Temp = tempSensor.readTempC();
    Humid = tempSensor.readHumidity();
    temp.giveAVGValue(Temp);
    humid.giveAVGValue(Humid);
//    Serial.printf("Temp = %.2f C\tHumid = %.2f %%\n", Temp, Humid);
    last_read_temp = millis();
  }
  if ((millis() - last_set_screen) >= set_screen_interval)
  {
    //get AVG value
    pm10.getAVGValue(&avg.PM_10);
    pm25.getAVGValue(&avg.PM_25);
    pm100.getAVGValue(&avg.PM_100);
    temp.getAVGValue(&avg.Temp);
    humid.getAVGValue(&avg.Humid);
    avg.Temp = round(avg.Temp * 100) / 100;
    avg.Humid = round(avg.Humid * 100) / 100;
    setScreen();
    last_set_screen = millis();
  }

  if ((millis() - last_send_time) >= send_interval)
  {
    //get AVG value
    pm10.getAVGValue(&avg.PM_10);
    pm25.getAVGValue(&avg.PM_25);
    pm100.getAVGValue(&avg.PM_100);
    temp.getAVGValue(&avg.Temp);
    humid.getAVGValue(&avg.Humid);
    avg.Temp = round(avg.Temp * 100) / 100;
    avg.Humid = round(avg.Humid * 100) / 100;
    //send data to server
    StaticJsonDocument<150> doc;
    doc["id"]   = mac_array;
    doc["pm1"]  = round(avg.PM_10);
    doc["pm25"] = round(avg.PM_25);
    doc["pm10"] = round(avg.PM_100);
    doc["Temp"] = avg.Temp;
    doc["Humid"] = avg.Humid;
    Serial.print("\n");
    serializeJsonPretty(doc, Serial);
    Serial.print("\n");
    serializeJsonPretty(doc, doc_char);
    client.publish(topic, doc_char);
    displaySystemMSG();
    last_send_time = millis();
  }
  
  client.loop();
}

void configModeCallback (WiFiManager *mywifiManager){
  tft.println("Starting AP...");
  tft.print("AP name: ");
  tft.println(mywifiManager -> getConfigPortalSSID());
}

void setScreen(void){
  static uint16_t PM25_color = 0;
  if (round(avg.PM_25) < 30) PM25_color = tft.color565(0, 100, 0); //Green
  else if (round(avg.PM_25) >= 30 && round(avg.PM_25) < 50) PM25_color = tft.color565(200, 100, 0); //Orange
  else if (round(avg.PM_25) >= 50) PM25_color = tft.color565(200, 0, 0); //Red
  tft.fillCircle(240, 110, 100, PM25_color);         // PM2.5
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.loadFont("DS-Digital-BoldItalic-50");
  tft.drawString("PM 2.5", 240, 60);
  tft.loadFont("DS-Digital-Bold-100");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PM25_color);
  tft.drawString("        ", 240, 140);
  tft.drawNumber(round(avg.PM_25), 240, 140);
  tft.loadFont("DS-Digital-BoldItalic-50");          // PM1.0 && PM10
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, tft.color565(0, 50, 100));
  tft.drawString("         ", 60, 68);
  tft.drawNumber(round(avg.PM_10),  60,  68);
  tft.setTextColor(TFT_WHITE, tft.color565(255, 0, 255));
  tft.drawString("        ", 60, 158);
  tft.drawNumber(round(avg.PM_100), 60, 158);
  tft.setTextDatum(ML_DATUM);                        //  Temp && Humid
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("             ", 40,  275, GFXFF);
  tft.drawString("             ", 220, 275, GFXFF);
  tft.drawFloat(avg.Temp, 2, 40,  275);
  tft.drawFloat(avg.Humid, 2, 220, 275);
  tft.loadFont("DS-Digital-Italic-40");              // unit
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("ug/m3", 340, 220);
  tft.unloadFont();
}

void setTime(void){
  struct tm timeinfo;
  static char timeHour[3];
  static char timeMin[3];
  static char timeSec[3];
  static bool first_time = true;
  
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  strftime(timeHour,3, "%H", &timeinfo);
  strftime(timeMin, 3, "%M", &timeinfo);
  strftime(timeSec, 3, "%S", &timeinfo);
  tft.setRotation(0);
  tft.loadFont("DS-Digital-Bold-70");
  tft.setTextDatum(CL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (first_time){
    tft.drawString(":", 110, 420, GFXFF);
    tft.drawString(":", 207, 420, GFXFF);
  }
  tft.drawString((String)timeSec + " ", 227, 420);
  if ((timeSec[0] == '0' && timeSec[1] == '0')|| first_time)
    {tft.drawString("      ", 130, 420); tft.drawString((String)timeMin, 130, 420);}
  if ((timeMin[0] == '0' && timeMin[1] == '0') || first_time)
    {tft.drawString("      ", 30, 420);  tft.drawString(" " + (String)timeHour, 30, 420);}
  tft.setRotation(1);
  first_time = false;
}

void reconnectMQTT(){
  static uint32_t last_attemp = 0;
  while( (!client.connected() && ((millis() - last_attemp) > 5000))){
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client")){
      Serial.println("connected");
      connection_state = true;
      displaySystemMSG();
    }
    else {
      Serial.print("failed, code = ");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      last_attemp = millis();
      connection_state = false;
      displaySystemMSG();
      return;
    }
  }
}

void displaySystemMSG(void){
  static bool last_connection_state = !connection_state;
  tft.unloadFont();   tft.setFreeFont(FS9); tft.setTextDatum(TL_DATUM);
  if (last_connection_state != connection_state){
    if(connection_state)  {tft.setTextColor(TFT_DARKGREEN, TFT_LIGHTGREY); tft.drawString("                       ", 360, 305, GFXFF);  tft.drawString(WiFi.SSID(), 360, 305, GFXFF);}
    else                  {tft.setTextColor(TFT_RED,       TFT_LIGHTGREY); tft.drawString("LOSS",  360, 305, GFXFF);} tft.setTextColor(TFT_BLUE);
    last_connection_state = connection_state;
  }
}
