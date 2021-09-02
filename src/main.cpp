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
#include <DMD32.h>        //
#include "fonts/SystemFont5x7.h"
#include "fonts/Arial_black_16.h"

const char * ssid = "3mbd3vk1d-2";
const char * password = "marsupiarmadomah3716";

//section code : DMD, toggle led, web server
TaskHandle_t taskLEDHandle = NULL;
TaskHandle_t taskWebHandle = NULL;
TaskHandle_t taskKeepWiFiHandle = NULL;
TaskHandle_t taskDMDHandle = NULL;

#define DISPLAYS_ACROSS 2
#define DISPLAYS_DOWN 1
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

hw_timer_t * timer = NULL;

String scrollingText = "Assalamu'alaikum";

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
}

void taskDMD(void * parameter){
   uint8_t cpuClock = ESP.getCpuFreqMHz();
   timer = timerBegin(0, cpuClock, true);
   timerAttachInterrupt(timer, &triggerScan, true);
   timerAlarmWrite(timer, 300, true);
   timerAlarmEnable(timer);
   dmd.clearScreen( true );   //true is normal (all pixels off), false is negative (all pixels on)

    //control brightness DMD
   ledcSetup(0, 5000, 8);
   ledcAttachPin(4, 0);
   ledcWrite(0, 30);

   for(;;){
      byte b;
   
      // 10 x 14 font clock, including demo of OR and NOR modes for pixels so that the flashing colon can be overlayed
      dmd.clearScreen( true );
      dmd.selectFont(Arial_Black_16);
      dmd.drawChar(  0,  3, '2', GRAPHICS_NORMAL );
      dmd.drawChar(  7,  3, '3', GRAPHICS_NORMAL );
      dmd.drawChar( 17,  3, '4', GRAPHICS_NORMAL );
      dmd.drawChar( 25,  3, '5', GRAPHICS_NORMAL );
      dmd.drawChar( 15,  3, ':', GRAPHICS_OR     );   // clock colon overlay on
      delay( 1000 );
      dmd.drawChar( 15,  3, ':', GRAPHICS_NOR    );   // clock colon overlay off
      delay( 1000 );
      dmd.drawChar( 15,  3, ':', GRAPHICS_OR     );   // clock colon overlay on
      delay( 1000 );
      dmd.drawChar( 15,  3, ':', GRAPHICS_NOR    );   // clock colon overlay off
      delay( 1000 );
      dmd.drawChar( 15,  3, ':', GRAPHICS_OR     );   // clock colon overlay on
      delay( 1000 );

      dmd.drawMarquee(scrollingText.c_str(),14,(32*DISPLAYS_ACROSS)-1,0);
      long start=millis();
      long timer=start;
      boolean ret=false;
      while(!ret){
        if ((timer+30) < millis()) {
          ret=dmd.stepMarquee(-1,0);
          timer=millis();
        }
      }
      // half the pixels on
      dmd.drawTestPattern( PATTERN_ALT_0 );
      delay( 1000 );

      // the other half on
      dmd.drawTestPattern( PATTERN_ALT_1 );
      delay( 1000 );
      
      // display some text
      dmd.clearScreen( true );
      dmd.selectFont(System5x7);
      for (byte x=0;x<DISPLAYS_ACROSS;x++) {
        for (byte y=0;y<DISPLAYS_DOWN;y++) {
          dmd.drawString(  2+(32*x),  1+(16*y), "freet", 5, GRAPHICS_NORMAL );
          dmd.drawString(  2+(32*x),  9+(16*y), "ronic", 5, GRAPHICS_NORMAL );
        }
      }
      delay( 2000 );
      
      // draw a border rectangle around the outside of the display
      dmd.clearScreen( true );
      dmd.drawBox(  0,  0, (32*DISPLAYS_ACROSS)-1, (16*DISPLAYS_DOWN)-1, GRAPHICS_NORMAL );
      delay( 1000 );
      
      for (byte y=0;y<DISPLAYS_DOWN;y++) {
        for (byte x=0;x<DISPLAYS_ACROSS;x++) {
          // draw an X
          int ix=32*x;
          int iy=16*y;
          dmd.drawLine(  0+ix,  0+iy, 11+ix, 15+iy, GRAPHICS_NORMAL );
          dmd.drawLine(  0+ix, 15+iy, 11+ix,  0+iy, GRAPHICS_NORMAL );
          delay( 1000 );
      
          // draw a circle
          dmd.drawCircle( 16+ix,  8+iy,  5, GRAPHICS_NORMAL );
          delay( 1000 );
      
          // draw a filled box
          dmd.drawFilledBox( 24+ix, 3+iy, 29+ix, 13+iy, GRAPHICS_NORMAL );
          delay( 1000 );
        }
      }

      // stripe chaser
      for( b = 0 ; b < 20 ; b++ )
      {
          dmd.drawTestPattern( (b&1)+PATTERN_STRIPE_0 );
          delay( 200 );      
      }
      delay( 200 );  
   }
}

#define WIFI_TIMEOUT_MS 20000 // 20 second WiFi connection timeout
#define WIFI_RECOVER_TIME_MS 30000 // Wait 30 seconds after a failed connection attempt

void taskKeepWiFiAlive(void * parameter){
    for(;;){
        if(WiFi.status() == WL_CONNECTED){
            delay(10000);
            continue;
        }

        Serial.println("[WIFI] Connecting");
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        unsigned long startAttemptTime = millis();

        // Keep looping while we're not connected and haven't reached the timeout
        while (WiFi.status() != WL_CONNECTED && 
                millis() - startAttemptTime < WIFI_TIMEOUT_MS){}

        // When we couldn't make a WiFi connection (or the timeout expired)
		  // sleep for a while and then retry.
        if(WiFi.status() != WL_CONNECTED){
            Serial.println("[WIFI] FAILED");
            delay(WIFI_RECOVER_TIME_MS);
			      continue;
        }

        Serial.print("Connected to ");
        Serial.println(ssid);
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        if (MDNS.begin("esp32")) {
          Serial.println("MDNS responder started");
        }
    }
}

const uint8_t built_in_led = 2;
const uint8_t relay = 26;

void taskToggleLED(void * parameter){
  for(;;){
    digitalWrite(built_in_led, HIGH);
    delay(1000);
    digitalWrite(built_in_led, LOW);
    delay(1000);
  }
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
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
  <input type="range" min="0" max="255" class="slider" id="brightnessSlider" onchange="brightnessChange(this.value)"/>
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

void handleWebRoot() {
  digitalWrite(built_in_led, 1);
  server.send(200, "text/plain", "hello world");
  digitalWrite(built_in_led, 0);
}

void handleWebNotFound() {
  digitalWrite(built_in_led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(built_in_led, 0);
}

void handleServerClient(){
    server.handleClient();
    delay(200);
}

void taskWebServer(void * parameter){
  server.on("/", handleWebRoot);

  server.on("/check", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.on("/setting", []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/get-setting", []() {
    String scrolltext = server.arg("scrolltext");
    scrollingText = scrolltext;
    server.sendHeader("Location", "/setting",true);
    server.send(302, "text/plain","");
  });

  server.on("/brightness", []() {
    String level = server.arg("level");
    ledcWrite(0, level.toInt());
    server.send(404, "text/plain", "ubah brigtness berhasil");
  });

  server.on("/on", []() {
    //digitalWrite(built_in_led, 1);
    //digitalWrite(relay, 1);
    server.send(200, "text/plain", "relay on");
  });

  server.on("/off", []() {
    //digitalWrite(built_in_led, 0);
    //digitalWrite(relay, 0);
    server.send(200, "text/plain", "relay off");
  });

  server.onNotFound(handleWebNotFound);

  server.begin();
  Serial.println("HTTP server started");
      
  // for(;;){
  //    handleServerClient();
  // }
}



void setup() {
  pinMode(built_in_led, OUTPUT);
  Serial.begin(115200);
  
  //task 1
  xTaskCreate(
    taskToggleLED,    // Function that should be called
    "Toggle LED",   // Name of the task (for debugging)
    1000,            // Stack size (bytes)
    NULL,            // Parameter to pass
    1,               // Task priority
    &taskLEDHandle             // Task handle
  );

  

  xTaskCreate(
    taskDMD,    // Function that should be called
    "Display DMD",   // Name of the task (for debugging)
    1000,            // Stack size (bytes)
    NULL,            // Parameter to pass
    1,               // Task priority
    &taskDMDHandle             // Task handle
  );



  xTaskCreate(
    taskKeepWiFiAlive,    // Function that should be called
    "Keep WiFi Alive",   // Name of the task (for debugging)
    1000,            // Stack size (bytes)
    NULL,            // Parameter to pass
    1,               // Task priority
    &taskKeepWiFiHandle             // Task handle
  );

  //task 2
  // xTaskCreate(
  //   taskWebServer,    // Function that should be called
  //   "Web Server",   // Name of the task (for debugging)
  //   1000,            // Stack size (bytes)
  //   NULL,            // Parameter to pass
  //   1,               // Task priority
  //   &taskWebHandle             // Task handle
  // );
  //int x;
  //taskWebServer(&x);
}

void loop() {
  //handleServerClient();
}