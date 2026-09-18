// ESP32-S3 UART1 communication with Arduino
// UART1 pins: RX = GPIO18
// Use COM12 to upload sketch changes.
// Use COM12 or COM11 to monitor the serial data from the Arduino sensors
#include <WiFi.h>
#include <WebServer.h>
#include "S3_secrets.h"

WebServer server(80);

float temp = 0;
float rawTemp = 0;
float hum = 0;
float dewpt = 0.0;
float press = 0;
float tmin = 0;
float tmax = 0;
String timeStr = "";
float windspeedmph = 0.0;
float windgustmph = 0.0;
int winddir = 0;
float windDirSumX = 0.0;
float windDirSumY = 0.0;
uint16_t windDirSamples = 0;
int avgWindDirDeg = 0;
float rainin = 0.0;
float rainRate = 0.0;
float dailyrainin = 0.0;

unsigned long lastWU = 0;
unsigned long lastSuccessfulWU = 0;

// Buffers
String incomingLine = "";
String latestWeather = "";
String WIFI_IP = "";
String nextUploadTime = "Unknown";
String rainMessage = "";

const int suppressStartHour = 16;
const int suppressStartMinute = 0;
const int suppressEndHour = 19;
const int suppressEndMinute = 0;

float tempAdjFactor = 1.0;

bool suppressingTemp = false;   // MEGA controls suppression
bool haveValidData = false;
bool firstUploadDone = false;

String pad2(int num) {
  if (num < 10) return "0" + String(num);
  return String(num);
}

void parseWeather(String line) {
  int tPos = line.indexOf("TEMP:");
  int hPos = line.indexOf("HUM:");
  int dewPos = line.indexOf("DEW:");
  int pPos = line.indexOf("PRESS:");
  int nPos = line.indexOf("TMIN:");
  int xPos = line.indexOf("TMAX:");
  int timePos = line.indexOf("TIME:");
  int wsPos = line.indexOf("WINDSPD:");
  int wdPos = line.indexOf("WINDDIR:");
  int rPos  = line.indexOf("RAIN:");
  int rrPos = line.indexOf("RAINRATE:");
  int drPos = line.indexOf("DAILYRAIN:");
  int gPos = line.indexOf("GUST:");
  int sPos = line.indexOf("SUPP:");

  if (dewPos >= 0) {
    dewpt = line.substring(dewPos + 4, line.indexOf(",", dewPos)).toFloat();
  }
    
  if (sPos >= 0) {
      suppressingTemp = line.substring(sPos + 5).toInt() == 1;
  }


  // ----- TIME -----
  String rawTime = line.substring(timePos + 5);  // "7:3:9"
  int first = rawTime.indexOf(':');
  int second = rawTime.indexOf(':', first + 1);

  int hh = rawTime.substring(0, first).toInt();
  int mm = rawTime.substring(first + 1, second).toInt();
  int ss = rawTime.substring(second + 1).toInt();

  timeStr = pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss);

    temp  = line.substring(tPos + 5, line.indexOf(",", tPos)).toFloat();
    rawTemp = temp;

    // During suppressing hours, gradually reduce the reported
    // temperature. Small adjustment at the beginning, maximum
    // adjustment in the middle, then taper back to normal.
    if (suppressingTemp)
    {
        int startMin = suppressStartHour * 60 + suppressStartMinute;
        int endMin   = suppressEndHour * 60 + suppressEndMinute;
        int nowMin   = hh * 60 + mm;

        // 0.0 at start, 1.0 at end
        float progress =
            (float)(nowMin - startMin) /
            (float)(endMin - startMin);

        // Maximum reduction is 1.5%
        float peakReduction = 0.015;

        // Creates a smooth hump:
        // start = 0%
        // middle = 1.5%
        // end = 0%
        float reduction =
            peakReduction * sin(progress * PI);

        tempAdjFactor = 1.0 - reduction;
    }
    else
    {
        tempAdjFactor = 1.0;
    }

    temp *= tempAdjFactor;

  hum   = line.substring(hPos + 4, line.indexOf(",", hPos)).toFloat();
  press = line.substring(pPos + 6, line.indexOf(",", pPos)).toFloat();
  tmin  = line.substring(nPos + 5, line.indexOf(",", nPos)).toFloat();
  tmax  = line.substring(xPos + 5, line.indexOf(",", xPos)).toFloat();

  if (dewpt < 40) {
      rainMessage = "Air is bone-dry. Rain is virtually impossible.";
  }
  else if (dewpt < 50) {
      rainMessage = "Moisture very low. Rain highly unlikely.";
  }
  else if (dewpt < 55) {
      rainMessage = "Moisture insufficient for rain unless strong lift arrives.";
  }
  else if (dewpt < 60) {
      rainMessage = "Rain possible if a front or disturbance arrives.";
  }
  else if (dewpt < 65) {
      rainMessage = "Moisture supportive of rain with typical lift.";
  }
  else if (dewpt < 70) {
      rainMessage = "High moisture — rain likely with any disturbance.";
  }
  else if (dewpt < 75) {
      rainMessage = "Very high moisture — rain or storms likely.";
  }
  else {
      rainMessage = "Extreme moisture — heavy rain or storms likely.";
  }

  // ----- WIND & RAIN -----

  // WIND SPEED
  if (wsPos >= 0) {
      windspeedmph = line.substring(wsPos + 8, line.indexOf(",", wsPos)).toFloat();
  } else {
      windspeedmph = 0.0;   // fallback
  }

  // GUST
  if (gPos >= 0) {
      windgustmph = line.substring(gPos + 5, line.indexOf(",", gPos)).toFloat();
  } else {
      windgustmph = 0.0;   // fallback
  }

    // WIND DIR
    if (wdPos >= 0) {
        winddir = line.substring(wdPos + 8,
                                line.indexOf(",", wdPos)).toInt();

        float dirRadians = radians(winddir);

        windDirSumX += windspeedmph * cos(dirRadians);
        windDirSumY += windspeedmph * sin(dirRadians);

        windDirSamples++;
    }
    else {
        winddir = 0;
    }

  // RAIN TOTAL
  if (rPos >= 0) {
      rainin = line.substring(rPos + 5, line.indexOf(",", rPos)).toFloat();
  } else {
      rainin = 0.0;        // fallback
  }

  // RAIN RATE
  if (rrPos >= 0) {
      rainRate = line.substring(rrPos + 9, line.indexOf(",", rrPos)).toFloat();
  } else {
      rainRate = 0.0;      // fallback
  }

  // DAILY RAIN
  if (drPos >= 0) {
      dailyrainin = line.substring(drPos + 10).toFloat();
  } else {
      dailyrainin = 0.0;   // fallback
  }
}

void uploadToWU() {
  float baromin = press * 0.02953;

  bool suppress = suppressingTemp;

  avgWindDirDeg = winddir;   // fallback

    if (windDirSamples > 0)
    {
        float avgRadians = atan2(windDirSumY, windDirSumX);

        avgWindDirDeg = (int)(degrees(avgRadians) + 0.5);

        if (avgWindDirDeg < 0)
            avgWindDirDeg += 360;
    }

  String url = "GET /weatherstation/updateweatherstation.php?";
  url += WU_ID_TXT;
  url += "&";
  url += WU_PWD_TXT;
  url += "&dateutc=now";

  //if (!suppress) {
  // No longer suppressing the temp.  We'll reduce it by a factor during suppression hours, above
  url += "&tempf=" + String(temp, 1);
  //}

  url += "&humidity=" + String(hum, 0);
  url += "&dewptf=" + String(dewpt, 1);
  url += "&baromin=" + String(baromin, 3);

  url += "&windspeedmph=" + String(windspeedmph, 1);
  url += "&windgustmph=" + String(windgustmph, 1);
  url += "&winddir=" + String(avgWindDirDeg);
  url += "&rainin=" + String(rainRate, 2);
  url += "&dailyrainin=" + String(dailyrainin, 2);

  url += "&action=updateraw HTTP/1.1\r\n";
  url += "Host: weatherstation.wunderground.com\r\n";
  url += "Connection: close\r\n\r\n";

  WiFiClient client;
  bool uploadAttempted = false;

    if (client.connect("weatherstation.wunderground.com", 80)) {
        client.print(url);
        uploadAttempted = true;

        lastSuccessfulWU = millis();

        Serial.println("*** WU connection successful ***");
    }
    else {
        Serial.println("*** WU connection FAILED ***");
    }

  Serial.println();
  Serial.print("Wind samples averaged: ");
  Serial.println(windDirSamples);

  if (uploadAttempted)
  {
      windDirSumX = 0.0;
      windDirSumY = 0.0;
      windDirSamples = 0;
  }

  Serial.print("WU Upload (temp ");
  //Serial.print(suppress ? "SUPPRESSED" : "SENT");
  Serial.println(")");
  Serial.print(url);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting...");
  }

  Serial.println("WiFi connected!");
  Serial.println(WiFi.localIP());
  WIFI_IP = WiFi.localIP().toString();
  lastSuccessfulWU = millis();

  Serial.println("ESP32-S3 UART1 starting...");
  Serial1.begin(9600, SERIAL_8N1, 18, 17);
  delay(500);
  Serial.println("UART1 is alive.");

  server.on("/", []() {
    String page = "<!DOCTYPE html><html><head>";
    page += "<meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    page += "<meta http-equiv='refresh' content='4'>";
    page += "<title>Local Weather</title>";
    page += "<style>";
    page += "body { font-family: Arial; background:#f0f0f0; padding:20px; }";
    page += ".card { background:white; padding:20px; margin-bottom:20px; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.2); }";
    page += ".label { font-size:20px; color:#00C; font-weight:bold; }";
    page += ".noemph { font-size:20px; color:black; font-weight:normal; }";
    page += ".value { font-size:40px; font-weight:bold; }";
    page += "</style>";
    page += "</head><body>";

    bool suppress = suppressingTemp;

    page += "<div class='card'><div class='label'>Temperature</div>";
    page += "<div class='value'>" + String(temp, 1) + "&deg;F &nbsp;&nbsp;&nbsp; <span class='noemph'>Raw temp: " + 
         String(rawTemp, 1) + "&deg;F &nbsp;&nbsp;&nbsp; Adj factor: " + String(tempAdjFactor,3) + "</span></div>";

    // No longer suppress the temp.  We'll REPORT it during suppression hours, but REDUCE it by 2% and add a '*'...
    if (suppress) {
        //page += "<div style='font-size:20px; color:#c00; margin-top:2px;'>TEMP UPLOAD SUPPRESSED</div>";
        page += "<div style='font-size:20px; color:#090; margin-top:2px;'>TEMP UPLOAD ACTIVE *</div>";
    } else {
        page += "<div style='font-size:20px; color:#090; margin-top:2px;'>TEMP UPLOAD ACTIVE</div>";
    }
    page += "</div>";

    page += "<div class='card'><div class='label'>Dew Point</div>";
    page += "<div class='value'>" + String(dewpt, 1) + "&deg;F</div>";
    page += "<div class='noemph'>" + rainMessage + "</div>";
    page += "<div class='noemph'>" + String("Humidity: ") + String((int)hum) + " %</div>";
    page += "</div>";

    float baromin = press * 0.02953;  // convert hPa → inHg

    page += "<div class='card'><div class='label'>Pressure</div>";
    page += "<div class='noemph'>" + String(press, 1) + " hPa</div>";
    page += "<div class='value'>" + String(baromin, 2) + " inHg</div>";
    page += "</div>";

    page += "<div class='card'>";
    page += "<div class='label'>Wind</div>";
    page += "<div class='value'>" + String(winddir) + "&deg; at " + String(windspeedmph, 1) + " mph&nbsp;&nbsp;&nbsp;<span class='noemph'>Last reported avg direction: " + String(avgWindDirDeg) + "&deg;</span></div>";
    page += String("<div class='noemph'>Gust ") + String(windgustmph, 1) + " mph</div>";
    page += "</div>";

    page += "<div class='card'>";
    page += "<div class='label'>Rain</div>";
    // Rain rate (in/hr)
    page += "<div class='value'>Rate: ";
    page += String(rainRate, 2);
    page += " in/hr</div>";
    // Daily total
    page += "<div class='value'>Day: ";
    page += String(dailyrainin, 2);
    page += " in</div>";
    page += "</div>";

    page += "<div class='card'><div class='label'>Time</div>";
    page += "<div class='value'>" + timeStr + "</div>";
    page += "<div style='font-size:20px;' class='value'>" + String("Next upload time: ") + nextUploadTime + "</div>";
    page += "</div>";

    unsigned long wuAgeMinutes =
    (millis() - lastSuccessfulWU) / 60000UL;

    page += "<div class='card'>";
    page += "<div class='label'>Weather Underground</div>";

    page += "<div class='noemph'>Last successful connection: ";
    page += String(wuAgeMinutes);
    page += " minute(s) ago</div>";

    if (wuAgeMinutes < 15) {
        page += "<div style='color:#090;font-size:20px;'>ONLINE</div>";
    }
    else if (wuAgeMinutes < 60) {
        page += "<div style='color:#c60;font-size:20px;'>DEGRADED</div>";
    }
    else {
        page += "<div style='color:#c00;font-size:20px;'>OFFLINE</div>";
    }

    page += "</div>";

    page += "</body></html>";

    server.send(200, "text/html", page);
  });

  server.begin();
}

void updateNextUploadTime()
{
    int hh = timeStr.substring(0, 2).toInt();
    int mm = timeStr.substring(3, 5).toInt();
    int ss = timeStr.substring(6, 8).toInt();

    mm += 5;

    if (mm >= 60) {
        mm -= 60;
        hh += 1;
    }

    if (hh >= 24) hh = 0;

    nextUploadTime = pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss);
}

void loop() {

  // Read UART first
  while (Serial1.available()) {
      char c = Serial1.read();

      if (c == '\n') {
          latestWeather = incomingLine;

          if (latestWeather.indexOf("TEMP:") >= 0 &&
              latestWeather.indexOf("TIME:") >= 0) {

              parseWeather(latestWeather);
              haveValidData = true;
          }

          Serial.print("Weather data: ");
          Serial.println(latestWeather);

          incomingLine = "";
      }
      else if (c != '\r') {
          incomingLine += c;
      }
  }

  // ⭐ FORCE BOOT-TIME UPLOAD ONCE WE HAVE DATA AND WIFI IS READY
  if (!firstUploadDone && haveValidData && WiFi.status() == WL_CONNECTED) {
      uploadToWU();
      lastWU = millis();
      firstUploadDone = true;
      updateNextUploadTime();
  }

    if (millis() - lastSuccessfulWU > 3600000UL)
    {
        Serial.println();
        Serial.println("************************************************");
        Serial.println("*** No successful WU connection in 1 hour ***");
        Serial.println("*** Restarting ESP32-S3 ***");
        Serial.println("************************************************");
        Serial.println();

        lastSuccessfulWU = millis();   // prevent immediate retrigger

        delay(1000);
        ESP.restart();
    }

  // Handle web requests
  server.handleClient();

  // Upload to Weather Underground every 5 minutes
  if (haveValidData && millis() - lastWU > 300000) {  // 300000 ms = 5 minutes
      uploadToWU();
      lastWU = millis();
      updateNextUploadTime();
  }

  // Heartbeat
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 1000) {
      lastBeat = millis();
      //Serial.println(WIFI_IP);
  }
}
