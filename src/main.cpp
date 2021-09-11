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
#include <nvs_flash.h>
#include <Preferences.h>



//=========================================================================
//============================= Global Declarations =======================
//=========================================================================


//section code : DMD, toggle led, wifi alive, web server, Clock, JWS
TaskHandle_t taskLEDHandle;
TaskHandle_t taskWebHandle;
TaskHandle_t taskKeepWiFiHandle;
TaskHandle_t taskDMDHandle;
TaskHandle_t taskClockHandle;
TaskHandle_t taskDateHandle;
TaskHandle_t taskJWSHandle;
TaskHandle_t taskCountdownJWSHandle;
TaskHandle_t taskButtonTouchHandle;

static Preferences preferences;

int h24 = 12; //hours in 24 format
int h = 12; // hours in 12 format
int m = 0; //minutes
int s = 0; //seconds
static char str_clock_full[9] = "--:--:--"; //used by dmd task
static char str_date[26] = "Minggu, 10 September 2021"; //used by dmd task
static char str_hijri_date[30] = "10 jumadil akhir 1443";
static char str_date_full[55] = "";
// char timeDay[3];
// char timeMonth[10];
// char timeYear[5];
int day;
int month;
int year;
int weekday;
int hijri_day;
int hijri_month;
int hijri_year;


bool isWiFiReady = false;
bool isClockReady = false;
bool isDateReady = false;
bool isDMDReady = false;
bool isJWSReady = false;
bool isCountdownJWSReady = false;
bool isSPIFFSReady = false;

const uint8_t built_in_led = 2;
const uint8_t relay = 26;

char data_jadwal_subuh[9];
char data_jadwal_syuruk[9];
char data_jadwal_dhuha[9];
char data_jadwal_dzuhur[9];
char data_jadwal_ashar[9];
char data_jadwal_maghrib[9];
char data_jadwal_isya[9];

static char type_jws[8] = "sholat"; //subuh, dzuhur, ashar, maghrib, isya
static char count_down_jws[9] = "--:--:--"; //04:30:00

//22.30 - 23.45 : 1 jam + 15 menit
//22.30 - 23.15 : 1 jam + -15 menit
//22.30 - 22.45 : 0 jam + 15 menit
//22.30 - 22.15 : 0 jam + -15 menit + 24 jam
//22.30 - 01.45 : -21 jam + 15 menit + 24 jam
//22.30 - 01.15 : -21 jam + -15 menit + 24 jam

unsigned long sDistanceFromNowToTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
    signed long deltaInSecond = ((hours-h24)*3600) + ((minutes-m)*60) + seconds-s;
    if(deltaInSecond <= 0){
      deltaInSecond += 24*3600;
    }
    return (unsigned long)deltaInSecond;
}

unsigned long msDistanceFromNowToTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
  return sDistanceFromNowToTime(hours, minutes, seconds) * 1000;
}

unsigned long sDistanceFromTimeToTime(uint8_t fhours, uint8_t fminutes, uint8_t fseconds, uint8_t thours, uint8_t tminutes, uint8_t tseconds){
    signed long deltaInSecond = ((thours-fhours)*3600) + ((tminutes-fminutes)*60) + (tseconds-fseconds);
    if(deltaInSecond <= 0){
      deltaInSecond += 24*3600;
    }
    return (unsigned long)deltaInSecond;
}

void delayUntilAtTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
    delay(msDistanceFromNowToTime(hours, minutes, seconds));
}

void eraseNVS(){
  nvs_flash_erase(); // erase the NVS partition and...
  nvs_flash_init(); // initialize the NVS partition.
  ESP.restart();
  while(true);
}

std::array<unsigned long, 4>  getArrayOfTime(const char * time){
  //Serial.print("getArrayOfTime : ");
  //Serial.print(time);

  char copied_time[9] = {'\0'};
  sprintf_P(copied_time, (PGM_P)F("%s"), time);

  const char delimiter[2] = ":";
  char * token = strtok(copied_time, delimiter);
  std::array<unsigned long, 4> as;
  int index = 0;
  while( token != NULL ) {
      as[index] = atoi(token);
      index++;
      token = strtok(NULL, delimiter);
  }
  as[3] = (as[0]*3600)+(as[1]*60)+as[2];
  // Serial.print("=>");
  // Serial.print(as[0]);
  // Serial.print("-");
  // Serial.print(as[1]);
  // Serial.print("-");
  // Serial.print(as[2]);
  // Serial.print("-");
  // Serial.println(as[3]);
  return as;
}

//=========================================================================
//==================================   Task DMD  ==========================
//=========================================================================
#define DISPLAYS_ACROSS 2
#define DISPLAYS_DOWN 1
#define DMD_DATA_SIZE 20

enum DMDType {
  DMD_TYPE_STATIC_SCROLL, DMD_TYPE_STATIC_STATIC, DMD_TYPE_SCROLL, DMD_TYPE_COUNTDOWN_SCROLL, DMD_TYPE_COUNTUP_SCROLL
};

struct DMD_Data{
  int type = -1; //1:datetime, 2:jws, 3:scrollingtext, 4:countdown, 5:countup
  char * text1;
  bool need_free_text1 = false;
  char * text2;
  bool need_free_text2 = false;
  const uint8_t *font;
  unsigned long delay_inMS = 0; //delay refresh dalam setiap kemunculan
  unsigned long duration_inMS = 0; //durasi setiap kemunculan
  int max_count = 1; //jumlah kemunculan, -1 for unlimited
  int count = 0; //by code
  unsigned long life_time_inMS = 0; // in ms
  unsigned long start_time_inMS = 0; //by code
};
bool need_reset_dmd_loop_index = false;
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
void resetDMDLoopIndex(){ //use this function to make show important message right now
  need_reset_dmd_loop_index = true;
}

void setupDMDdata(uint8_t index, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int max_count, unsigned long life_time_inMS, unsigned long start_time_inMS){
  dmd_data_list[index].type = type;
  dmd_data_list[index].text1 = (char*)text1;
  dmd_data_list[index].need_free_text1 = need_free_text1;
  dmd_data_list[index].text2 = (char*)text2;
  dmd_data_list[index].need_free_text2 = need_free_text2;
  dmd_data_list[index].font = font;
  dmd_data_list[index].delay_inMS = delay_inMS;
  dmd_data_list[index].duration_inMS = duration_inMS;
  dmd_data_list[index].max_count = max_count;
  dmd_data_list[index].life_time_inMS = life_time_inMS;
  dmd_data_list[index].start_time_inMS = start_time_inMS;
}
void setupDMDdata(uint8_t index, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int max_count, unsigned long life_time_inMS, const char * exact_time /*09:10:23*/){
  std::array<unsigned long, 4> timeInfo = getArrayOfTime(exact_time);
  setupDMDdata(index,type,text1,need_free_text1,text2,need_free_text2,font,delay_inMS,duration_inMS,max_count,life_time_inMS,millis()+msDistanceFromNowToTime(timeInfo[0],timeInfo[1], timeInfo[2]));
}

void setupDMDdata(uint8_t index, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int max_count, unsigned long life_time_inMS){
  setupDMDdata(index,type,text1,need_free_text1,text2,need_free_text2,font,delay_inMS,duration_inMS,max_count,life_time_inMS,0.0);
}

void setupDMDdata(uint8_t index, DMDType type, const char * text1, const char * text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int max_count){
  setupDMDdata(index,type,text1,false,text2,false,font,delay_inMS,duration_inMS,max_count,0.0,0.0);
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

  setupDMDdata(5,DMD_TYPE_STATIC_SCROLL,str_date_full,str_clock_full, System5x7,1000,15000,-1);
  setupDMDdata(6,DMD_TYPE_SCROLL,"Kejarlah Akhirat dan Jangan Lupakan Dunia", "",Arial_Black_16,1000,10000,-1);
  setupDMDdata(7,DMD_TYPE_STATIC_STATIC,type_jws, count_down_jws,System5x7,1000,10000,-1);
  setupDMDdata(8,DMD_TYPE_SCROLL,(char*)"Bertakwa dan bertawakal lah hanya kepada Allah", "",Arial_Black_16,1000,10000,-1);
  setupDMDdata(9,DMD_TYPE_STATIC_STATIC,type_jws, count_down_jws,System5x7,1000,10000,-1);
  setupDMDdata(10,DMD_TYPE_SCROLL,(char*)"Utamakanlah sholat dan sabar", "",Arial_Black_16,1000,10000,-1);

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
      if(need_reset_dmd_loop_index){
        need_reset_dmd_loop_index = false;
        dmd_loop_index = 0;
        continue;
      }

      DMD_Data * item = dmd_data_list+dmd_loop_index;
      unsigned long start  = millis();

      if(item->start_time_inMS > 0 && start < item->start_time_inMS){
        continue;
      }


      ++item->count;
      dmd.clearScreen(true);

      while(start + item->duration_inMS > millis()){
        if(need_reset_dmd_loop_index){
          break;
        }

        switch (item->type)
        {
          case DMD_TYPE_STATIC_SCROLL:
            {
              int counter = item->duration_inMS/1000;
              unsigned long start = millis();
              unsigned long timer = start;
              dmd.selectFont(item->font);
              int width = stringWidth(item->font,item->text1);
              int posx = (32*DISPLAYS_ACROSS) - 1;
        
              while(counter >= 0){
                  if(need_reset_dmd_loop_index){
                    break;
                  }
                  if (millis() - start > item->delay_inMS){   
                    drawTextCenter(item->font, item->text2, 1);
                    start = millis();
                    counter--;
                  }
                  if (millis() - timer > 30){
                    dmd.drawString(--posx, 9, item->text1, strlen(item->text1), GRAPHICS_NORMAL);
                    if(posx < (-1*width)){
                      posx = (32*DISPLAYS_ACROSS) - 1;
                    }
                    timer = millis();
                  }
              }
            }
            break;
          case DMD_TYPE_STATIC_STATIC:
            drawTextCenter(item->font, item->text2, 1);
            drawTextCenter(item->font, item->text1, 9); 
            break;
          case DMD_TYPE_SCROLL: //single scrolling text
            marqueeText(item->font, item->text1, 1);
            break;
          case DMD_TYPE_COUNTDOWN_SCROLL: //count down timer
            {
              int counter = item->duration_inMS/1000;
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

              dmd.selectFont(item->font);
              
              unsigned long start = millis();
              unsigned long timer = start;
              int width = stringWidth(item->font,item->text1);
              int posx = (32*DISPLAYS_ACROSS) - 1;
              while(counter >= 0){
                if(need_reset_dmd_loop_index){
                  break;
                }

                if (millis() - start > item->delay_inMS){
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
                  drawTextCenter(item->font, count_down, 1);
                  seconds--;
                  counter--;
                  start = millis();
                }

                if (millis() - timer > 30){
                  dmd.drawString(--posx, 9, item->text1, strlen(item->text1), GRAPHICS_NORMAL);
                  if(posx < (-1*width)){
                    posx = (32*DISPLAYS_ACROSS) - 1;
                  }
                  timer = millis();
                }
              }
            }
            break;
          case DMD_TYPE_COUNTUP_SCROLL: //count up timer
            {
              int counter = item->duration_inMS/1000;
              int hours = 0;
              int minutes = 0;
              int seconds = 0;

              dmd.selectFont(item->font);
              
              unsigned long start = millis();
              unsigned long timer = start;
              int width = stringWidth(item->font,item->text1);
              int posx = (32*DISPLAYS_ACROSS) - 1;
              int countup = 0;
              while(countup <= counter){
                if(need_reset_dmd_loop_index){
                  break;
                }
                if (millis() - start > item->delay_inMS){
                  if(seconds==61){
                    seconds=1;
                    minutes++;
                  }
                  if(minutes==61){
                    minutes=1;
                    hours++;
                  }
                  //display
                  char count_up[9];
                  sprintf_P(count_up, (PGM_P)F("%02d:%02d:%02d"), hours, minutes, seconds);
                  drawTextCenter(item->font, count_up, 1);
                  seconds++;
                  countup++;
                  start = millis();
                }

                if (millis() - timer > 30){
                  dmd.drawString(--posx, 9, item->text1, strlen(item->text1), GRAPHICS_NORMAL);
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
        delay(item->delay_inMS);
      }

      //Logic to destroy DMDData
      bool deleteData = false;
      if(item->max_count > 0 && item->count >= item->max_count){
        deleteData = true;
      }

      if(item->life_time_inMS > 0 && (millis()-item->start_time_inMS) > item->life_time_inMS){
        deleteData = true;
      }

      if(deleteData){
        //reset struct to stop drawing in dmd
        item->count = 0;
        item->max_count = 1;
        item->delay_inMS = 0;
        item->duration_inMS = 0;
        item->life_time_inMS = -1;
        item->start_time_inMS = -1;
        item->type = -1;
        if(item->need_free_text1){
          free(item->text1);
        }
        if(item->need_free_text2){
          free(item->text2);
        }
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
String ssid = "";
String password = "";
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
    WiFi.begin(ssid.c_str(), password.c_str());

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
const char index_html_ap[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML>
<html>
 <head>
  <meta content="text/html; charset=ISO-8859-1" http-equiv="content-type">
  <meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
  <title>WiFi Creds Form</title>
  <style>
   body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; text-align:center; }
  </style>
 </head>
 <body>
  <h3>Enter your WiFi credentials</h3>
  <form action="/setwifi" method="post">
  <p>
   <label>SSID:&nbsp;</label>
   <input maxlength="30" name="ssid"><br>
   <label>Key:&nbsp;&nbsp;&nbsp;&nbsp;</label><input maxlength="30" name="password"><br><br>
   <input type="submit" value="Save"> 
  </p>
  </form>
  <form action="/forgetwifi" method="post">
  <p>
   <input type="submit" value="Forget wifi"> 
  </p>
 </body>
</html>
)rawliteral";

const char index_html_setting[] PROGMEM = R"rawliteral(
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
    <input type="submit" value="Notify">
  </form><br>
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
  server.send(200, "text/plain", "Selamat datang sahabat pengguna Speaker Murottal by AhsaiLabs");
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
            { server.send(200, "text/html", index_html_setting); });

  server.on("/get-setting", []()
            {
              String scrolltext = server.arg("scrolltext");
              char * info = (char *)malloc(sizeof(char)*(scrolltext.length()+1));
              sprintf_P(info, (PGM_P)F("%s"), scrolltext.c_str());              
              setupDMDdata(1,DMD_TYPE_SCROLL,info,true,(char*)"",false,Arial_Black_16,1000,5000,1,0.0);
              resetDMDLoopIndex();
              server.sendHeader("Location", "/setting", true);
              server.send(302, "text/plain", "");
            });

  server.on("/brightness", []()
            {
              String level = server.arg("level");
              ledcWrite(0, level.toInt());
              server.send(404, "text/plain", "ubah brigtness berhasil");
            });

  server.on("/restart", []()
            {
              server.send(200, "text/plain", "restart ESP");
              ESP.restart();
            });

  server.on("/setwifi",[](){
            if (server.hasArg("ssid")&& server.hasArg("password")) {
              String ssid = server.arg("ssid");
              String password = server.arg("password");
              preferences.putString("ssid", ssid);
              preferences.putString("password", password);
              server.send(200, "text/plain", "setting wifi berhasil, silakan restart");
              ESP.restart();
            } else {
              server.send(200, "text/html", index_html_ap);
            }
  });

  server.on("/forgetwifi",[](){
            preferences.remove("ssid");
            preferences.remove("password");
            server.send(200, "text/plain", "forget wifi berhasil, silakan restart");
            //ESP.restart();
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
  isClockReady = false;
  while (!isWiFiReady)
  {
    Serial.println("Task clock waiting for wifi...");
    delay(5000);
  }
  
  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  while(!getLocalTime(&timeinfo)){
    Serial.println("Clock : Failed to obtain time");
    delay(2000);
  }

  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  // Serial.print("Day of week: ");
  // Serial.println(&timeinfo, "%A");
  // Serial.print("Month: ");
  // Serial.println(&timeinfo, "%B");
  // Serial.print("Day of Month: ");
  // Serial.println(&timeinfo, "%d");
  // Serial.print("Year: ");
  // Serial.println(&timeinfo, "%Y");
  // Serial.print("Hour: ");
  // Serial.println(&timeinfo, "%H");
  // Serial.print("Hour (12 hour format): ");
  // Serial.println(&timeinfo, "%I");
  // Serial.print("Minute: ");
  // Serial.println(&timeinfo, "%M");
  // Serial.print("Second: ");
  // Serial.println(&timeinfo, "%S");

  // strftime(timeDay,3, "%d", &timeinfo);
  // strftime(timeMonth,10, "%B", &timeinfo);
  // strftime(timeYear,5, "%Y", &timeinfo);

  String type="AM";

  h24 = timeinfo.tm_hour; // 24 hours
  h = timeinfo.tm_hour > 12 ? timeinfo.tm_hour-12 : timeinfo.tm_hour;
  m = timeinfo.tm_min;
  s = timeinfo.tm_sec;

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


    // Serial.print("Time : ");
    // Serial.print(timeinfo.tm_hour);
    // Serial.print(":");
    // Serial.print(m);
    // Serial.print(":");
    // Serial.println(s);
    sprintf_P(str_clock_full, (PGM_P)F("%02d:%02d:%02d"), h24, m, s);
    isClockReady = true;
    //Serial.println(str_clock);
  }
}

//===========================================================================
//==================================   Task Date & Hijri Date  ==============
//===========================================================================
void taskDate(void * parameter)
{
  isDateReady = false;
  for (;;)
  {
    while (!isWiFiReady)
    {
      Serial.println("Task date waiting for wifi...");
      delay(5000);
    }
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    while(!getLocalTime(&timeinfo)){
      Serial.println("Date : Failed to obtain time");
      delay(2000);
    }

    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

    day = timeinfo.tm_mday;
    month = timeinfo.tm_mon; //0-11 since januari
    year = timeinfo.tm_year+1900;
    weekday = timeinfo.tm_wday;//0-6 since sunday

    //get hijri date
    char link[140] = {'\0'};
    sprintf_P(link, (PGM_P)F("https://www.al-habib.info/utils/calendar/pengubah-kalender-hijriyah-v7.php?the_y=%04d&the_m=%02d&the_d=%02d&the_conv=ctoh&lg=1"), year, month+1, day);
    Serial.println(link);

    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();

    // Your Domain name with URL path or IP address with path
    http.begin(client, link);

    // Send HTTP GET request
    int httpResponseCode = http.GET();

    if (httpResponseCode>0) {
      Serial.print("Date HTTP Response code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("Date Error code: ");
      Serial.println(httpResponseCode);
    }

    String jsonData = http.getString();

    // Free resources
    http.end();

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
      Serial.print(F("Date deserializeJson() failed: "));
      Serial.println(error.f_str());
      delay(20000);
      isDateReady = false;
      continue;
    }

    const char * hijri_date = doc["tanggal_hijriyah"];
    sprintf_P(str_hijri_date, (PGM_P)F("%s"), hijri_date);
    hijri_day = doc["hijri_tanggal"];
    hijri_month = doc["hijri_bulan"];
    hijri_year = doc["hijri_tahun"];


    String day_names[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jum'at", "Sabtu"};
    String month_names[] = {"Januari", "Februari", "Maret", "April", "Mei", "Juni", "Juli", "Agustus", "September", "Oktober", "November", "Desember"};

    memset(str_date,'\0',sizeof(char)*26);
    sprintf_P(str_date, (PGM_P)F("%s, %02d %s %02d"), day_names[weekday].c_str(), day, month_names[month].c_str(),year);
    
    sprintf_P(str_date_full, (PGM_P)F("%s / %s"), str_date, str_hijri_date);
    isDateReady = true;

    if(weekday == 0){
      setupDMDdata(15,DMD_TYPE_STATIC_SCROLL,"Besok adalah puasa hari senin, silakan dipersiapkan semuanya",false,"Info PUASA", false,  System5x7,1000,5000,-1,0.0, "09:00:00");
    } else if(weekday == 3){
      setupDMDdata(15,DMD_TYPE_STATIC_SCROLL,"Besok adalah puasa hari kamis, silakan dipersiapkan semuanya",false,"Info PUASA", false,  System5x7,1000,5000,-1,0.0, "09:00:00");
    }

    delayUntilAtTime(1,0,0);
  }
}


//=========================================================================
//==================================  Task Jadwal Sholat =================
//=========================================================================
void taskJadwalSholat(void * parameter){
  for(;;){
    if(!isDateReady){
      Serial.println("Task JWS waiting for date...");
      delay(3000);
      continue;
    }
    char link[100] = {'\0'};
    sprintf_P(link, (PGM_P)F("https://api.myquran.com/v1/sholat/jadwal/1301/%02d/%02d/%02d"), year, month+1, day);
    Serial.println(link);
    
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();

    // Your Domain name with URL path or IP address with path
    http.begin(client, link);
    
    // Send HTTP GET request
    int httpResponseCode = http.GET();
    
    if (httpResponseCode>0) {
      Serial.print("JWS HTTP Response code: ");
      Serial.println(httpResponseCode);
    }
    else {
      Serial.print("JWS Error code: ");
      Serial.println(httpResponseCode);
    }

    String jsonData = http.getString();

    // Free resources
    http.end();

    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
      Serial.print(F("JWS deserializeJson() failed: "));
      Serial.println(error.f_str());
      delay(20000);
      isJWSReady = false;
      continue;
    }

    JsonObject data_jadwal = doc["data"]["jadwal"];

    // for testing only
    // sprintf_P(data_jadwal_subuh, (PGM_P)F("%s:00"), "02:37");// "04:37"
    // sprintf_P(data_jadwal_syuruk, (PGM_P)F("%s:00"), "02:42");
    // sprintf_P(data_jadwal_dhuha, (PGM_P)F("%s:00"), "03:30");
    // sprintf_P(data_jadwal_dzuhur, (PGM_P)F("%s:00"), "04:30");
    // sprintf_P(data_jadwal_ashar, (PGM_P)F("%s:00"), "05:50");
    // sprintf_P(data_jadwal_maghrib, (PGM_P)F("%s:00"), "06:39");
    // sprintf_P(data_jadwal_isya, (PGM_P)F("%s:00"), "07:58");

    sprintf_P(data_jadwal_subuh, (PGM_P)F("%s:00"), data_jadwal["subuh"].as<const char*>());// "04:37"
    sprintf_P(data_jadwal_syuruk, (PGM_P)F("%s:00"), data_jadwal["terbit"].as<const char*>());// "04:37"
    sprintf_P(data_jadwal_dhuha, (PGM_P)F("%s:00"), data_jadwal["dhuha"].as<const char*>());// "04:37"
    sprintf_P(data_jadwal_dzuhur, (PGM_P)F("%s:00"), data_jadwal["dzuhur"].as<const char*>());
    sprintf_P(data_jadwal_ashar, (PGM_P)F("%s:00"), data_jadwal["ashar"].as<const char*>());
    sprintf_P(data_jadwal_maghrib, (PGM_P)F("%s:00"), data_jadwal["maghrib"].as<const char*>());
    sprintf_P(data_jadwal_isya, (PGM_P)F("%s:00"), data_jadwal["isya"].as<const char*>());
    
    Serial.print("Subuh : ");
    Serial.println(data_jadwal_subuh);
    Serial.print("Syuruk : ");
    Serial.println(data_jadwal_syuruk);
    Serial.print("Dhuha : ");
    Serial.println(data_jadwal_dhuha);
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



#define ALERT_COUNTUP_SHOLAT 5*60*1000/*5 menit*/
#define ALERT_COUNTDOWN_DZIKIR 5*60*1000/*5 menit*/

void taskCountDownJWS(void * parameter){
  for(;;){
    if(!isJWSReady){
      Serial.println("Task countdown-jws waiting for jws...");
      isCountdownJWSReady = false;
      delay(3000);
      continue;
    }
    std::array<unsigned long,4> clock = getArrayOfTime(str_clock_full);
    std::array<unsigned long,4> subuh = getArrayOfTime(data_jadwal_subuh);
    std::array<unsigned long,4> syuruk = getArrayOfTime(data_jadwal_syuruk);
    std::array<unsigned long,4> dhuha = getArrayOfTime(data_jadwal_dhuha);
    std::array<unsigned long,4> dzuhur = getArrayOfTime(data_jadwal_dzuhur);
    std::array<unsigned long,4> ashar = getArrayOfTime(data_jadwal_ashar);
    std::array<unsigned long,4> maghrib = getArrayOfTime(data_jadwal_maghrib);
    std::array<unsigned long,4> isya = getArrayOfTime(data_jadwal_isya);
    
    int counter = 0;

    memset(type_jws,'\0',sizeof(char)*8);

    if((clock[3] < subuh[3] && clock[3] >= 0) || (clock[3] >= isya[3] && clock[3] <=86400)){
      sprintf_P(type_jws, (PGM_P)F("subuh"));
      counter = sDistanceFromTimeToTime(clock[0],clock[1],clock[2],subuh[0],subuh[1],subuh[2]);
    } else if(clock[3] < syuruk[3]){
      sprintf_P(type_jws, (PGM_P)F("syuruk"));
      counter = syuruk[3] - clock[3];

      //it's time to dzikir in the morning
      setupDMDdata(2,DMD_TYPE_STATIC_SCROLL,"Dzikir Pagi",false,count_down_jws,false,System5x7,1000,ALERT_COUNTDOWN_DZIKIR,-1,msDistanceFromNowToTime(syuruk[0], syuruk[1], syuruk[2]));
      resetDMDLoopIndex();
    } else if(clock[3] < dhuha[3]){
      sprintf_P(type_jws, (PGM_P)F("dhuha"));
      counter = dhuha[3] - clock[3];

      //it's time to sholat dhuha
      setupDMDdata(3,DMD_TYPE_STATIC_SCROLL,"Waktu Sholat Dhuha",false,str_clock_full,false,System5x7,1000,10000,-1,(dzuhur[3]-dhuha[3]-(15*60))*1000);
      resetDMDLoopIndex();
    } else if(clock[3] < dzuhur[3]){
      sprintf_P(type_jws, (PGM_P)F("dzuhur"));
      counter = dzuhur[3] - clock[3];
    } else if(clock[3] < ashar[3]){
      sprintf_P(type_jws, (PGM_P)F("ashar"));
      counter = ashar[3] - clock[3];
    } else if(clock[3] < maghrib[3]){
      sprintf_P(type_jws, (PGM_P)F("maghrib"));
      counter = maghrib[3] - clock[3];

      //it's time to dzikir in the afternoon
      setupDMDdata(2,DMD_TYPE_STATIC_SCROLL,"Dzikir Petang",false,count_down_jws,false,System5x7,1000,ALERT_COUNTDOWN_DZIKIR,-1,msDistanceFromNowToTime(maghrib[0], maghrib[1], maghrib[2]));
      resetDMDLoopIndex();
    } else if(clock[3] < isya[3]){
      sprintf_P(type_jws, (PGM_P)F("isya"));
      counter = isya[3] - clock[3];


      //it's time to update hijri date
      sprintf_P(str_hijri_date, (PGM_P)F("%d%s"), hijri_day+1,(hijri_day >= 10 ? str_hijri_date+2 : str_hijri_date+1));     
      Serial.print("New Hijri Date :");
      Serial.println(str_hijri_date);
      sprintf_P(str_date_full, (PGM_P)F("%s / %s"), str_date, str_hijri_date);
    }

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

    Serial.print("Counter Countdown for ");
    Serial.print(type_jws);
    Serial.print(" : ");
    Serial.print(counter);
    Serial.print(" ==> ");
    Serial.print(hours);
    Serial.print(" - ");
    Serial.print(minutes);
    Serial.print(" - ");
    Serial.println(seconds);

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
      //Serial.print("String Countdown : ");
      //Serial.print(type_jws);
      //Serial.print(" : ");
      //Serial.println(count_down_jws);
      seconds--;
      counter--;
      delay(1000);
    }

    //show alert
    char count_sholat_alert[30] = {0};
    sprintf_P(count_sholat_alert, (PGM_P)F("Sudah masuk waktu sholat %s"), type_jws);
    setupDMDdata(1,DMD_TYPE_COUNTUP_SCROLL,count_sholat_alert,"",System5x7,1000,ALERT_COUNTUP_SHOLAT,1);
    resetDMDLoopIndex();
    delay(5000);
  }
}


//=========================================================================
//==================================   Task Button / Touch Handle  ============================
//=========================================================================
void taskButtonTouch(void * parameter){
  for(;;){
    uint16_t touchValue = touchRead(33);
    bool isTouched = touchValue < 20;
    //Serial.print("Touch Value : ");
    //Serial.println(touchValue);
    if(isTouched){
      //remove ssid & password in preferences setting
      //preferences.remove("ssid");
      //preferences.remove("password");
      Serial.println("Restarting after remove wifi credential");
      //delay(3000);
      //ESP.restart();
    }
    delay(1000);
  }
}


//=========================================================================
//==================================   SPIFFS  ============================
//=========================================================================

void listAllFiles(){
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while(file){
      Serial.print("FILE: ");
      Serial.println(file.name());
      file = root.openNextFile();
  }
}

//=========================================================================
//==================================   Main App  ==========================
//=========================================================================
void setup()
{
  pinMode(built_in_led, OUTPUT);
  Serial.begin(115200);
  delay(1000); // give me time to bring up serial monitor
  preferences.begin("settings", false);
  //ssid = preferences.getString("ssid","3mbd3vk1d-2");
  //password = preferences.getString("password","marsupiarmadomah3716");
  ssid = preferences.getString("ssid","");
  password = preferences.getString("password","");

  while(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  isSPIFFSReady = true;

  // xTaskCreatePinnedToCore(
  //     taskButtonTouch,  // Function that should be called
  //     "Button/Touch Action",   // Name of the task (for debugging)
  //     1000,           // Stack size (bytes)
  //     NULL,           // Parameter to pass
  //     1,              // Task priority
  //     &taskButtonTouchHandle, // Task handle
  //     CONFIG_ARDUINO_RUNNING_CORE);

  if(ssid.length() <= 0 || password.length() <= 0){
    WiFi.mode(WIFI_AP);
    IPAddress IP = {192, 168, 48, 81};
    IPAddress NMask = {255, 255, 255, 0};
    WiFi.softAPConfig(IP, IP, NMask);
    WiFi.softAP("Speaker Murottal AP", "qwerty654321");
    if (MDNS.begin("speaker-murottal"))
    {
      Serial.println("speaker-murottal.local is available");
    }
    startTaskToggleLED();
  } else {
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
        taskDate,  // Function that should be called
        "Date",   // Name of the task (for debugging)
        7000,           // Stack size (bytes)
        NULL,           // Parameter to pass
        1,              // Task priority
        &taskDateHandle, // Task handle
        0);

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
        &taskCountdownJWSHandle, // Task handle
        CONFIG_ARDUINO_RUNNING_CORE);

    delay(5000);
    xTaskCreatePinnedToCore(
        taskDMD,        // Function that should be called
        "Display DMD",  // Name of the task (for debugging)
        5000,           // Stack size (bytes)
        NULL,           // Parameter to pass
        1,              // Task priority
        &taskDMDHandle, // Task handle
        CONFIG_ARDUINO_RUNNING_CORE);
  }

  delay(5000);
  xTaskCreatePinnedToCore(
      taskWebServer,  // Function that should be called
      "Web Server",   // Name of the task (for debugging)
      5000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskWebHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);

  //vTaskDelete(NULL);
}

void loop()
{
  //do nothing, everything is doing in task
}