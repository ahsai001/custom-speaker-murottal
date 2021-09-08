#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <SD.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DMD32.h>
#include "fonts/SystemFont5x7.h"
#include "fonts/Arial_black_16.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"



//=========================================================================
//============================= Global Declarations =======================
//=========================================================================


//section code : DMD, toggle led, wifi alive, web server, Clock, JWS
TaskHandle_t taskLEDHandle;
TaskHandle_t taskWebHandle;
TaskHandle_t taskKeepWiFiHandle;
TaskHandle_t taskDMDHandle;
TaskHandle_t taskClockHandle;
TaskHandle_t taskJWSHandle;


int h24 = 12; //hours in 24 format
int h = 12; // hours in 12 format
int m = 0; //minutes
int s = 0; //seconds
char str_clock_full[9]; //used by dmd task
char str_clock[6]; //used by dmd task
char timeDay[3];
char timeMonth[10];
char timeYear[5];
int day;
int month;
int year;
bool isWiFiReady = false;
bool isClockReady = false;
bool isDMDReady = false;
bool isJWSReady = false;
bool isCountdownJWSReady = false;

const uint8_t built_in_led = 2;
const uint8_t relay = 26;


char data_jadwal_subuh[9];
char data_jadwal_dzuhur[9];
char data_jadwal_ashar[9];
char data_jadwal_maghrib[9];
char data_jadwal_isya[9];

char type_jws[8]; //subuh, dzuhur, ashar, maghrib, isya
char count_down_jws[9]; //04:30:00

//22.30 - 23.45 : 1 jam + 15 menit
//22.30 - 23.15 : 1 jam + -15 menit
//22.30 - 22.45 : 0 jam + 15 menit
//22.30 - 22.15 : 0 jam + -15 menit + 24 jam
//22.30 - 01.45 : -21 jam + 15 menit + 24 jam
//22.30 - 01.15 : -21 jam + -15 menit + 24 jam

uint32_t sDistanceFromNowToTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
    int64_t deltaInSecond = ((hours-h24)*3600) + ((minutes-m)*60) + seconds-s;
    if(deltaInSecond <= 0){
      deltaInSecond += 24*3600;
    }
    return (uint32_t)deltaInSecond;
}

uint32_t sDistanceFromTimeToTime(uint8_t fhours, uint8_t fminutes, uint8_t fseconds, uint8_t thours, uint8_t tminutes, uint8_t tseconds){
    int64_t deltaInSecond = ((thours-fhours)*3600) + ((tminutes-fminutes)*60) + (tseconds-fseconds);
    if(deltaInSecond <= 0){
      deltaInSecond += 24*3600;
    }
    return (uint32_t)deltaInSecond;
}

void delayUntilAtTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
    uint32_t delta = sDistanceFromNowToTime(hours,minutes, seconds)*1000;
    Serial.println(delta);
    delay(delta);
}

//=========================================================================
//==================================   Task DMD  ==========================
//=========================================================================
#define DISPLAYS_ACROSS 2
#define DISPLAYS_DOWN 1
#define DMD_DATA_SIZE 20

struct DMD_Data{
  int type = 0; //1:jam, 2:jws, 3:scrollingtext, 4:countdown
  const char * text1;
  const char * text2;
  const uint8_t *font;
  unsigned long delay = 0; //delay refresh dalam setiap kemunculan
  unsigned long duration = 0; //durasi setiap kemunculan
  int max_count = 1; //jumlah kemunculan
  int count = 0; 
};
int dmd_loop_index = 0; //we can change this runtime
struct DMD_Data dmd_data_list[DMD_DATA_SIZE]; //index 0 - 5 for important message
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

hw_timer_t *timer = NULL;
String scrollingText = "Assalamu'alaikum";

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
}


void marqueeText(const uint8_t *font, const char * text, int top){
  dmd.selectFont(font);
  dmd.drawMarquee(text, strlen(text), (32 * DISPLAYS_ACROSS) - 1, top);
  long start = millis();
  long timer = start;
  boolean ret = false;
  while (!ret)
  {
    if ((timer + 30) < millis())
    {
      ret = dmd.stepMarquee(-1, 0);
      timer = millis();
    }
  }
}

void setupDMDdata(uint8_t index, uint8_t type, const char * text1, const char * text2, const uint8_t * font, unsigned long delay, unsigned long duration, int max_count){
  dmd_data_list[index].type = type;
  dmd_data_list[index].text1 = text1;
  dmd_data_list[index].text2 = text2;
  dmd_data_list[index].font = font;
  dmd_data_list[index].delay = delay;
  dmd_data_list[index].duration = duration;
  dmd_data_list[index].max_count = max_count;
}

void setupDMD()
{
  uint8_t cpuClock = ESP.getCpuFreqMHz();
  timer = timerBegin(0, cpuClock, true);
  timerAttachInterrupt(timer, &triggerScan, false);
  timerAlarmWrite(timer, 300, true);
  timerAlarmEnable(timer);

  //control brightness DMD
  ledcSetup(0, 5000, 8);
  ledcAttachPin(4, 0);
  ledcWrite(0, 20);

  dmd.clearScreen(true);
  marqueeText(Arial_Black_16, scrollingText.c_str(),1);
  dmd.clearScreen(true);
  marqueeText(Arial_Black_16, "Developed by AhsaiLabs", 1);

  setupDMDdata(5,1,str_clock_full, "",System5x7,1000,15000,-1);
  setupDMDdata(6,3,"Kejarlah Akhirat dan Jangan Lupakan Dunia", "",Arial_Black_16,1000,10000,-1);
  setupDMDdata(7,2,count_down_jws, type_jws,System5x7,1000,10000,-1);
  setupDMDdata(8,3,"Bertakwa dan bertawakal lah hanya kepada Allah", "",Arial_Black_16,1000,10000,-1);
  //setupDMDdata(9,2,count_down_jws, type_jws,System5x7,1000,10000,-1);
  setupDMDdata(10,3,"Utamakan sholat dalam keseharianmu", "",Arial_Black_16,1000,10000,-1);

  Serial.println("DMD is coming");
}

unsigned int stringWidth(const uint8_t *font, const char * str){
  unsigned int width = 0;
  char c;
  int idx;
  dmd.selectFont(font);
  for(idx = 0; c = str[idx], c != 0; idx++) {
    int cwidth = dmd.charWidth(c);
    if(cwidth > 0)
      width += cwidth + 1;
  }
  if(width) {
    width--;
  }
  return width;
}

void drawTextCenter(const uint8_t * font, const char * str, int top){
    unsigned int length = stringWidth(font, str);
    float posX = ((32 * DISPLAYS_ACROSS) - length)/2;
    dmd.drawString(posX, top, str, strlen(str), GRAPHICS_NORMAL);
}



void taskDMD(void *parameter)
{
  setupDMD();
  for (;;)
  {
    //byte b;
    // 10 x 14 font clock, including demo of OR and NOR modes for pixels so that the flashing colon can be overlayed
    //dmd.drawBox(0, 0, (32 * DISPLAYS_ACROSS) - 1, (16 * DISPLAYS_DOWN) - 1, GRAPHICS_TOGGLE);

    for(dmd_loop_index=0;dmd_loop_index<DMD_DATA_SIZE;dmd_loop_index++){
      DMD_Data item = dmd_data_list[dmd_loop_index];
      ++item.count;
      unsigned long start  = millis();
      dmd.clearScreen(true);
      while(start + item.duration > millis()){
        switch (item.type)
        {
        case 1: //jam
          drawTextCenter(item.font, item.text1, 5);
          break;
        case 2: //jws
          drawTextCenter(item.font, item.text1, 1);
          Serial.println(item.text2);
          //drawTextCenter(item.font, item.text2, 9);
          break;
        case 3: //scrolling text
          marqueeText(item.font, item.text1, 1);
          break;
        case 4: //count down timer
          {
            int counter = item.duration/1000;
            int leftSeconds = counter;
            int hours = leftSeconds/3600;
            int minutes = 0;
            int seconds = 0;
            if(hours > 0){
              leftSeconds = leftSeconds % 3600;
            }
            minutes = leftSeconds/60;
            if(minutes > 0){
              leftSeconds = leftSeconds % 60;
            }
            seconds = leftSeconds;

            dmd.selectFont(item.font);
            
            long start = millis();
            long timer = start;
            int width = stringWidth(item.font,item.text1);
            int posx = (32*DISPLAYS_ACROSS) - 1;
            while(counter >= 0){
              if (millis() - start > item.delay){
                if(seconds==-1){
                  seconds=59;
                  minutes--;
                }
                if(minutes==-1){
                  minutes=59;
                  hours--;
                }
                //display
                char count_down[9];
                sprintf_P(count_down, (PGM_P)F("%02d:%02d:%02d"), hours, minutes, seconds);
                drawTextCenter(item.font, count_down, 1);
                seconds--;
                counter--;
                start = millis();
              }

              if (millis() - timer > 30){
                dmd.drawString(--posx, 9, item.text1, strlen(item.text1), GRAPHICS_NORMAL);
                if(posx < (-1*width)){
                  posx = (32*DISPLAYS_ACROSS) - 1;
                }
                timer = millis();
              }
            }
          }
          break;
        default:
          break;
        }
        delay(item.delay);
      }
      if(item.max_count > 0 && item.count >= item.max_count){
        //reset struct to stop drawing in dmd
        (*(dmd_data_list+dmd_loop_index)).count = 0;
        (*(dmd_data_list+dmd_loop_index)).max_count = 1;
        (*(dmd_data_list+dmd_loop_index)).delay = 0;
        (*(dmd_data_list+dmd_loop_index)).duration = 0;
        (*(dmd_data_list+dmd_loop_index)).type = 0;
      }
    }
    

    /*
    dmd.drawChar(0, 3, '2', GRAPHICS_NORMAL);
    dmd.drawChar(7, 3, '3', GRAPHICS_NORMAL);
    dmd.drawChar(17, 3, '4', GRAPHICS_NORMAL);
    dmd.drawChar(25, 3, '5', GRAPHICS_NORMAL);
    dmd.drawChar(15, 3, ':', GRAPHICS_OR); // clock colon overlay on
    delay(1000);
    dmd.drawChar(15, 3, ':', GRAPHICS_NOR); // clock colon overlay off
    delay(1000);
    dmd.drawChar(15, 3, ':', GRAPHICS_OR); // clock colon overlay on
    delay(1000);
    dmd.drawChar(15, 3, ':', GRAPHICS_NOR); // clock colon overlay off
    delay(1000);
    dmd.drawChar(15, 3, ':', GRAPHICS_OR); // clock colon overlay on
    delay(1000);*/

    

    // half the pixels on
    //dmd.drawTestPattern(PATTERN_ALT_0);
    //delay(1000);

    // the other half on
    //dmd.drawTestPattern(PATTERN_ALT_1);
    //delay(1000);

    // display some text
    // dmd.clearScreen(true);
    // dmd.selectFont(System5x7);
    // for (byte x = 0; x < DISPLAYS_ACROSS; x++)
    // {
    //   for (byte y = 0; y < DISPLAYS_DOWN; y++)
    //   {
    //     dmd.drawString(2 + (32 * x), 1 + (16 * y), "freet", 5, GRAPHICS_NORMAL);
    //     dmd.drawString(2 + (32 * x), 9 + (16 * y), "ronic", 5, GRAPHICS_NORMAL);
    //   }
    // }
    // delay(2000);

    // draw a border rectangle around the outside of the display
    // dmd.clearScreen(true);
    // dmd.drawBox(0, 0, (32 * DISPLAYS_ACROSS) - 1, (16 * DISPLAYS_DOWN) - 1, GRAPHICS_NORMAL);
    // delay(1000);

    // for (byte y = 0; y < DISPLAYS_DOWN; y++)
    // {
    //   for (byte x = 0; x < DISPLAYS_ACROSS; x++)
    //   {
    //     // draw an X
    //     int ix = 32 * x;
    //     int iy = 16 * y;
    //     dmd.drawLine(0 + ix, 0 + iy, 11 + ix, 15 + iy, GRAPHICS_NORMAL);
    //     dmd.drawLine(0 + ix, 15 + iy, 11 + ix, 0 + iy, GRAPHICS_NORMAL);
    //     delay(1000);

    //     // draw a circle
    //     dmd.drawCircle(16 + ix, 8 + iy, 5, GRAPHICS_NORMAL);
    //     delay(1000);

    //     // draw a filled box
    //     dmd.drawFilledBox(24 + ix, 3 + iy, 29 + ix, 13 + iy, GRAPHICS_NORMAL);
    //     delay(1000);
    //   }
    // }

    // // stripe chaser
    // for (b = 0; b < 20; b++)
    // {
    //   dmd.drawTestPattern((b & 1) + PATTERN_STRIPE_0);
    //   delay(200);
    // }
    // delay(200);
  }
}



//================================================================================
//==================================   Task Toggle LED  ==========================
//================================================================================
uint32_t led_on_delay = 500;
uint32_t led_off_delay = 500;

void taskToggleLED(void *parameter)
{
  for (;;)
  {
    digitalWrite(built_in_led, HIGH);
    delay(led_on_delay);
    digitalWrite(built_in_led, LOW);
    delay(led_off_delay);
  }
}


void startTaskToggleLED()
{
  xTaskCreate(
      taskToggleLED, // Function that should be called
      "Toggle LED",  // Name of the task (for debugging)
      1000,          // Stack size (bytes)
      NULL,          // Parameter to pass
      1,             // Task priority
      &taskLEDHandle // Task handle
  );
}

void stopTaskToggleLED()
{
  vTaskDelete(taskLEDHandle);
  digitalWrite(built_in_led, HIGH);
}



//=====================================================================================
//==================================   Task Keep WiFi Alive  ==========================
//=====================================================================================
const char *ssid = "3mbd3vk1d-2";
const char *password = "marsupiarmadomah3716";
#define WIFI_TIMEOUT_MS 20000      // 20 second WiFi connection timeout
#define WIFI_RECOVER_TIME_MS 30000 // Wait 30 seconds after a failed connection attempt

void taskKeepWiFiAlive(void *parameter)
{
  for (;;)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      delay(10000);
      continue;
    }
    isWiFiReady = false;
    startTaskToggleLED();

    Serial.println("[WIFI] Connecting");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long startAttemptTime = millis();

    // Keep looping while we're not connected and haven't reached the timeout
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttemptTime < WIFI_TIMEOUT_MS)
    {
    }

    // When we couldn't make a WiFi connection (or the timeout expired)
    // sleep for a while and then retry.
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("[WIFI] FAILED");
      stopTaskToggleLED();
      delay(WIFI_RECOVER_TIME_MS);
      continue;
    }

    isWiFiReady = true;
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("speaker-murottal"))
    {
      Serial.println("speaker-murottal.local is available");
    }
    stopTaskToggleLED();
  }
}



//================================================================================
//==================================   Task Web Server  ==========================
//================================================================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Custom Speaker Murottal Setting</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
    body {
      text-align: center;
      font-family: "Trebuchet MS", Arial;
      margin-left:auto;
      margin-right:auto;
    }
    .slider {
      width: 300px;
    }
  </style>
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.3.1/jquery.min.js"></script>
  </head><body>
  <form action="/get-setting">
    Scrolling Text: <input type="text" name="scrolltext">
    <input type="submit" value="Submit">
  </form><br>
  <form action="/get-setting">
    input2: <input type="text" name="input2">
    <input type="submit" value="Submit">
  </form><br>
  <form action="/get-setting">
    input3: <input type="text" name="input3">
    <input type="submit" value="Submit">
  </form>
  <p>Brightness: <span id="brightnessPos"></span> %</p>
  <input type="range" min="0" max="255" value="20" class="slider" id="brightnessSlider" onchange="brightnessChange(this.value)"/>
  <script>
    $.ajaxSetup({timeout:1000});

    var slider = document.getElementById("brightnessSlider");
    var brightnessP = document.getElementById("brightnessPos");
    brightnessP.innerHTML = Math.round((slider.value/255)*100);
    $.get("/brightness?level=" + slider.value);

    slider.oninput = function() {
      slider.value = this.value;
      brightnessP.innerHTML = Math.round((this.value/255)*100);
    }
    function brightnessChange(pos) {
      $.get("/brightness?level=" + pos);
    }
  </script>
</body></html>)rawliteral";

WebServer server(80);

void handleWebRoot()
{
  digitalWrite(built_in_led, 0);
  server.send(200, "text/plain", "hello world");
  digitalWrite(built_in_led, 1);
}

void handleWebNotFound()
{
  digitalWrite(built_in_led, 0);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(built_in_led, 1);
}

void handleServerClient()
{
  server.handleClient();
  delay(1000);
}

void taskWebServer(void *parameter)
{
  server.on("/", handleWebRoot);

  server.on("/check", []()
            { server.send(200, "text/plain", "this works as well"); });

  server.on("/setting", []()
            { server.send(200, "text/html", index_html); });

  server.on("/get-setting", []()
            {
              String scrolltext = server.arg("scrolltext");
              scrollingText = scrolltext;
              server.sendHeader("Location", "/setting", true);
              server.send(302, "text/plain", "");
            });

  server.on("/brightness", []()
            {
              String level = server.arg("level");
              ledcWrite(0, level.toInt());
              server.send(404, "text/plain", "ubah brigtness berhasil");
            });

  server.on("/on", []()
            {
              //digitalWrite(built_in_led, 1);
              //digitalWrite(relay, 1);
              server.send(200, "text/plain", "relay on");
            });

  server.on("/off", []()
            {
              //digitalWrite(built_in_led, 0);
              //digitalWrite(relay, 0);
              server.send(200, "text/plain", "relay off");
            });

  server.onNotFound(handleWebNotFound);

  server.begin();
  Serial.println("HTTP server started");

  for (;;)
  {
    handleServerClient();
  }
}




//===========================================================================
//==================================   Task Clock  ==========================
//===========================================================================
//const char * ntpServer = "pool.ntp.org";
const char * ntpServer = "time.google.com";
const uint8_t timezone = 7; //jakarta GMT+7
const long  gmtOffset_sec = timezone*3600; //in seconds
const int   daylightOffset_sec = 0;

void taskClock(void * parameter)
{
  while (!isWiFiReady)
  {
    Serial.println("Task clock waiting for wifi...");
    delay(5000);
  }
  
  // Init and get the time
  isClockReady = false;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  while(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    delay(2000);
  }

  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  strftime(timeDay,3, "%d", &timeinfo);
  strftime(timeMonth,10, "%B", &timeinfo);
  strftime(timeYear,5, "%Y", &timeinfo);
  day = timeinfo.tm_mday;
  month = timeinfo.tm_mon+1;
  year = timeinfo.tm_year+1900;

  String type="AM";

  h24 = timeinfo.tm_hour; // 24 hours
  h = timeinfo.tm_hour > 12 ? timeinfo.tm_hour-12 : timeinfo.tm_hour;
  m = timeinfo.tm_min;
  s = timeinfo.tm_sec;

  int state1 = 1;
  int state2 = 1;

  for (;;)
  {
    s = s + 1;
    //show clock
    if (h24 < 12)
      type = "AM";
    if (h24 == 12)
      type = "PM";
    if (h24 > 12)
      type = "PM";
    if (h24 == 24)
      h24 = 0;
    
    delay(1000);
  
    if (s == 60)
    {
      s = 0;
      m = m + 1;
    }
    if (m == 60)
    {
      m = 0;
      h = h + 1;
      h24 = h24 + 1;
    }
    if (h == 13)
    {
      h = 1;
    }

    if (h <= 12 && h24 < 12)
    {
      //Serial.println("Selamat Pagi ;)");
    }
    if ((h == 12 || h == 1 || h == 2 || h == 3) && h24 >= 12)
    {
      //Serial.println("Selamat siang :)");
    }
    if ((h == 4 || h == 5 || h == 6 || h == 7 || h == 8) && h24 > 12)
    {
      //Serial.println("Selamat sore :)");
    }
    if (h >= 9 && h24 > 12)
    {
      //Serial.println("Selamat malam :)");
    }

    // state1 = touchRead(33) > 20;
    // if (state1 == 0)
    // {
    //   h = h + 1;
    //   h24 = h24 + 1;
    //   if (h24 < 12)
    //     type = "AM";
    //   if (h24 == 12)
    //     type = "PM";
    //   if (h24 > 12)
    //     type = "PM";
    //   if (h24 == 24)
    //     h24 = 0;
    //   if (h == 13)
    //     h = 1;
    // }
    // state2 = touchRead(32) > 20;
    // if (state2 == 0)
    // {
    //   s = 0;
    //   m = m + 1;
    // }

    
    // Serial.print("Time : ");
    // Serial.print(timeinfo.tm_hour);
    // Serial.print(":");
    // Serial.print(m);
    // Serial.print(":");
    // Serial.println(s);
    sprintf_P(str_clock_full, (PGM_P)F("%02d:%02d:%02d"), h24, m, s);
    sprintf_P(str_clock, (PGM_P)F("%02d:%02d"), h24, m);
    isClockReady = true;
    //Serial.println(str_clock);
  }
}


//=========================================================================
//==================================  Task Jadwal Sholat =================
//=========================================================================
void taskJadwalSholat(void * parameter){
  for(;;){
    if(!isClockReady){
      Serial.println("Task JWS waiting for clock...");
      delay(3000);
      continue;
    }
    char link[100];
    sprintf_P(link, (PGM_P)F("https://api.myquran.com/v1/sholat/jadwal/1301/%s/%d/%s"), timeYear, month,timeDay);
    Serial.println(link);
    
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();

    // Your Domain name with URL path or IP address with path
    http.begin(client, link);
    
    // Send HTTP POST request
    int httpResponseCode = http.GET();
    
    if (httpResponseCode>0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
    }
    else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }

    String jsonData = http.getString();

    // Free resources
    http.end();

    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      delay(4000);
      isJWSReady = false;
      continue;
    }

    JsonObject data_jadwal = doc["data"]["jadwal"];

    sprintf_P(data_jadwal_subuh, (PGM_P)F("%s:00"), data_jadwal["subuh"].as<const char*>());// "04:37"
    sprintf_P(data_jadwal_dzuhur, (PGM_P)F("%s:00"), data_jadwal["dzuhur"].as<const char*>());
    sprintf_P(data_jadwal_ashar, (PGM_P)F("%s:00"), data_jadwal["ashar"].as<const char*>());
    sprintf_P(data_jadwal_maghrib, (PGM_P)F("%s:00"), data_jadwal["maghrib"].as<const char*>());
    sprintf_P(data_jadwal_isya, (PGM_P)F("%s:00"), data_jadwal["isya"].as<const char*>());
    
    Serial.print("Subuh : ");
    Serial.println(data_jadwal_subuh);
    Serial.print("Dzuhur : ");
    Serial.println(data_jadwal_dzuhur);
    Serial.print("Ashar : ");
    Serial.println(data_jadwal_ashar);
    Serial.print("Magrib : ");
    Serial.println(data_jadwal_maghrib);   
    Serial.print("Isya : ");
    Serial.println(data_jadwal_isya); 

    doc.clear();
    isJWSReady = true;
    delayUntilAtTime(1,12,0);
  }
}

std::array<float, 4>  getArrayOfTime(char * time){
  Serial.print("getArrayOfTime : ");
  Serial.print(time);
  const char delimiter[2] = ":";
  char * token = strtok(time, delimiter);
  std::array<float, 4> as;
  int index = 0;
  while( token != NULL ) {
      as[index] = atoi(token);
      index++;
      token = strtok(NULL, delimiter);
  }
  as[3] = (as[0]*3600)+(as[1]*60)+as[2];
  Serial.print("=>");
  Serial.print(as[0]);
  Serial.print("-");
  Serial.print(as[1]);
  Serial.print("-");
  Serial.print(as[2]);
  Serial.print("-");
  Serial.println(as[3]);
  return as;
}

void taskCountDownJWS(void * parameter){
  for(;;){
    if(!isJWSReady){
      Serial.println("Task countdown-jws waiting for jws...");
      isCountdownJWSReady = false;
      delay(3000);
      continue;
    }
    std::array<float,4> clock = getArrayOfTime(str_clock_full);
    std::array<float,4> subuh = getArrayOfTime(data_jadwal_subuh);
    std::array<float,4> dzuhur = getArrayOfTime(data_jadwal_dzuhur);
    std::array<float,4> ashar = getArrayOfTime(data_jadwal_ashar);
    std::array<float,4> maghrib = getArrayOfTime(data_jadwal_maghrib);
    std::array<float,4> isya = getArrayOfTime(data_jadwal_isya);
    
    int counter = 0;

    memset(type_jws,0,sizeof(type_jws));

    if(clock[3] <= subuh[3] || clock[3] > isya[3]){
      sprintf_P(type_jws, (PGM_P)F("subuh"));
      counter = sDistanceFromTimeToTime(clock[0],clock[1],clock[2],subuh[0],subuh[1],subuh[2]);
    } else if(clock[3] <= dzuhur[3]){
      sprintf_P(type_jws, (PGM_P)F("dzuhur"));
      counter = dzuhur[3] - clock[3];
    } else if(clock[3] <= ashar[3]){
      sprintf_P(type_jws, (PGM_P)F("ashar"));
      counter = ashar[3] - clock[3];
    } else if(clock[3] <= maghrib[3]){
      sprintf_P(type_jws, (PGM_P)F("maghrib"));
      counter = maghrib[3] - clock[3];
    } else if(clock[3] <= isya[3]){
      sprintf_P(type_jws, (PGM_P)F("isya"));
      counter = isya[3] - clock[3];
    }
    
    Serial.print("Counter Countdown for ");
    Serial.print(type_jws);
    Serial.print(" : ");
    Serial.println(counter);

    int leftSeconds = counter;
    int hours = leftSeconds/3600;
    int minutes = 0;
    int seconds = 0;
    if(hours > 0){
      leftSeconds = leftSeconds % 3600;
    }
    minutes = leftSeconds/60;
    if(minutes > 0){
      leftSeconds = leftSeconds % 60;
    }
    seconds = leftSeconds;

    while(counter >= 0){
      if(seconds==-1){
        seconds=59;
        minutes--;
      }
      if(minutes==-1){
        minutes=59;
        hours--;
      }
      //display
      sprintf_P(count_down_jws, (PGM_P)F("%02d:%02d:%02d"), hours, minutes, seconds);
      isCountdownJWSReady = true;
      Serial.print("String Countdown : ");
      Serial.print(type_jws);
      Serial.print(" : ");
      Serial.println(count_down_jws);
      seconds--;
      counter--;
      delay(1000);
    }

    //show alert
    char count_down_alert[30] = {0};
    sprintf_P(count_down_alert, (PGM_P)F("Waktunya sholat %s"), type_jws);
    setupDMDdata(1,4,count_down_alert,"",System5x7,1000,5*60*1000/*5 menit*/,1);
    delay(5000);
  }
}

//=========================================================================
//==================================   Main App  ==========================
//=========================================================================
void setup()
{
  pinMode(built_in_led, OUTPUT);
  Serial.begin(115200);

  xTaskCreatePinnedToCore(
      taskKeepWiFiAlive,  // Function that should be called
      "Keep WiFi Alive",  // Name of the task (for debugging)
      5000,               // Stack size (bytes)
      NULL,               // Parameter to pass
      1,                  // Task priority
      &taskKeepWiFiHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE
  );

  delay(5000);
  xTaskCreatePinnedToCore(
      taskClock,  // Function that should be called
      "Clock",   // Name of the task (for debugging)
      5000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskClockHandle, // Task handle
      0);

  delay(5000);
  xTaskCreatePinnedToCore(
      taskDMD,        // Function that should be called
      "Display DMD",  // Name of the task (for debugging)
      5000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskDMDHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);

  delay(5000);
  xTaskCreatePinnedToCore(
      taskWebServer,  // Function that should be called
      "Web Server",   // Name of the task (for debugging)
      5000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskWebHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);


  delay(5000);
  xTaskCreatePinnedToCore(
      taskJadwalSholat,  // Function that should be called
      "Jadwal Sholat",   // Name of the task (for debugging)
      10000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskJWSHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);

  delay(5000);
  xTaskCreatePinnedToCore(
      taskCountDownJWS,  // Function that should be called
      "Countdown Jadwal Sholat",   // Name of the task (for debugging)
      5000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskJWSHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);

  vTaskDelete(NULL);
}

void loop()
{
  //do nothing, everything is doing in task
}