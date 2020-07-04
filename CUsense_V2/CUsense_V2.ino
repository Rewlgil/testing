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
#include "time.h"
#include <JPEGDecoder.h>
#include "jpeg1.h"
#include "jpeg2.h"

IPAddress mqtt_server(161, 200, 80, 206);
#define current_version 1.00
#define versionINF "https://raw.githubusercontent.com/Rewlgil/testing/master/text.txt"

#define numreading 500
#define set_screen_interval 60000 //(1 * 60 * 1000)
#define send_interval 300000      //(5 * 60 * 1000)

SHTC3 tempSensor(Wire);
WiFiManager wifiManager;
TFT_eSPI tft = TFT_eSPI();
WiFiClient espClient;
PubSubClient client(espClient);

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
float Temp,Humid;
bool tempError = true;

struct average_data {
  uint32_t PM_10, PM_25, PM_100;
  uint32_t Humid;
  float Temp;
};
struct average_data avg;

template<class T>
class MovingAverage
{
  public:
    void giveAVGValue(T getdata);
    void getAVGValue(T *avg_value);
  private:
    T value[numreading] = {0};
    T sum = 0;
    uint16_t index = 0;
    boolean first_time = true;
};

MovingAverage<uint32_t> pm10;
MovingAverage<uint32_t> pm25;
MovingAverage<uint32_t> pm100;
MovingAverage<float> temp;
MovingAverage<uint32_t> humid;

boolean readPMSdata(void);

// MQTT
char topic[22] = "pmsensor/";
char doc_char[130];
String mac;
char mac_array[13];
// NTP time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7*3600;
const int   daylightOffset_sec = 0;

uint32_t start_time , last_set_screen, last_send_time;
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
  tft.fillRect(0, 20, 135, 90, tft.color565(0, 50, 100)); // Blue
  tft.fillTriangle(135, 20, 135, 110, 200, 110, tft.color565(0, 50, 100)); // Blue
  tft.fillRect(0, 110, 135, 90, tft.color565(255, 0, 255)); // Pink
  tft.fillTriangle(135, 200, 135, 110, 200, 110, tft.color565(255, 0, 255)); // Pink
  tft.fillCircle(240, 110, 100, tft.color565(200,   0,  0)); // Red
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.loadFont("DS-Digital-BoldItalic-50");
  tft.drawString("PM 2.5", 240, 60);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("C", 170, 275);
  tft.drawString("%", 345, 275);
  tft.setTextDatum(TL_DATUM);
  tft.loadFont("DS-Digital-Italic-40");
  tft.drawString("PM 1", 5, 25);
  tft.drawString("PM 10", 5, 115);
  tft.setTextDatum(BR_DATUM);
  tft.loadFont("Calibri-Italic-40");
  tft.drawString("ug/m", 320, 220);
  tft.setTextDatum(BL_DATUM);
  tft.loadFont("Calibri-Italic-28");
  tft.drawString("3", 320, 200);
  tft.unloadFont();
  drawArrayJpeg(thermometer, sizeof(thermometer),   5, 251);
  drawArrayJpeg(humidity,    sizeof(humidity),    180, 251);
  //set footers tab
  tft.fillRect(0, 300, 480, 20, TFT_LIGHTGREY);
  tft.setTextColor(TFT_BLUE);   tft.setFreeFont(FS9); tft.setTextDatum(TL_DATUM);
  tft.drawString("MAC: " + mac, 5, 305, GFXFF);
  tft.drawString("Firmware: ", 192, 305, GFXFF);  tft.drawFloat(current_version, 2, 267, 305, GFXFF);
  tft.drawString("Wi-Fi: ",  308, 305, GFXFF);
  start_time = millis();
  last_set_screen = millis();
  last_send_time = millis();
  setTime();
}

void loop() {
  setTime();
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
  
  if (((millis() - start_time) % 1000) == 0) {
    tempSensor.begin(true);
    tempSensor.sample();
    Temp = tempSensor.readTempC();
    Humid = tempSensor.readHumidity();
    if (Temp >= 0 && Humid >= 0){
      tempError = false;
      temp.giveAVGValue(Temp);
      humid.giveAVGValue(Humid);
//      Serial.printf("Temp = %.2f C\tHumid = %.2f %%\n", Temp, Humid);
    }
    else{
      tempError = true;
//      Serial.println("error to read Temp and Humid")
    }
  }
  
  if ((millis() - last_set_screen) >= set_screen_interval){
    getAllAVG();
    setScreen();
    last_set_screen = millis();
  }

  if ((millis() - last_send_time) >= send_interval){
    getAllAVG();
    //send data to server
    StaticJsonDocument<150> doc;
    doc["id"]   = mac_array;
    doc["pm1"]  = avg.PM_10;
    doc["pm25"] = avg.PM_25;
    doc["pm10"] = avg.PM_100;
    doc["Temp"] = avg.Temp;
    doc["Humid"] = avg.Humid;
    Serial.print("\n");
    serializeJsonPretty(doc, Serial);
    Serial.print("\n");
    serializeJsonPretty(doc, doc_char);
    client.publish(topic, doc_char);
    last_send_time = millis();
  }
  client.loop();
}

void getAllAVG(void)
{
  pm10.getAVGValue(&avg.PM_10);
  pm25.getAVGValue(&avg.PM_25);
  pm100.getAVGValue(&avg.PM_100);
  temp.getAVGValue(&avg.Temp);
  humid.getAVGValue(&avg.Humid);
  avg.Temp = round(avg.Temp * 10) / 10;
  avg.Humid = round(avg.Humid);
}

void configModeCallback (WiFiManager *mywifiManager){
  tft.println("Starting AP...");
  tft.print("AP name: ");
  tft.println(mywifiManager -> getConfigPortalSSID());
}

void setScreen(void){
  static uint16_t PM25_color = 0;
  if (avg.PM_25 < 30) PM25_color = tft.color565(0, 100, 0); //Green
  else if (avg.PM_25 >= 30 && avg.PM_25 < 50) PM25_color = tft.color565(200, 100, 0); //Orange
  else if (avg.PM_25 >= 50) PM25_color = tft.color565(200, 0, 0); //Red
  tft.fillCircle(240, 110, 100, PM25_color);         // PM2.5
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.loadFont("DS-Digital-BoldItalic-50");
  tft.drawString("PM 2.5", 240, 60);
  tft.loadFont("DS-Digital-Bold-100");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, PM25_color);
  tft.drawString("        ", 240, 140);
  tft.drawNumber(avg.PM_25, 240, 140);
  tft.loadFont("DS-Digital-BoldItalic-50");          // PM1.0 && PM10
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, tft.color565(0, 50, 100));
  tft.drawString("         ", 60, 68);
  tft.drawNumber(avg.PM_10,  60,  68);
  tft.setTextColor(TFT_WHITE, tft.color565(255, 0, 255));
  tft.drawString("        ", 60, 158);
  tft.drawNumber(avg.PM_100, 60, 158);
  tft.setTextDatum(ML_DATUM);                        //  Temp && Humid
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("            ", 40,  275, GFXFF);
  tft.drawString("          ", 220, 275, GFXFF);
  if (!tempError){
    tft.drawFloat(avg.Temp, 1, 45,  275);
    tft.drawNumber(avg.Humid, 230, 275);
  }
  else{
    tft.drawString("ERR   ", 40,  275, GFXFF);
    tft.drawString("ERR  ", 220, 275, GFXFF);
  }
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.loadFont("Calibri-Italic-40");
  tft.drawString("ug/m", 320, 220);
  tft.setTextDatum(BL_DATUM);
  tft.loadFont("Calibri-Italic-28");
  tft.drawString("3", 320, 200);
  tft.unloadFont();
}

void setTime(void){
  struct tm timeinfo;
  static char timeHour[3];    static char lastHour[3] = "88";
  static char timeMin[3];     static char lastMin[3] = "88";
  static char timeSec[3];     static char lastSec[3] = "88";
  static bool first_time = true;
  
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  strftime(timeHour,3, "%H", &timeinfo);
  strftime(timeMin, 3, "%M", &timeinfo);
  strftime(timeSec, 3, "%S", &timeinfo);
  if (first_time){
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.loadFont("DS-Digital-Italic-40");
    tft.setTextDatum(TL_DATUM);
    tft.drawString(" s", 440, 210);
    tft.drawString(" m", 440, 130);
    tft.drawString(" h",  440,  50);
    first_time = false;
  }
  if (lastSec[0] != timeSec[0] || lastSec[1] != timeSec[1])
  { 
    tft.loadFont("DS-Digital-Bold-70");
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_BLACK);  tft.drawString((String)lastSec, 440, 190);
    tft.setTextColor(TFT_WHITE);  tft.drawString((String)timeSec, 440, 190);
    for (uint8_t i = 0; i < 3; i++) lastSec[i] = timeSec[i];
    if (lastMin[0] != timeMin[0] || lastMin[1] != timeMin[1])
    {
      tft.setTextColor(TFT_BLACK);  tft.drawString((String)lastMin, 440, 110);
      tft.setTextColor(TFT_WHITE);  tft.drawString((String)timeMin, 440, 110);
      for (uint8_t i = 0; i < 3; i++) lastMin[i] = timeMin[i];
      if (lastHour[0] != timeHour[0] || lastHour[1] != timeHour[1])
      {
        tft.setTextColor(TFT_BLACK);  tft.drawString((String)lastHour, 440, 30);
        tft.setTextColor(TFT_WHITE);  tft.drawString((String)timeHour, 440, 30);
        for (uint8_t i = 0; i < 3; i++) lastHour[i] = timeHour[i];
      }
    }
  }
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
    else                  {tft.setTextColor(TFT_RED,       TFT_LIGHTGREY); tft.drawString("LOSS                   ", 360, 305, GFXFF);} tft.setTextColor(TFT_BLUE);
    last_connection_state = connection_state;
  }
}
