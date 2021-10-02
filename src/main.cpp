#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <SD.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <DMD32.h>
#include "fonts/SystemFont5x7.h"
#include "fonts/Arial_black_16.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include <nvs_flash.h>
#include <Preferences.h>
#include <FirebaseFS.h>
#include <FirebaseESP32.h>
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"



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
TaskHandle_t taskFirebaseHandle;
TaskHandle_t taskWebSocketHandle;

SemaphoreHandle_t mutex_con;
SemaphoreHandle_t mutex_dmd;

Preferences preferences;

volatile int h24 = 12; //hours in 24 format
volatile int h = 12; // hours in 12 format
volatile int m = 0; //minutes
volatile int s = 0; //seconds
char str_clock_full[9] = "--:--:--"; //used by dmd task
char str_date[26] = "------, -- --------- ----"; //used by dmd task
char str_hijri_date[30] = "-- ------- ----- ----";
char str_date_full[55] = "";
// char timeDay[3];
// char timeMonth[10];
// char timeYear[5];
volatile int day = -1;
volatile int month = -1;
volatile int year = -1;
volatile int weekday = -1;
volatile int hijri_day = -1;
volatile int hijri_month = -1;
volatile int hijri_year = -1;


volatile bool isWiFiReady = false;
volatile bool isClockReady = false;
volatile bool isDateReady = false;
volatile bool isJWSReady = false;
volatile bool isSPIFFSReady = false;
volatile bool isWebSocketReady = false;
volatile bool isFirebaseReady = false;

const uint8_t built_in_led = 2;
const uint8_t relay = 26;

const uint8_t marquee_speed = 27;

char data_jadwal_subuh[9];
char data_jadwal_syuruk[9];
char data_jadwal_dhuha[9];
char data_jadwal_dzuhur[9];
char data_jadwal_ashar[9];
char data_jadwal_maghrib[9];
char data_jadwal_isya[9];

char type_jws[8] = "sholat"; //subuh, dzuhur, ashar, maghrib, isya
char count_down_jws[9] = "--:--:--"; //04:30:00

//22.30 - 23.45 : 1 jam + 15 menit
//22.30 - 23.15 : 1 jam + -15 menit
//22.30 - 22.45 : 0 jam + 15 menit
//22.30 - 22.15 : 0 jam + -15 menit + 24 jam
//22.30 - 01.45 : -21 jam + 15 menit + 24 jam
//22.30 - 01.15 : -21 jam + -15 menit + 24 jam

//================================================================================
//==================================   Task Web Socket Server  ===================
//================================================================================
WebSocketsServer webSocket = WebSocketsServer(81);

void log(const char * message){
  Serial.print(message);
  if(isWebSocketReady){
    webSocket.broadcastTXT(message, strlen(message));
  }
}


void logln(const char * message){
  log(message);
  Serial.println();
  if(isWebSocketReady){
    webSocket.broadcastTXT("<br>",4, false);
  }
}

void logf(const char *format, ...){
  char loc_buf[128];
  char * temp = loc_buf;
  va_list arg;
  va_list copy;
  va_start(arg, format);
  va_copy(copy, arg);
  int len = vsnprintf(temp, sizeof(loc_buf), format, copy);
  va_end(copy);
  if(len < 0) {
      va_end(arg);
      return;
  };
  if(len >= sizeof(loc_buf)){
      temp = (char*) malloc(len+1);
      temp[len] = '\0';
      if(temp == NULL) {
          va_end(arg);
          return;
      }
      len = vsnprintf(temp, len+1, format, arg);
  }
  va_end(arg);

  Serial.print(temp);
  Serial.println();
  if(isWebSocketReady){
    webSocket.broadcastTXT(temp, len, false);
    webSocket.broadcastTXT("<br>",4, false);
  }
  if(temp != loc_buf){
      free(temp);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

				        // send message to client
				        webSocket.sendTXT(num, "Connected");
            }
            break;
        case WStype_TEXT:
            Serial.printf("[%u] get Text: %s\n", num, payload);

            // send message to client
            webSocket.sendTXT(num, "Command Received : OK");

            // send data to all connected clients
            // webSocket.broadcastTXT("message here");
            break;
        case WStype_BIN:
            Serial.printf("[%u] get binary length: %u\n", num, length);
            //hexdump(payload, length);

            // send message to client
            // webSocket.sendBIN(num, payload, length);
            break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
    }
}

void taskWebSocketServer(void * paramater){
  isWebSocketReady = false;
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  isWebSocketReady = true;
  logln("Web Socket server started");
  logf("Web socket stack size : %d", uxTaskGetStackHighWaterMark(NULL));
  for(;;){
    webSocket.loop();
    delay(1000);
  }
}



char * getAllocatedString(String text){
  unsigned int length = text.length()+1;
  char * allocatedString = (char *)malloc(sizeof(char)*(length));
  memset(allocatedString,'\0',sizeof(char)*length);
  sprintf_P(allocatedString, (PGM_P)F("%s"), text.c_str());
  //allocatedString[length-1] = '\0';
  return allocatedString;
}

long long sDistanceFromNowToTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
    long long deltaInSecond = ((hours-h24)*3600) + ((minutes-m)*60) + seconds-s;
    if(deltaInSecond <= 0){
      deltaInSecond += 24*3600;
    }
    return deltaInSecond;
}

long long msDistanceFromNowToTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
  return sDistanceFromNowToTime(hours, minutes, seconds) * 1000;
}

long long sDistanceFromTimeToTime(uint8_t fhours, uint8_t fminutes, uint8_t fseconds, uint8_t thours, uint8_t tminutes, uint8_t tseconds){
    long long deltaInSecond = ((thours-fhours)*3600) + ((tminutes-fminutes)*60) + (tseconds-fseconds);
    if(deltaInSecond <= 0){
      deltaInSecond += 24*3600;
    }
    return deltaInSecond;
}

long long msDistanceFromTimeToTime(uint8_t fhours, uint8_t fminutes, uint8_t fseconds, uint8_t thours, uint8_t tminutes, uint8_t tseconds){
    return sDistanceFromTimeToTime(fhours, fminutes, fseconds, thours, tminutes, tseconds)*1000;
}


std::array<long long, 2> sDistanceFromDayTimeToDayTime(int16_t fdays, uint8_t fhours, uint8_t fminutes, uint8_t fseconds, int16_t tdays, uint8_t thours, uint8_t tminutes, uint8_t tseconds){
    //now is day 0, fdays=1 means tomorrow, fdays=2 means the day after tomorrow
    std::array<long long,2> result;
    //1, 9:00:00 ==> 5, 8:30:00
    //5, 9:00:00 ==> 5, 8:30:00
    //2, 9:00:00 ==> 2, 8:00:00
    //-1, 9:00:00 ==> 0, 22:00:00 ==> 0, 23:00:00
    long long deltaInSecond = 0;
    deltaInSecond += (tdays-fdays-1)*24*3600;
    deltaInSecond += sDistanceFromTimeToTime(fhours,fminutes,fseconds,24,0,0);
    deltaInSecond += sDistanceFromTimeToTime(0,0,0,thours,tminutes,tseconds);
    result[1] = deltaInSecond; //distance from 'from' to 'to'

    deltaInSecond = 0;
    deltaInSecond += (fdays-0-1)*24*3600; //-48 jam + 2 jam + 9 jam = -37
    deltaInSecond += sDistanceFromTimeToTime(h24,m,s,24,0,0); 
    deltaInSecond += sDistanceFromTimeToTime(0,0,0,fhours,fminutes,fseconds);
    result[0] = deltaInSecond; //distance from 'now' to 'from'
    return result;
}


std::array<long long, 2> msDistanceFromDayTimeToDayTime(int16_t fdays, uint8_t fhours, uint8_t fminutes, uint8_t fseconds, int16_t tdays, uint8_t thours, uint8_t tminutes, uint8_t tseconds){
  std::array<long long, 2> sDistance = sDistanceFromDayTimeToDayTime(fdays,fhours, fminutes, fseconds, tdays, thours,tminutes, tseconds);
  std::array<long long, 2> msDistance;
  msDistance[0] = sDistance[0]*1000;
  msDistance[1] = sDistance[1]*1000;
  return msDistance;
}

void delayMSUntilAtTime(uint8_t hours, uint8_t minutes, uint8_t seconds){
    delay(msDistanceFromNowToTime(hours, minutes, seconds));
}

void eraseNVS(){
  nvs_flash_erase(); // erase the NVS partition and...
  nvs_flash_init(); // initialize the NVS partition.
  ESP.restart();
  while(true);
}

std::array<unsigned long, 4>  getArrayOfTime(const char * time){
  //log("getArrayOfTime : ");
  //log(time);

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
  // log("=>");
  // log(as[0]);
  // log("-");
  // log(as[1]);
  // log("-");
  // log(as[2]);
  // log("-");
  // logln(as[3]);
  return as;
}

//=========================================================================
//==================================   Task DMD  ==========================
//=========================================================================
#define DISPLAYS_ACROSS 2
#define DISPLAYS_DOWN 1
#define DMD_DATA_SIZE 30
#define DMD_DATA_FLASH_INDEX 0
#define DMD_DATA_IMPORTANT_INDEX 1
#define DMD_DATA_REGULER_INDEX 6
#define DMD_DATA_FREE_INDEX DMD_DATA_SIZE

enum DMDType {
  DMD_TYPE_SCROLL_STATIC, DMD_TYPE_STATIC_STATIC, DMD_TYPE_SCROLL, DMD_TYPE_SCROLL_COUNTDOWN, DMD_TYPE_SCROLL_COUNTUP
};

struct DMD_Data{
  int type = -1; //0:datetime, 1:jws, 2:scrollingtext, 3:countdown, 4:countup
  char * text1;
  uint8_t speed1 = 0;
  bool need_free_text1 = false;
  char * text2;
  uint8_t speed2 = 0;
  bool need_free_text2 = false;
  const uint8_t * font;
  unsigned long delay_inMS = 0; //delay refresh dalam setiap kemunculan
  unsigned long duration_inMS = 0; //durasi setiap kemunculan
  int max_count = 1; //jumlah kemunculan, -1 for unlimited
  int count = 0; //by code
  unsigned long life_time_inMS = 0; // in ms
  long long start_time_inMS = 0; //by code
};

bool need_reset_dmd_loop_index = false;
int dmd_loop_index = 0; //we can change this runtime
struct DMD_Data dmd_data_list[DMD_DATA_SIZE]; //index 0 - 5 for important message
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

hw_timer_t *timer = NULL;

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
}


void marqueeText(const uint8_t *font, const char * text, int top){
  dmd.selectFont(font);
  dmd.drawMarquee(text, strlen(text), (32 * DISPLAYS_ACROSS) - 1, top);
  unsigned long start = millis();
  unsigned long timer = start;
  boolean ret = false;
  while (!ret)
  {
    if ((timer + marquee_speed) < millis())
    {
      ret = dmd.stepMarquee(-1, 0);
      timer = millis();
    }
  }
}

void  resetDMDLoopIndex(){ //use this function to make show important message right now
  need_reset_dmd_loop_index = true;
}

uint8_t getAvailableDMDIndex(bool isImportant, uint8_t reservedIndex){
  uint8_t choosenIndex = 0;
  if(reservedIndex >= DMD_DATA_SIZE){
    if(isImportant){
      choosenIndex = DMD_DATA_IMPORTANT_INDEX;
    } else {
      choosenIndex = DMD_DATA_REGULER_INDEX;
    }
  } else {
    choosenIndex = reservedIndex;
  }
  bool full = false;
  while(dmd_data_list[choosenIndex].type >= 0 && !full){
    choosenIndex++;
    if(choosenIndex >= DMD_DATA_SIZE){
      full = true;
    }
  }
  return choosenIndex;
}

//show with custom
void setupDMDdata(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, uint8_t speed1, bool need_free_text1, const char * text2, uint8_t speed2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int max_count, unsigned long life_time_inMS, long long start_time_inMS){
  logln("dmd wait.....");
  xSemaphoreTake(mutex_dmd, portMAX_DELAY); 
  logln("dmd start.....");
  uint8_t index = getAvailableDMDIndex(isImportant, reservedIndex);
  
  if(index >= DMD_DATA_SIZE){
    logln("DMD slot is full");
    xSemaphoreGive(mutex_dmd); 
    return;
  }
  logf("%s : %s,index : %d,type : %d,max_count : %d,life_time : %d", text1,text2,index,type,max_count,life_time_inMS);

  dmd_data_list[index].type = type;
  dmd_data_list[index].text1 = (char*)text1;
  dmd_data_list[index].speed1 = speed1;
  dmd_data_list[index].need_free_text1 = need_free_text1;
  dmd_data_list[index].text2 = (char*)text2;
  dmd_data_list[index].speed2 = speed2;
  dmd_data_list[index].need_free_text2 = need_free_text2;
  dmd_data_list[index].font = font;
  dmd_data_list[index].delay_inMS = delay_inMS;
  dmd_data_list[index].duration_inMS = duration_inMS;
  dmd_data_list[index].max_count = max_count;
  dmd_data_list[index].count = 0;
  dmd_data_list[index].life_time_inMS = life_time_inMS;
  dmd_data_list[index].start_time_inMS = start_time_inMS;
  logln("dmd done .....");
  xSemaphoreGive(mutex_dmd); 
}

void resetDMDData(uint8_t index){
  DMD_Data * item = dmd_data_list+index;
  if(item->type >= 0 && item->need_free_text1){
    free(item->text1);
    item->text1 = NULL;
  }
  if(item->type >= 0 && item->need_free_text2){
    free(item->text2);
    item->text2 = NULL;
  }
  item->type = -1;
  item->speed1 = 0;
  item->need_free_text1 = false;
  item->speed2 = 0;
  item->need_free_text2 = false;
  item->font = 0;
  item->delay_inMS = 0;
  item->duration_inMS = 0;
  item->max_count = 0;
  item->count = 0;
  item->life_time_inMS = 0;
  item->start_time_inMS = 0;
}

void showFlashMessage(const char * text, bool need_free_text){
  resetDMDData(DMD_DATA_FLASH_INDEX);
  setupDMDdata(true,DMD_DATA_FLASH_INDEX,DMD_TYPE_SCROLL,text,0,need_free_text,"",0,false, Arial_Black_16,1000,4000,1,0,0);
  resetDMDLoopIndex();
}


//show at exact range time
void setupDMDAtExactRangeTime(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS,  int16_t start_day, const char * start_time, int16_t end_day, const char * end_time /*09:10:23*/){
  std::array<unsigned long, 4> start_time_info = getArrayOfTime(start_time);
  std::array<unsigned long, 4> end_time_info = getArrayOfTime(end_time);
  std::array<long long, 2> distance_info = msDistanceFromDayTimeToDayTime(start_day,start_time_info[0],start_time_info[1],start_time_info[2],end_day, end_time_info[0],end_time_info[1],end_time_info[2]);
  setupDMDdata(isImportant,reservedIndex,type,text1,0,need_free_text1,text2,0,need_free_text2,font,delay_inMS,duration_inMS,-1, distance_info[1], millis()+distance_info[0]);
}

//show at exact time for iteration
void setupDMDAtExactTimeForIteration(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int max_count, int16_t day, const char * exact_time /*09:10:23*/){
  std::array<unsigned long, 4> timeInfo = getArrayOfTime(exact_time);
  std::array<long long, 2> distance_info = msDistanceFromDayTimeToDayTime(day,timeInfo[0],timeInfo[1],timeInfo[2],day,timeInfo[0],timeInfo[1],timeInfo[2]);
  setupDMDdata(isImportant,reservedIndex,type,text1,0,need_free_text1,text2,0,need_free_text2,font,delay_inMS,duration_inMS,max_count, 0, millis()+distance_info[0]);
}

//show at exact time for some life time
void setupDMDAtExactTimeForLifeTime(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, unsigned long life_time_inMS, int16_t day, const char * exact_time /*09:10:23*/){
  std::array<unsigned long, 4> timeInfo = getArrayOfTime(exact_time);
  std::array<long long, 2> distance_info = msDistanceFromDayTimeToDayTime(day,timeInfo[0],timeInfo[1],timeInfo[2],day,timeInfo[0],timeInfo[1],timeInfo[2]);
  setupDMDdata(isImportant,reservedIndex,type,text1,0,need_free_text1,text2,0,need_free_text2,font,delay_inMS,duration_inMS,-1, life_time_inMS, millis()+distance_info[0]);
}

//show at exact time forever
void setupDMDAtExactTimeForever(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int16_t day, const char * exact_time /*09:10:23*/){
  std::array<unsigned long, 4> timeInfo = getArrayOfTime(exact_time);
  std::array<long long, 2> distance_info = msDistanceFromDayTimeToDayTime(day,timeInfo[0],timeInfo[1],timeInfo[2],day,timeInfo[0],timeInfo[1],timeInfo[2]);
  setupDMDdata(isImportant,reservedIndex,type,text1,0,need_free_text1,text2,0,need_free_text2,font,delay_inMS,duration_inMS,-1, 0, millis()+distance_info[0]);
}

//show at now for some iteration
void setupDMDAtNowForIteration(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, int max_count){
  setupDMDdata(isImportant,reservedIndex,type,text1,0,need_free_text1,text2,0,need_free_text2,font,delay_inMS,duration_inMS,max_count,0,0);
}

//show at now for some life time
void setupDMDAtNowForLifeTime(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS, unsigned long life_time_inMS){
  setupDMDdata(isImportant,reservedIndex,type,text1,0,need_free_text1,text2,0,need_free_text2,font,delay_inMS,duration_inMS,-1,life_time_inMS,0);
}

//show at now forever
void setupDMDAtNowForever(bool isImportant, uint8_t reservedIndex, DMDType type, const char * text1, bool need_free_text1, const char * text2, bool need_free_text2, const uint8_t * font, unsigned long delay_inMS, unsigned long duration_inMS){
  setupDMDdata(isImportant,reservedIndex,type,text1,0,need_free_text1,text2,0,need_free_text2,font,delay_inMS,duration_inMS,-1,0,0);
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


  //setup clock
  wifi_mode_t mode = WiFi.getMode();
  if(mode == WIFI_MODE_STA){
    setupDMDAtNowForever(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,str_date_full,false,str_clock_full,false,System5x7,1000,15000);
  } else if(mode == WIFI_MODE_AP){
    setupDMDAtNowForever(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"1. silakan connect ke wifi 'Speaker Murottal AP' dengan password 'qwerty654321'",false,"Cara Setup",false,System5x7,1000,5000);
    setupDMDAtNowForever(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"2. Akses website http://speaker-murottal.local",false,"Cara Setup",false,System5x7,1000,5000);
    setupDMDAtNowForever(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"3. Masuk menu 'Wifi manager' dan set wifi akses anda yg terkoneksi ke internet",false,"Cara Setup",false,System5x7,1000,5000);
    setupDMDAtNowForever(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"4. silakan restart device anda",false,"Cara Setup",false,System5x7,1000,5000);
  }

  dmd.clearScreen(true);
  marqueeText(Arial_Black_16, "Assalamu'alaikum",1);
  dmd.clearScreen(true);
  marqueeText(Arial_Black_16, "Developed by AhsaiLabs", 1);

  logln("DMD is coming");
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
  logf("DMD stack size : %d", uxTaskGetStackHighWaterMark(NULL));
  for (;;)
  {
    //byte b;
    // 10 x 14 font clock, including demo of OR and NOR modes for pixels so that the flashing colon can be overlayed
    //dmd.drawBox(0, 0, (32 * DISPLAYS_ACROSS) - 1, (16 * DISPLAYS_DOWN) - 1, GRAPHICS_TOGGLE);

    for(dmd_loop_index=0;dmd_loop_index<DMD_DATA_SIZE;dmd_loop_index++){
      if(need_reset_dmd_loop_index){
        need_reset_dmd_loop_index = false;
        dmd_loop_index = -1;
        continue;
      }

      DMD_Data * item = dmd_data_list+dmd_loop_index;

      xSemaphoreTake(mutex_con, portMAX_DELAY);
      logln("======= start ======");
      logf("index : %d", dmd_loop_index);
      logf("type : %d", item->type);
      logf("text1 : %d", item->text1);
      logf("text2 : %d", item->text2);
      logf("start_time : %d", item->start_time_inMS);
      logf("max_count : %d", item->max_count);
      logf("life_time : %d", item->life_time_inMS);
      logln("======== end =======");
      xSemaphoreGive(mutex_con);

      if(item->type < 0){
        //logln("no type");
        continue;
      }

      unsigned long start  = millis();

      if(item->start_time_inMS > 0 && start < item->start_time_inMS){
        //logln("dont go now");
        continue;
      }

      //Logic to destroy DMDData
      bool deleteData = false;
      if(item->max_count > 0 && item->count >= item->max_count){
        //logln("max_count > 0");
        deleteData = true;
      }

      if(item->life_time_inMS > 0 && (millis()-item->start_time_inMS) > item->life_time_inMS){
        //logln("life_time_inMS > 0");
        deleteData = true;
      }

      if(deleteData){
        //reset struct to stop drawing in dmd
        resetDMDData(dmd_loop_index);
        //logln("delete");
        continue;
      }

      //logln("go.................");
      dmd.clearScreen(true);
      item->count++;
      while(start + item->duration_inMS > millis()){
        if(need_reset_dmd_loop_index){
          break;
        }

        switch (item->type)
        {
          case DMD_TYPE_SCROLL_STATIC:
            {
              int counter = item->duration_inMS/1000;
              unsigned long start = millis();
              unsigned long timer = start;
              dmd.selectFont(item->font);
              int width = stringWidth(item->font,item->text1);
              int posx = (32*DISPLAYS_ACROSS) - 1;
              bool message_full_displayed = false;
              while(counter >= 0 || !message_full_displayed){
                  if(need_reset_dmd_loop_index){
                    break;
                  }
                  if (millis() - start > item->delay_inMS){   
                    drawTextCenter(item->font, item->text2, 1);
                    start = millis();
                    counter--;
                  }
                  if (millis() - timer > marquee_speed){
                    dmd.drawString(--posx, 9, item->text1, strlen(item->text1), GRAPHICS_NORMAL);
                    if(posx < (-1*width)){
                      posx = (32*DISPLAYS_ACROSS) - 1;
                      message_full_displayed = true;
                    } else {
                      message_full_displayed = false;
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
            {
              dmd.selectFont(item->font);
              dmd.drawMarquee(item->text1, strlen(item->text1), (32 * DISPLAYS_ACROSS) - 1, 1);
              unsigned long start = millis();
              unsigned long timer = start;
              boolean ret = false;
              while (!ret){
                if(need_reset_dmd_loop_index){
                  break;
                }
                if ((timer + marquee_speed) < millis())
                {
                  ret = dmd.stepMarquee(-1, 0);
                  timer = millis();
                }
              }
            }
            break;
          case DMD_TYPE_SCROLL_COUNTDOWN: //count down timer
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

                if (millis() - timer > marquee_speed){
                  dmd.drawString(--posx, 9, item->text1, strlen(item->text1), GRAPHICS_NORMAL);
                  if(posx < (-1*width)){
                    posx = (32*DISPLAYS_ACROSS) - 1;
                  }
                  timer = millis();
                }
              }
            }
            break;
          case DMD_TYPE_SCROLL_COUNTUP: //count up timer
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

                if (millis() - timer > marquee_speed){
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
      } //end while
    } //end for
    

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


void startTaskDMD(){
  xTaskCreatePinnedToCore(
      taskDMD,        // Function that should be called
      "Display DMD",  // Name of the task (for debugging)
      4500,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskDMDHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);
}

void stopTaskDMD(){
  if(taskDMDHandle != NULL){
    vTaskDelete(taskDMDHandle);
  }
}


//================================================================================
//==================================   Task Toggle LED  ==========================
//================================================================================
uint32_t led_on_delay = 500;
uint32_t led_off_delay = 500;

void taskToggleLED(void *parameter)
{

  //logf("LED stack size : %d", uxTaskGetStackHighWaterMark(NULL));
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
  if(taskLEDHandle != NULL){
    vTaskDelete(taskLEDHandle);
  }
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

    logln("[WIFI] Connecting");
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
      logln("[WIFI] FAILED");
      stopTaskToggleLED();
      delay(WIFI_RECOVER_TIME_MS);
      continue;
    }

    isWiFiReady = true;
    log("Connected to ");
    logln(ssid.c_str());
    log("IP address: ");
    logln(WiFi.localIP().toString().c_str());

    if (MDNS.begin("speaker-murottal"))
    {
      logln("speaker-murottal.local is available");
    }

    logf("Keep Wifi stack size : %d", uxTaskGetStackHighWaterMark(NULL));
    stopTaskToggleLED();
  }
}



//================================================================================
//==================================   Task Web Server  ==========================
//================================================================================
const char index_html_wifi[] PROGMEM = R"rawliteral(
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
  <form action="/wifi" method="post">
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
    Flash Message: <input type="text" name="scrolltext">
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

const char index_html_ws[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>Log Serial</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <meta charset="UTF-8" />
    <style>
      body {
        background-color: #e6d8d5;
      }
      p {
        background-color: #a59999;
        color: #020000;
      }
      /* The switch - the box around the slider */
      .switch {
        position: relative;
        display: inline-block;
        width: 60px;
        height: 34px;
      }

      /* Hide default HTML checkbox */
      .switch input {
        opacity: 0;
        width: 0;
        height: 0;
      }

      /* The slider */
      .slider {
        position: absolute;
        cursor: pointer;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
        background-color: #ccc;
        -webkit-transition: 0.4s;
        transition: 0.4s;
      }

      .slider:before {
        position: absolute;
        content: "";
        height: 26px;
        width: 26px;
        left: 4px;
        bottom: 4px;
        background-color: white;
        -webkit-transition: 0.4s;
        transition: 0.4s;
      }

      input:checked + .slider {
        background-color: #2196f3;
      }

      input:focus + .slider {
        box-shadow: 0 0 1px #2196f3;
      }

      input:checked + .slider:before {
        -webkit-transform: translateX(26px);
        -ms-transform: translateX(26px);
        transform: translateX(26px);
      }

      /* Rounded sliders */
      .slider.round {
        border-radius: 34px;
      }

      .slider.round:before {
        border-radius: 50%;
      }
    </style>
  </head>
  <body>
    <h1 id="heading">Received Logs: <small>active</small></h1>
    <p id="message"></p>
    <button type="button" id="btn_reset">reset</button>
    <label class="switch">
      <input id="cb_on" type="checkbox" onclick="handleClick(this);" checked/>
      <span class="slider round"></span>
    </label>
  </body>
  <script>
    var Socket;
    var heading = document.getElementById("heading");
    var p_message = document.getElementById("message");
    var btn_reset = document.getElementById("btn_reset");
    var cb_on = document.getElementById("cb_on");
    btn_reset.addEventListener("click", button_reset_pressed);
    function init() {
      Socket = new WebSocket("ws://" + window.location.hostname + ":81/");
      Socket.onmessage = function (event) {
        processCommand(event);
      };
    }

    function handleClick(cb) {
      console.log("Clicked, new value = " + cb.checked);
      if(cb.checked){
        heading.innerHTML = "Received Logs: <small>active</small>";
      } else {
        heading.innerHTML = "Received Logs: <small>inactive</small>";
      }
    }

    function processCommand(event) {
      if (cb_on.checked) {
        var log = event.data;
        p_message.innerHTML = p_message.innerHTML + log;
        console.log(log);
      }
    }

    function button_reset_pressed() {
      //Socket.send("on");
      p_message.innerHTML = "";
    }
    window.onload = function (event) {
      init();
    };
  </script>
</html>
)rawliteral";

const char index_html_root[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>Log Serial</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <meta charset="UTF-8" />
    <style>
      body {
        background-color: #e6d8d5;
        text-align: center;
      }
      a {
        display: block;
        line-height: 30px;
      }
    </style>
  </head>
  <body>
    <h1>Selamat datang sahabat pengguna Speaker Murottal by AhsaiLabs</h1>
    <a href="/wifi">Wifi Manager</a>
    <a href="/setting">Settings</a>
    <a href="/logs">Show Logs</a>
    <a href="/restart">Restart</a>
  </body>
  <script>
    function init() {
    }
    window.onload = function (event) {
      init();
    };
  </script>
</html>
)rawliteral";

WebServer server(80);
void handleWebRoot()
{
  server.send(200, "text/html", index_html_root);
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

  server.on("/setting", []()
            { server.send(200, "text/html", index_html_setting); });

  server.on("/get-setting", []()
            {
              String scrolltext = server.arg("scrolltext");
              char * info = getAllocatedString(scrolltext);            
              showFlashMessage(info,true);
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

  server.on("/wifi",[](){
            if (server.hasArg("ssid")&& server.hasArg("password")) {
              String ssid = server.arg("ssid");
              String password = server.arg("password");
              stopTaskDMD();
              delay(1000);
              preferences.putString("ssid", ssid);
              preferences.putString("password", password);
              server.send(200, "text/plain", "setting wifi berhasil, silakan restart");
              startTaskDMD();
              //ESP.restart();
            } else {
              server.send(200, "text/html", index_html_wifi);
            }
  });

  server.on("/forgetwifi",[](){
            preferences.remove("ssid");
            preferences.remove("password");
            server.send(200, "text/plain", "forget wifi berhasil, silakan restart");
            //ESP.restart();
  });

  server.on("/logs",[](){
            server.send(200, "text/html", index_html_ws);
  });

  server.onNotFound(handleWebNotFound);

  server.begin();
  logln("HTTP server started");
  logf("Web Server stack size : %d",uxTaskGetStackHighWaterMark(NULL));
  for (;;)
  {
    handleServerClient();
  }
}


void startTaskWebSocketServer(){
  xTaskCreatePinnedToCore(
      taskWebSocketServer,  // Function that should be called
      "Web Socket",   // Name of the task (for debugging)
      5000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskWebSocketHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);
}

void stopTaskWebSocketServer(){
  if(taskWebSocketHandle != NULL){
    vTaskDelete(taskWebSocketHandle);
  }
}

void startTaskWebServer(){
  xTaskCreatePinnedToCore(
      taskWebServer,  // Function that should be called
      "Web Server",   // Name of the task (for debugging)
      5000,           // Stack size (bytes)
      NULL,           // Parameter to pass
      1,              // Task priority
      &taskWebHandle, // Task handle
      CONFIG_ARDUINO_RUNNING_CORE);
}

void stopTaskWebServer(){
  if(taskWebHandle != NULL){
    vTaskDelete(taskWebHandle);
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
    logln("Task clock waiting for wifi...");
    delay(5000);
  }
  
  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  while(!getLocalTime(&timeinfo)){
    logln("Clock : Failed to obtain time");
    delay(2000);
  }

  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  // log("Day of week: ");
  // logln(&timeinfo, "%A");
  // log("Month: ");
  // logln(&timeinfo, "%B");
  // log("Day of Month: ");
  // logln(&timeinfo, "%d");
  // log("Year: ");
  // logln(&timeinfo, "%Y");
  // log("Hour: ");
  // logln(&timeinfo, "%H");
  // log("Hour (12 hour format): ");
  // logln(&timeinfo, "%I");
  // log("Minute: ");
  // logln(&timeinfo, "%M");
  // log("Second: ");
  // logln(&timeinfo, "%S");

  // strftime(timeDay,3, "%d", &timeinfo);
  // strftime(timeMonth,10, "%B", &timeinfo);
  // strftime(timeYear,5, "%Y", &timeinfo);

  String type="AM";

  h24 = timeinfo.tm_hour; // 24 hours
  h = timeinfo.tm_hour > 12 ? timeinfo.tm_hour-12 : timeinfo.tm_hour;
  m = timeinfo.tm_min;
  s = timeinfo.tm_sec;

  logf("Clock stack size : %d",uxTaskGetStackHighWaterMark(NULL));

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


    // log("Time : ");
    // log(timeinfo.tm_hour);
    // log(":");
    // log(m);
    // log(":");
    // logln(s);
    sprintf_P(str_clock_full, (PGM_P)F("%02d:%02d:%02d"), h24, m, s);
    isClockReady = true;
    //logln(str_clock);
  }
}


const char * getJsonData(const char * link){

}

bool isKabisat(int year){
  bool isKabisat = false;
  if(year % 4 == 0){
      if(year % 100 == 0){
        if(year % 400 == 0){
          isKabisat = true;
        } else {
          isKabisat = false;
        }
      } else {
        isKabisat = true;
      }
  } else {
    isKabisat = false;
  }
  return isKabisat;
}

//===========================================================================
//==================================   Task Masehi Date & Hijri Date  =======
//===========================================================================
void taskDate(void * parameter)
{
  isDateReady = false;
  for (;;)
  {
    xSemaphoreTake(mutex_con, portMAX_DELAY);
    { 
      bool isMasehiOfflineMode = false;
      bool isHijriOfflineMode = false;
      if(isWiFiReady)
      {
        //ONLINE MODE
        //get masehi date
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        struct tm timeinfo;
        if(!getLocalTime(&timeinfo)){
          logln("Date : Failed to obtain time");
          isMasehiOfflineMode = true;
          isHijriOfflineMode = true;
        } else {
          Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

          day = timeinfo.tm_mday;
          month = timeinfo.tm_mon; //0-11 since januari
          year = timeinfo.tm_year+1900;
          weekday = timeinfo.tm_wday;//0-6 since sunday

          //get hijri date
          char link[140] = {'\0'};
          sprintf_P(link, (PGM_P)F("https://www.al-habib.info/utils/calendar/pengubah-kalender-hijriyah-v7.php?the_y=%04d&the_m=%02d&the_d=%02d&the_conv=ctoh&lg=1"), year, month+1, day);
          logln(link);

          WiFiClientSecure client;
          HTTPClient http;
          client.setInsecure();

          // Your Domain name with URL path or IP address with path
          http.begin(client, link);
          int httpResponseCode = http.GET();

          if (httpResponseCode==200) {
            logf("Date HTTP Response code: %d",httpResponseCode);
            String jsonData = http.getString();

            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, jsonData);

            if (error) {
              log("Date deserializeJson() failed: ");
              logln(reinterpret_cast<const char *>(error.f_str()));
              isHijriOfflineMode = true;
            } else {
              const char * hijri_date = doc["tanggal_hijriyah"];
              sprintf_P(str_hijri_date, (PGM_P)F("%s"), hijri_date);
              hijri_day = doc["hijri_tanggal"];
              hijri_month = doc["hijri_bulan"];
              hijri_year = doc["hijri_tahun"];
            }
            
            doc.clear();
          } else {
            logf("Date Error code: %d",httpResponseCode);
            isHijriOfflineMode = true;
          }

          // Free resources
          http.end();
        }
      }

      if(day>-1 && hijri_day>-1){
        isDateReady = true;
      }

      if(isDateReady){
        if(isMasehiOfflineMode){
          //MASEHI OFFLINE MODE
          //31: 0,2,4,6,7,9,11
          //30: 3,5,8,10,12
          //28/29: 1
          day++;
          if(day>=29){
            if(month==1){
              if(isKabisat(year)){
                if(day>29){
                  day = 1;
                  month++;
                }
              } else {
                if(day>28){
                  day = 1;
                  month++;
                }
              }
            } else if(month==3 || month==5 || month==8 || month==10 || month==12){
              if(day>30){
                day = 1;
                month++;
              }
            } else if(month==0 || month==2 || month==4 || month==6 || month==7 || month==9 || month==11){
              if(day>31){
                day = 1;
                month++;
              }
            }
          }

          if(month>11){
            month = 0;
            year++;
          }

          weekday++;
          if(weekday>=7){
            weekday=0;
          }   
        }

        if(isHijriOfflineMode){
          //HIJRI OFFLINE MODE
          //dont adjust here
        }

        //calculation
        String day_names[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jum'at", "Sabtu"};
        String month_names[] = {"Januari", "Februari", "Maret", "April", "Mei", "Juni", "Juli", "Agustus", "September", "Oktober", "November", "Desember"};

        memset(str_date,'\0',sizeof(char)*26);
        sprintf_P(str_date, (PGM_P)F("%s, %02d %s %02d"), day_names[weekday].c_str(), day, month_names[month].c_str(),year);
        sprintf_P(str_date_full, (PGM_P)F("%s / %s"), str_date, str_hijri_date);

        if(weekday == 0){
          setupDMDAtExactRangeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Besok adalah puasa hari senin, silakan dipersiapkan semuanya",false,"Info PUASA", false, System5x7,1000,5000,0,"09:00:00",0,"23:59:00");
        } else if(weekday == 3){
          setupDMDAtExactRangeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Besok adalah puasa hari kamis, silakan dipersiapkan semuanya",false,"Info PUASA", false,  System5x7,1000,5000,0,"09:00:00",0,"23:59:00");
        } else if(weekday == 4){
          setupDMDAtExactRangeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Waktunya Al Kahfi, Sholawat Nabi, Doa penghujung jumat",false,"Ayo Ayo ayo", false,  System5x7,1000,5000,0,"18:30:00",1,"17:30:00");
        } else if(weekday == 5){
          setupDMDAtExactRangeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Waktunya Al Kahfi, Sholawat Nabi, Doa penghujung jumat",false,"Ayo Ayo ayo", false,  System5x7,1000,5000,0,"00:01:00",0,"17:30:00");
        }

        if(hijri_day == 12 || hijri_day == 13 || hijri_day == 14){
          setupDMDAtExactRangeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Besok adalah puasa ayyamul bidh, silakan dipersiapkan semuanya",false,"Info PUASA", false,  System5x7,1000,5000,0,"09:00:00",0,"23:59:00");
        }
      }

      logf("Date stack size : %d", uxTaskGetStackHighWaterMark(NULL));
    }
    xSemaphoreGive(mutex_con);
    if(!isDateReady){
      logln("Task date waiting for wifi...");
      delay(180000); //3 minutes
    } else {
      delayMSUntilAtTime(0,1,0);
    }
  }
}

void startTaskCountdownJWS();
void stopTaskCountdownJWS();

//=========================================================================
//==================================  Task Jadwal Sholat =================
//=========================================================================
void taskJadwalSholat(void * parameter){
  for(;;){
    if(!isDateReady){
      logln("Task JWS waiting for date...");
      delay(10000);
      continue;
    }
    bool isFetchSuccess = false;
    xSemaphoreTake(mutex_con, portMAX_DELAY);
    { 
      char link[100] = {'\0'};
      sprintf_P(link, (PGM_P)F("https://api.myquran.com/v1/sholat/jadwal/1301/%02d/%02d/%02d"), year, month+1, day);
      logln(link);
      
      WiFiClientSecure client;
      HTTPClient http;
      client.setInsecure();

      // Your Domain name with URL path or IP address with path
      http.begin(client, link);
      int httpResponseCode = http.GET();
      
      if (httpResponseCode==200) {
        logf("JWS HTTP Response code: %d",httpResponseCode);
        String jsonData = http.getString();
    
        DynamicJsonDocument doc(768);
        DeserializationError error = deserializeJson(doc, jsonData);

        if (error) {
          log("JWS deserializeJson() failed: ");
          logln(reinterpret_cast<const char *>(error.f_str()));
          delay(20000);
        } else {
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

          isJWSReady = true;
          isFetchSuccess = true;
          
          log("Subuh : ");
          logln(data_jadwal_subuh);
          log("Syuruk : ");
          logln(data_jadwal_syuruk);
          log("Dhuha : ");
          logln(data_jadwal_dhuha);
          log("Dzuhur : ");
          logln(data_jadwal_dzuhur);
          log("Ashar : ");
          logln(data_jadwal_ashar);
          log("Magrib : ");
          logln(data_jadwal_maghrib);   
          log("Isya : ");
          logln(data_jadwal_isya); 
        }
        doc.clear();
      } else {
        logf("JWS Error code: %d",httpResponseCode);
      }

      // Free resources
      http.end();
    }
    xSemaphoreGive(mutex_con);
    logf("JWS stack size : %d",uxTaskGetStackHighWaterMark(NULL));

    if(isFetchSuccess){
      stopTaskCountdownJWS();
      startTaskCountdownJWS();
      delayMSUntilAtTime(0,30,0);
    } else {
      delay(180000); //3 minutes
    }
  }
}


#define ALERT_COUNTUP_SHOLAT 5*60*1000/*5 menit*/
#define ALERT_COUNTDOWN_DZIKIR 1*60*1000/*5 menit*/


void updateHijriForFirstHalfNight(){
    //it's time to update hijri date
    if(hijri_day+1 <= 29){
      sprintf_P(str_hijri_date, (PGM_P)F("%d%s"), hijri_day+1,(hijri_day >= 10 ? str_hijri_date+2 : str_hijri_date+1));     
      log("New Hijri Date :");
      logln(str_hijri_date);
      sprintf_P(str_date_full, (PGM_P)F("%s / %s"), str_date, str_hijri_date);
    }
}

void taskCountDownJWS(void * parameter){
  for(;;){
    if(!isJWSReady){
      logln("Task countdown-jws waiting for jws...");
      delay(10000);
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

      if(clock[3] >= isya[3] && clock[3] <=86400){
        updateHijriForFirstHalfNight();
      }

    } else if(clock[3] < syuruk[3]){
      sprintf_P(type_jws, (PGM_P)F("syuruk"));
      counter = syuruk[3] - clock[3];

      //it's time to dzikir in the morning
      setupDMDAtNowForLifeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Dzikir Pagi",false,count_down_jws,false,System5x7,1000,ALERT_COUNTDOWN_DZIKIR,msDistanceFromNowToTime(syuruk[0], syuruk[1], syuruk[2]));
      //resetDMDLoopIndex();
    } else if(clock[3] < dhuha[3]){
      sprintf_P(type_jws, (PGM_P)F("dhuha"));
      counter = dhuha[3] - clock[3];

      //it's time to sholat dhuha
      setupDMDAtNowForLifeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Waktu Sholat Dhuha",false,str_clock_full,false,System5x7,1000,10000,(dzuhur[3]-dhuha[3]-(15*60))*1000);
      //resetDMDLoopIndex();
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
      setupDMDAtNowForLifeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Dzikir Petang",false,count_down_jws,false,System5x7,1000,ALERT_COUNTDOWN_DZIKIR,msDistanceFromNowToTime(maghrib[0], maghrib[1], maghrib[2]));
      //resetDMDLoopIndex();
      if(weekday == 5){
        setupDMDAtNowForLifeTime(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,"Doa di akhir hari jumat",false,count_down_jws,false,System5x7,1000,ALERT_COUNTDOWN_DZIKIR,msDistanceFromNowToTime(maghrib[0], maghrib[1], maghrib[2]));
      }
    } else if(clock[3] < isya[3]){
      sprintf_P(type_jws, (PGM_P)F("isya"));
      counter = isya[3] - clock[3];

      updateHijriForFirstHalfNight();
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

    log("Counter Countdown for ");
    log(type_jws);
    logf(" : %d ==> %d - %d - %d",counter,hours,minutes,seconds);


    logf("Countdown JWS stack size : %d",uxTaskGetStackHighWaterMark(NULL));
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
      //log("String Countdown : ");
      //log(type_jws);
      //log(" : ");
      //logln(count_down_jws);
      seconds--;
      counter--;
      delay(1000);
    }

    //show alert
    char count_sholat_alert[30] = {0};
    sprintf_P(count_sholat_alert, (PGM_P)F("Sudah masuk waktu %s"), type_jws);
    setupDMDAtNowForIteration(true,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,getAllocatedString(count_sholat_alert),true,str_clock_full,false,System5x7,1000,ALERT_COUNTUP_SHOLAT,5);
    resetDMDLoopIndex();
    delay(5000);
  }
}

void startTaskCountdownJWS(){
  xTaskCreatePinnedToCore(
        taskCountDownJWS,  // Function that should be called
        "Countdown Jadwal Sholat",   // Name of the task (for debugging)
        4500,           // Stack size (bytes)
        NULL,           // Parameter to pass
        1,              // Task priority
        &taskCountdownJWSHandle, // Task handle
        CONFIG_ARDUINO_RUNNING_CORE);
}

void stopTaskCountdownJWS(){
  if(taskCountdownJWSHandle != NULL){
    vTaskDelete(taskCountdownJWSHandle);
  }
}

//=========================================================================
//==================================   Task Firebase Scheduler  ===========
//=========================================================================

#define NASEHAT_COUNT_MAX 10

boolean appendFile(const char * text, const char * fileName, boolean overWrite){
  size_t result = 0;
  File file;
  if(fileName != NULL){
    if(!SPIFFS.exists(fileName)){
      file = SPIFFS.open(fileName, FILE_WRITE);
    } else {
      if(overWrite){
        SPIFFS.remove(fileName);
        file = SPIFFS.open(fileName, FILE_WRITE);
      } else {
        file = SPIFFS.open(fileName, FILE_APPEND);
      }
    }
    if(file){
      if(text != NULL){
        result = file.println(text);
      }
      file.close();
    }
  }
  return result;
}

std::array<String,NASEHAT_COUNT_MAX> readFile(const char * fileName){
  File file;
  std::array<String,NASEHAT_COUNT_MAX> stringResult;
  if(fileName != NULL){
    if(SPIFFS.exists(fileName)){
      file = SPIFFS.open(fileName, FILE_READ);
      if(file){
        int count = 0;
        while(file.available() && count < NASEHAT_COUNT_MAX){
          String line = file.readStringUntil('\n');
          stringResult[count] = line;
          count++;
        }
        file.close();
      }
    }
  }
  return stringResult;
}


void setupDMDNasehat(const char * info){
  setupDMDAtNowForLifeTime(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL,info,true,"",false,Arial_Black_16,1000,10000,msDistanceFromNowToTime(23, 59, 0));
  setupDMDAtNowForLifeTime(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,str_date_full,false,str_clock_full,false,System5x7,1000,10000, msDistanceFromNowToTime(23, 59, 0));
  setupDMDAtNowForLifeTime(false,DMD_DATA_FREE_INDEX,DMD_TYPE_SCROLL_STATIC,type_jws,false,count_down_jws,false,System5x7,1000,10000, msDistanceFromNowToTime(23, 59, 0));
}

// Your Firebase Project Web API Key
#define FB_API_KEY "AIzaSyAQ1OoPvV3_235dZPvZKW5CN-8pQghjJbQ"
// Your Firebase Realtime database URL
#define FB_DATABASE_URL "https://custom-speaker-murottal-default-rtdb.asia-southeast1.firebasedatabase.app/"
void taskFirebase(void * parameter){
  FirebaseData fbdo;
  FirebaseAuth auth;
  FirebaseConfig config;
  std::array<String,10> nasehatVector;
  String nasehatListPath = "/app/nasehat/list";
  String fuid = ""; 
  bool isAuthenticated = false;
  config.api_key = FB_API_KEY;
  config.database_url = FB_DATABASE_URL;

  Firebase.enableClassicRequest(fbdo, true);
  fbdo.setResponseSize(8192); //minimum size is 4096 bytes
  logln("------------------------------------");
  logln("Firebase Sign up new user...");
  xSemaphoreTake(mutex_con, portMAX_DELAY); 
  // Sign in to firebase Anonymously
  if (Firebase.signUp(&config, &auth, "", "")){
    logln("Firebase signup Success");
    isAuthenticated = true;
    fuid = auth.token.uid.c_str();
  }
  else
  {
    logf("Firebase signup Failed, %s\n", config.signer.signupError.message.c_str());
    isAuthenticated = false;
  }

  // Assign the user sign in credentials
  //auth.user.email = "ahsai001@gmail.com";
  //auth.user.password = "123456ytrewq";
  //isAuthenticated = true;

  // Assign the callback function for the long running token generation task, see addons/TokenHelper.h
  config.token_status_callback = tokenStatusCallback;
  //config.signer.tokens.legacy_token = "kNVt4A1fFWNFifHbGMYqPR9hVwL4DE9S1Nyik9iG";

  // Initialise the firebase library
  Firebase.begin(&config, &auth);
  xSemaphoreGive(mutex_con);

  //Firebase.reconnectWiFi(true);
  logf("Firebase stack size : %d",uxTaskGetStackHighWaterMark(NULL));
  //int test = 0;
  for(;;){
    xSemaphoreTake(mutex_con, portMAX_DELAY); 
    {
      boolean isFbReady = Firebase.ready();
      logf("Firebase ready or not ? %d",isFbReady);
      if (isAuthenticated && isFbReady){
          logln("------------------------------------");
          logln("Firebase get data...");

          if(Firebase.getArray(fbdo, nasehatListPath)){
            FirebaseJsonArray fbja = fbdo.jsonArray();
            //appendFile(NULL,"/nasehat_firebase.txt",true);
            for (size_t i = 0; i < fbja.size(); i++){
              FirebaseJsonData result;
              //result now used as temporary object to get the parse results
              fbja.get(result, i);

              //Print its value
              logf("Array index: %d, type: %d, value: %s",i,result.type,result.to<String>().c_str());

              const char * info = getAllocatedString(result.to<String>());
              setupDMDNasehat(info);

              //appendFile(info,"/nasehat_firebase.txt",false);
            }
            isFirebaseReady = true;
            logln("Firebase get process...");
          } else {
            isFbReady = false;
          }
          logln("Firebase done data...");
      }
      
      // if(!isFbReady && isFirebaseReady){
      //   nasehatVector = readFile("/nasehat_firebase.txt");
      //   for(int x=0; x<NASEHAT_COUNT_MAX; x++){
      //     String info = nasehatVector.at(x);
      //     Serial.println(info);
      //     //setupDMDNasehat(info.c_str());
      //   }
      // }
    }
    xSemaphoreGive(mutex_con);
    delayMSUntilAtTime(1,20,0);
  }
}

//=========================================================================
//==================================   Task Button / Touch Handle  ========
//=========================================================================
void taskButtonTouch(void * parameter){
  
  logf("Button Touch stack size : %d",uxTaskGetStackHighWaterMark(NULL));
  for(;;){
    uint16_t touchValue = touchRead(33);
    bool isTouched = touchValue < 20;
    //log("Touch Value : ");
    //logln(touchValue);
    if(isTouched){
      //remove ssid & password in preferences setting
      //preferences.remove("ssid");
      //preferences.remove("password");
      logln("Restarting after remove wifi credential");
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
      log("FILE: ");
      logln(file.name());
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

  mutex_con = xSemaphoreCreateMutex(); 
  if (mutex_con == NULL) { 
    logln("Mutex con can not be created"); 
  }

  mutex_dmd = xSemaphoreCreateMutex(); 
  if (mutex_dmd == NULL) { 
    logln("Mutex dmd can not be created"); 
  } 

 
  preferences.begin("settings", false);
  //ssid = preferences.getString("ssid","3mbd3vk1d-2");
  //password = preferences.getString("password","marsupiarmadomah3716");
  ssid = preferences.getString("ssid","");
  password = preferences.getString("password","");

  while(!SPIFFS.begin(true)){
    logln("An Error has occurred while mounting SPIFFS");
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
      logln("speaker-murottal.local is available");
    }
    startTaskToggleLED();
    delay(5000);

    startTaskWebServer();
    delay(5000);

    startTaskWebSocketServer();
    delay(5000);

    startTaskDMD();
    delay(6000);
  } else {
    xTaskCreatePinnedToCore(
        taskKeepWiFiAlive,  // Function that should be called
        "Keep WiFi Alive",  // Name of the task (for debugging)
        3200,               // Stack size (bytes)
        NULL,               // Parameter to pass
        1,                  // Task priority
        &taskKeepWiFiHandle, // Task handle
        CONFIG_ARDUINO_RUNNING_CORE
    );
    delay(5000);

    startTaskWebServer();
    delay(5000);

    startTaskWebSocketServer();
    delay(5000);

    xTaskCreatePinnedToCore(
        taskClock,  // Function that should be called
        "Clock",   // Name of the task (for debugging)
        3400,           // Stack size (bytes)
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
        5500,           // Stack size (bytes)
        NULL,           // Parameter to pass
        1,              // Task priority
        &taskJWSHandle, // Task handle
        CONFIG_ARDUINO_RUNNING_CORE);
    delay(5000);

    startTaskDMD();
    delay(10000);

    xTaskCreatePinnedToCore(
        taskFirebase,        // Function that should be called
        "Firebase",  // Name of the task (for debugging)
        65000,           // Stack size (bytes)
        NULL,           // Parameter to pass
        1,              // Task priority
        &taskFirebaseHandle, // Task handle
        CONFIG_ARDUINO_RUNNING_CORE);
      delay(5000);
  }

  //vTaskDelete(NULL);
}

void loop(){
  //do nothing, everything is doing in task
}