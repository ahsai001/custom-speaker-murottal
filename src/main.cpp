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


//=========================================================================
//============================= Declarations ==============================
//=========================================================================

const char *ssid = "3mbd3vk1d-2";
const char *password = "marsupiarmadomah3716";

//section code : DMD, toggle led, wifi alive, web server
TaskHandle_t taskLEDHandle;
TaskHandle_t taskWebHandle;
TaskHandle_t taskKeepWiFiHandle;
TaskHandle_t taskDMDHandle;
TaskHandle_t taskClockHandle;
TaskHandle_t taskJWSHandle;

int h24 = 12;
int h = 12;
int m = 0;
int s = 0; 
char str_clock[9];
char timeDay[3];
char timeMonth[10];
char timeYear[4];

const char * ntpServer = "pool.ntp.org";
const uint8_t timezone = 7;
const long  gmtOffset_sec = timezone*3600;
const int   daylightOffset_sec = 0;

//=========================================================================
//==================================   Task DMD  ==========================
//=========================================================================
#define DISPLAYS_ACROSS 2
#define DISPLAYS_DOWN 1
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

hw_timer_t *timer = NULL;
String scrollingText = "Assalamu'alaikum";

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
}


void marqueeText(const char * text){
  dmd.drawMarquee(text, strlen(text), (32 * DISPLAYS_ACROSS) - 1, 1);
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
  dmd.selectFont(Arial_Black_16);
  marqueeText(scrollingText.c_str());
  marqueeText("Developed by AhsaiLabs");

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

void drawTextCenter(const uint8_t * font, const char * str){
    unsigned int length = stringWidth(font, str);
    float posX = ((32 * DISPLAYS_ACROSS) - length)/2;
    dmd.drawString(posX, 5, str, strlen(str), GRAPHICS_NORMAL);
}

void taskDMD(void *parameter)
{
  setupDMD();
  for (;;)
  {
    //byte b;
    Serial.println("DMD is coming");
    // 10 x 14 font clock, including demo of OR and NOR modes for pixels so that the flashing colon can be overlayed
    //dmd.drawBox(0, 0, (32 * DISPLAYS_ACROSS) - 1, (16 * DISPLAYS_DOWN) - 1, GRAPHICS_TOGGLE);
    drawTextCenter(System5x7, str_clock);
    delay(1000);
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
const uint8_t built_in_led = 2;
const uint8_t relay = 26;
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
void taskClock(void * parameter)
{
  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
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
  strftime(timeYear,4, "%Y", &timeinfo);

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
    sprintf_P(str_clock, (PGM_P)F("%02d:%02d:%02d"), h, m, s);
    //Serial.println(str_clock);
  }
}

String httpGETRequest(const char* serverName) {
  WiFiClientSecure client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  http.begin(client, serverName);
  
  // Send HTTP POST request
  int httpResponseCode = http.GET();
  
  String payload = "{}"; 
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

void taskJadwalSholat(void * parameter){
  char link[100];
  sprintf_P(link, (PGM_P)F("https://api.myquran.com/v1/sholat/jadwal/1301/%d/%d/%d"), timeYear, timeMonth, timeDay);
  
  String jsonData = httpGETRequest(link);
  Serial.println(jsonData);
  vTaskDelete(NULL);
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