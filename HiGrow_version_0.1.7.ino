/*
Program to control the HiGrow soil monitor sensor.
Sensor had an DHT11 for temperature and humidity and an soil moisture sensor.
It uses an ESP32 for processing and Wifi connecion.
This code is for Rev 1 of the board. Rev 2 wil have light sensor and voltage divider for battery monitoring.

Code: Frank Wijgerse
Version: aug 2018

TO-DO:

- User manual
- Config page in witch user can edit ID, SSID, Password, API-key, report shedule.
- Deepspleep options
- Upload data to server for logging
- Warning how long level was low
- Callibration of sensor(s)
- Code refinement 
- Optimize DHT11 code (no use of external library?)
*/


/**************************************************************                                              
 INIT                                             
 **************************************************************/

// Libraries
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>          // https://github.com/adafruit/DHT-sensor-library
#include <Preferences.h>  // https://github.com/espressif/arduino-esp32/tree/master/libraries/Preferences
#include <ESP.h>
#include <esp_deep_sleep.h>
#include <TimeLib.h>      // http://playground.arduino.cc/code/time
#include <NTPClient.h>    // https://github.com/arduino-libraries/NTPClient
//#include <PushBullet.h> // https://github.com/koen-github/PushBullet-ESP8266

                          // https://www.esp8266.com/viewtopic.php?f=29&t=7116

// Select type of DHT chip
#define DHTTYPE DHT11     // DHT 11
//#define DHTTYPE DHT21   // DHT 21 (AM2301)
//#define DHTTYPE DHT11   // DHT 22 (AM2302), AM2321, maybe for future versions

// Deep sleep variables
#define uS_TO_S_FACTOR 1000000
int DEEPSLEEP_SECONDS = 1800;

// Services
WiFiClient client;
WiFiServer server(80);

WiFiUDP ntpUDP;  // Start UDP service for TimeServer connection
// By default 'pool.ntp.org' is used with 60 seconds update interval and no offset

NTPClient timeClient(ntpUDP);
// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
// NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

HTTPClient http;

Preferences preferences;

// Misc.
uint64_t chipid;

// Define pins
const int LED_PIN = 16;
const int DHT_PIN = 22;
const int SOIL_PIN = 32;
const int POWER_PIN = 34;
const int LIGHT_PIN = 33;

// Initialize DHT sensor.
DHT dht(DHT_PIN, DHTTYPE);

// Temporary variables
static char celsiusTemp[7];
static char humidityTemp[7];

// Global variables 
//char linebuf[80];
//int charcount=0;
int waterlevel; 
int humidity;                        // Precision of the DHT11 is only 1 percent
int temperature;                     // Precision of the DHT11 is only 1 degree celcius
int lightlevel;                      // Light level sensor. Not present in Rev1
unsigned long TimeLastReading;       // Time (in millis) last reading was made
unsigned long TimeSinceLastReading;  // Time elapsed since last reading
unsigned long TimeLastReport;        // Time (in millis) last report was send
unsigned long TimeSinceLastReport;   // Time elapsed since last report was send
unsigned long ConnectTimeStart;
unsigned long ConnectTimePassed;
boolean LedState;
char deviceid[21];

// this section handles configuration values which can be configured via webpage form in a webbrowser
// 8 configuration values max
String ConfigName[8];     // name of the configuration value
String ConfigValue[8];    // the value itself (String)
int    ConfigStatus[8];   // status of the value    0 = not set    1 = valid   -1 = not valid

int shedule1[] = {10};
int shedule2[] = {0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
int shedule3[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};

// PushBullet variables
const char* host = "api.pushbullet.com";
const int httpsPort = 443;
// Use web browser to view and copy
// SHA1 fingerprint of the certificate
const char* fingerprint = "2C BC 06 10 0A E0 6E B0 9E 60 E5 96 BA 72 C5 63 93 23 54 B3"; //got it using https://www.grc.com/fingerprints.htm

String ssidstr = "";                                         
String passwordstr = "";                                     
String PushBulletAPIKeystr = "";                             

// Set personal keys
char ssid[50] = "";                                         // Set this to YOURSSID
char password[50] = "";                                     // Set this to YOURPASSWORD
char PushBulletAPIKey[50] = "";                             // Set this to YOURPUSHBULLETAPIKEY   (Pushbullet API-Key) 
String ID = "one";

//PushBullet pb = PushBullet("API", &client, 443);
//WiFiClientSecure client;
//PushBullet pb = PushBullet(PushBulletAPIKey, &client, 443);



/**************************************************************                                              
 SETUP                                     
 Start all of the services 
 Define pins                                             
 **************************************************************/
void setup() {
  dht.begin();
  timeClient.begin();
  Serial.begin(115200);
  preferences.begin("higrowsensor", false);
  ssidstr=preferences.getString("ssid","");
  passwordstr=preferences.getString("password","");
  PushBulletAPIKeystr=preferences.getString("PBAPIKey","");    // Key length is max 15 
  ID=preferences.getString("ID","");
  
  ssidstr.toCharArray(ssid, 50);                               // Copy it over; type convertion complete 
  passwordstr.toCharArray(password, 50);                       // Copy it over; type convertion complete 
  PushBulletAPIKeystr.toCharArray(PushBulletAPIKey, 50);       // Copy it over; type convertion complete 

 
  pinMode(LED_PIN, OUTPUT); 
  // pinMode(POWER_PIN, INPUT);  // Option for battery level monitoring not in rev1!
  digitalWrite(LED_PIN, HIGH);   // LED off! 
  //WiFi.mode(WIFI_STA);
  WiFi.mode(WIFI_AP_STA);        // Access point AND stand alone
  WiFi.softAP("HiGrow_Sensor");

  chipid = ESP.getEfuseMac();
  sprintf(deviceid, "%" PRIu64, chipid);
  Serial.print("DeviceId: ");
  Serial.println(deviceid);
}



/**************************************************************                                              
 MAIN PROGRAM                                     
 **************************************************************/
void loop() {
  if ( (WiFi.status() != WL_CONNECTED) || (WiFi.SSID() != ssid) ) {    // if no connection OR used had changed ssid
    ConnectToWifi();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
  }
  
  // Check for request for html page load or refresh. Evaluate request and send page.
  checkForHTMLRequest();

  TimeSinceLastReport=now()-TimeLastReport;
//  if ( (TimeSinceLastReport>3600) && (now()>30) ) {                 // Send report every minute (60) or hour (3600), wait 30 seconds for NTP-time sync.
  if (TimeSinceLastReport>3600) {                                     // Send report every minute (60) or hour (3600)
    SendPushMessage("HiGrow" + ID + "Report");
    TimeLastReport=now();
  }  // END if statement "TimeSinceLastReport"
  
  TimeSinceLastReading=millis()-TimeLastReading;  // Used for sensorreading and output to serial (ones every 5 sec for now)
  if (TimeSinceLastReading>5000) {
    ReadSensorsData();
    ReportSerial();
    TimeLastReading=millis();
  }  // END if statement "TimeSinceLastReading"
}    // END loop



/**************************************************************                                              
 SUBROUTINES                                     
 **************************************************************/
void ConnectToWifi() {
  WiFi.disconnect();                                  // close connection if "old" one exists
  Serial.print("Connecting to: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);                         // Only try to connect if there is no connection
  server.begin();                                     // Call after every WiFI.begin()
  ConnectTimeStart=millis();
  while (WiFi.status() != WL_CONNECTED) {             // Do nothing else while trying to connect
    ConnectTimePassed = millis() - ConnectTimeStart;
    if (ConnectTimePassed > 30000) {break;}           // TimeOut after 30 seconds
    BlinkLED();                                       // While searching for wifi connection blink led on and off
    Serial.print(".");      
    checkForHTMLRequest();                            // Waiting takes 30 sec, need to check for request regularly
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);                      // LED is off!!
    Serial.println("Connection failed!");
  }  
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);                       // LED is on!!
    Serial.println("Connection succesfull");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    timeClient.update();                              // Sync time, so start up message had correct time
    delay(500);                                       // Give sync time   
    ReadSensorsData();                                // Read data so first start up message to be usefull
    SendPushMessage("HiGrow " + ID + "Connect");      // Send message via PushBullet service
  }
}



void ReadSensorsData(){   // This section read sensors
  
  waterlevel = analogRead(SOIL_PIN);
  lightlevel = analogRead(LIGHT_PIN);   // NOT AVAILABLE IN REV 1. PLANNED FOR REV 2
  
  //waterlevel = map(waterlevel, 0, 4095, 0, 100);   // Convert RAW input to a percentage. Need to check this further.
  waterlevel = map(waterlevel, 1350, 3275, 100, 0);
  waterlevel = constrain(waterlevel, 0, 100);    // Use this as ultimate values
  lightlevel = map(lightlevel, 0, 4095, 0, 1023);
  //lightlevel = constrain(lightlevel, 0, 1023);
  
  humidity = dht.readHumidity();        // Read relative humidity, sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  temperature = dht.readTemperature();  // Read temperature as Celsius (the default), sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
} // END ReadSensorsData



void SendPushMessage(String title) {   // Send push message via the PuskBullet service
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  Serial.print("connecting to ");
  Serial.println(host);
  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  if (client.verify(fingerprint, host)) {
    Serial.println("certificate matches");
  } else {
    Serial.println("certificate doesn't match");
  }
  String url = "/v2/pushes";
  //String title = "HiGrow 01 Report";
  String body = "Time: " + timeClient.getFormattedTime() +"UT\\n" +
                "Temperature: " + String(temperature) + "°C\\n" +  
                "Humidity: " + String(humidity) + "%\\n" +
                "Waterlevel: " + String(waterlevel);
  //String body = "Temperature: " + temperature + "\\nHumidity: " + humidity + "\\nWaterlevel: " + waterlevel;
  //String body = "Hello!\\nSecond line";
      String messagebody= "{\"type\": \"note\", \"title\": \"" + title + "\", \"body\": \"" + body + "\"}\r\n";
   //   String messagebody = "{\"type\": \"note\", \"title\": \"ESP8266\", \"body\": \"Hello World!\"}\r\n";

    Serial.print("Messagebody: ");
    Serial.println(messagebody);
  Serial.println(url);

  client.print(String("POST ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + PushBulletAPIKey + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " +
               String(messagebody.length()) + "\r\n\r\n");
  client.print(messagebody);

  Serial.println("request sent");

  //print the response

  while (client.available() == 0);

  while (client.available()) {
    String line = client.readStringUntil('\n');
    Serial.println(line);
  }
} // END SendPushMessage



void BlinkLED() {    // Change Led from On to Off and reverse
  LedState = !LedState;  // troggle value
  if (LedState==true)  {digitalWrite(LED_PIN, LOW);}  // LED on!
  if (LedState==false) {digitalWrite(LED_PIN, HIGH);}  // LED off!
} // END BlinkLED



boolean ElementOf(int element, int myArray[])   {
  int i;
  boolean result;
  result=false;
  for (i = 0; i < sizeof(myArray) - 1; i++){
    if (element==myArray[i]) {result=true;}   
  }
 return result; 
} // END ElementOf



void ReportSerial() {
    Serial.print(timeClient.getFormattedTime());
    Serial.println("UT");
    Serial.print("IP address:  ");
    Serial.println(WiFi.localIP());
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println("°C");
    Serial.print("Humidity:    ");
    Serial.print(humidity);    
    Serial.println("%");
    Serial.print("Waterlevel:  ");
    Serial.println(waterlevel);
} // END ReportSerial



void checkForHTMLRequest() {
  // Check for request for html page load or refresh. Evaluate request and send page.
  String GETParameter = GetRequestGETparameter();   
  if (GETParameter.length() > 0) {       // we got a request, client connection stays open
    if (GETParameter.length() > 1)  {    // request contains some GET parameter
      int countValues = DecodeGETParameterAndSetConfigValues(GETParameter);     // decode the GET parameter and set ConfigValue        
      ProcessAndValidateConfigValues(countValues);                              // check and process ConfigValues
    }     
    OutputHTMLpage();                                   // Need to run this often. Maybe this not the right place. If local network, is WiFi.status() then WL_CONNECTED?
  }
}


//  Call this function regularly to look for client requests
//  template see https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/SimpleWiFiServer/SimpleWiFiServer.ino
//  returns empty string if no request from any client
//  returns GET Parameter: everything after the "/?" if ADDRESS/?xxxx was entered by the user in the webbrowser
//  returns "-" if ADDRESS but no GET Parameter was entered by the user in the webbrowser
//  remark: client connection stays open after return
String GetRequestGETparameter() {
  String GETParameter = "";
  client = server.available();
  if (client) {
    Serial.println("new client");
    String currentLine = "";                 // make a String to hold incoming data from the client
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n') {                     // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request
          if (currentLine.length() == 0) {
            
            if (GETParameter == "") {GETParameter = "-";};    // if no "GET /?" was found so far in the request bytes, return "-"
            
            // break out of the while loop:
            break;
        
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
          
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        if (c=='\r' && currentLine.startsWith("GET /?")) 
        // we see a "GET /?" in the HTTP data of the client request
        // user entered ADDRESS/?xxxx in webbrowser, xxxx = GET Parameter
        {         
          GETParameter = currentLine.substring(currentLine.indexOf('?') + 1, currentLine.indexOf(' ', 6));    // extract everything behind the ? and before a space           
        } 
      } 
    } 
  }
  return GETParameter;
}



    /*
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }
}
*/

// Decodes a GET parameter (expression after ? in URI (URI = expression entered in address field of webbrowser)), like "Country=Germany&City=Aachen"
// and set the ConfigValues
int DecodeGETParameterAndSetConfigValues(String GETParameter)
{
   
   int posFirstCharToSearch = 1;
   int count = 0;
   
   // while a "&" is in the expression, after a start position to search
   while (GETParameter.indexOf('&', posFirstCharToSearch) > -1)
   {
     int posOfSeparatorChar = GETParameter.indexOf('&', posFirstCharToSearch);  // position of & after start position
     int posOfValueChar = GETParameter.indexOf('=', posFirstCharToSearch);      // position of = after start position
  
     ConfigValue[count] = GETParameter.substring(posOfValueChar + 1, posOfSeparatorChar);  // extract everything between = and & and enter it in the ConfigValue
      
     posFirstCharToSearch = posOfSeparatorChar + 1;  // shift the start position to search after the &-char 
     count++;
   }

   // no more & chars found
   
   int posOfValueChar = GETParameter.indexOf('=', posFirstCharToSearch);       // search for =
   
   ConfigValue[count] = GETParameter.substring(posOfValueChar + 1, GETParameter.length());  // extract everything between = and end of string
   count++;

   return count;  // number of values found in GET parameter
}



void ProcessAndValidateConfigValues(int countValues) {

  ConfigValue[0]=GetRidOfurlCharacters(ConfigValue[0]);  // Clean up values from special chars
  ConfigValue[1]=GetRidOfurlCharacters(ConfigValue[1]);
  ConfigValue[2]=GetRidOfurlCharacters(ConfigValue[2]);
  ConfigValue[3]=GetRidOfurlCharacters(ConfigValue[3]);

  ConfigValue[0].toCharArray(ssid, 50);                  // Copy it over; type convertion complete 
  ConfigValue[1].toCharArray(password, 50);              // Copy it over; type convertion complete 
  ConfigValue[2].toCharArray(PushBulletAPIKey, 50);      // Copy it over; type convertion complete 
  ID=ConfigValue[3];                                     // already a string 

  preferences.putString("ssid", String(ssid));
  preferences.putString("password", String(password));
  preferences.putString("PBAPIKey", String(PushBulletAPIKey));
  preferences.putString("ID", ID);

}



void OutputHTMLpage() {
            /*  The use of (F("string)) is for compatible reasons only. On the ESP32 non-changing date is automaticaly stored in flash memory, 
             *  not in dynamic memory.
             */
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println("Connection: close");  // the connection will be closed after completion of the response
            //client.println("Refresh: 5");  // refresh the page automatically every 5 sec
            client.println();
            client.println("<!DOCTYPE HTML>");

            client.println(F("<html>"));
            client.println(F("<head>"));
            client.println(F("  <style>"));
            client.println(F("    html {")); 
            client.println(F("      font-family: Arial, Verdana, sans-serif;"));
            client.println(F("    }"));
            client.println(F("    p1 {")); 
            client.println(F("      font-size: 24px;"));
            client.println(F("    }"));
            client.println(F("    p2 {")); 
            client.println(F("      font-size: 20px;"));
            client.println(F("    }"));
            client.println(F("    p3 {")); 
            client.println(F("      font-size: 16px;"));
            client.println(F("    }"));
            client.println(F("    body {")); 
            client.println(F("      font-size: 12px;"));
            client.println(F("    }"));
            client.println(F("    table {")); 
            client.println(F("      font-size: 12px;"));
            client.println(F("    }"));
            client.println(F("    input[type='text'] {")); 
            client.println(F("      font-family: Arial, Verdana, sans-serif;"));
            client.println(F("      font-size: 12px;"));
            client.println(F("    }"));
            client.println(F("  </style>"));
            client.println(F("</head>"));
            client.println();
            client.println();
            client.println(F("<body>")); 
            client.println();
            client.println(F("<p1>HiGrow</p1><br>"));
            client.println(F("<p2>configuration</p2><br>"));
            client.println(F("<br>"));
            client.println(F("<p3>Values</p3><br>"));
              client.print(F("  Temperature: "));   client.print(temperature);                    client.println(F("&deg;C<br>"));
              client.print(F("  Humidity: "));      client.print(humidity);                       client.println(F("%<br>"));
              client.print(F("  Water level: "));   client.print(waterlevel);                     client.println(F("<br>"));
            client.println(F("<br>"));
            client.println(F("<p3>Config</p3><br>"));
              client.print(F("  Connected to: "));  client.print(WiFi.SSID());                    client.println(F("<br>"));        
              client.print(F("  IP-address: "));    client.print(WiFi.localIP());                 client.println(F("<br>"));        
              client.print(F("  RSSI: "));          client.print(WiFi.RSSI());                    client.println(F("<br>"));           //  TO-DO: insert nice wifi-strenght-connection symbol ·)))  
              client.print(F("  Time: "));          client.print(timeClient.getFormattedTime());  client.println(F("UT<br>"));
            client.println();
            client.println(F("<br>"));
            
         // client.print(F("Config0: "));client.print(ConfigValue[0]);client.println(F("<br>"));
         // client.print(F("Config1: "));client.print(ConfigValue[1]);client.println(F("<br>"));
         // client.print(F("Config2: "));client.print(ConfigValue[2]);client.println(F("<br>"));
         // client.print(F("ssid: "));client.print(ssid);client.println(F("<br>"));
         // client.println(F("<br>"));   
       
            client.println();
            client.println(F("<form>"));
            client.println();
            client.println(F("  <p3>Settings</p3><br>"));
            client.println(F("  <table>"));
              client.print(F("    <tr><td>SSID:      </td><td>  <input type=\"text\" size=\"50\" name=\"SSID\" value=\""));      client.print(ssid);              client.println(F("\"></td></tr>"));
              client.print(F("    <tr><td>Password:  </td><td>  <input type=\"text\" size=\"50\" name=\"password\" value=\""));  client.print(password);          client.println(F("\"></td></tr>"));
              client.print(F("    <tr><td>API-key:   </td><td>  <input type=\"text\" size=\"50\" name=\"apikey\" value=\""));    client.print(PushBulletAPIKey);  client.println(F("\"></td></tr>"));
              client.print(F("    <tr><td>ID:        </td><td>  <input type=\"text\" size=\"50\" name=\"ID\" value=\""));        client.print(ID);                client.println(F("\"></td></tr>"));
            client.println(F("  </table>"));  //42948 bytes
            client.println();
            client.println(F("  <br>"));
            client.println(F("  <p3>Reports</p3><br>"));
            //client.println(F("    <input type=\"radio\" name=\"shedule\" value=\"1\">Once a day a 10:00UT<br>"));
            client.println(F("    <input type=\"radio\" name=\"shedule\" value=\"2\">Every hour<br>"));
            //client.println(F("    <input type=\"radio\" name=\"shedule\" value=\"3\">Every hour from 7:00UT to 22:00UT"));
            client.println(F("  <br>"));
            client.println(F("  <br>"));
            client.println(F("  <input type=\"submit\" value=\"Save\">"));
            client.println();
            client.println(F("</form>")); 
            client.println(F("</body>"));
            client.println(F("</html>")); 

            client.stop();
            Serial.println("Client Disconnected.");
}




String GetRidOfurlCharacters(String urlChars)
{

  urlChars.replace("%0D%0A", String('\n'));

  urlChars.replace("+",   " ");
  urlChars.replace("%20", " ");
  urlChars.replace("%21", "!");
  urlChars.replace("%22", String(char('\"')));
  urlChars.replace("%23", "#");
  urlChars.replace("%24", "$");
  urlChars.replace("%25", "%");
  urlChars.replace("%26", "&");
  urlChars.replace("%27", String(char(39)));
  urlChars.replace("%28", "(");
  urlChars.replace("%29", ")");
  urlChars.replace("%2A", "*");
  urlChars.replace("%2B", "+");
  urlChars.replace("%2C", ",");
  urlChars.replace("%2D", "-");
  urlChars.replace("%2E", ".");
  urlChars.replace("%2F", "/");
  urlChars.replace("%30", "0");
  urlChars.replace("%31", "1");
  urlChars.replace("%32", "2");
  urlChars.replace("%33", "3");
  urlChars.replace("%34", "4");
  urlChars.replace("%35", "5");
  urlChars.replace("%36", "6");
  urlChars.replace("%37", "7");
  urlChars.replace("%38", "8");
  urlChars.replace("%39", "9");
  urlChars.replace("%3A", ":");
  urlChars.replace("%3B", ";");
  urlChars.replace("%3C", "<");
  urlChars.replace("%3D", "=");
  urlChars.replace("%3E", ">");
  urlChars.replace("%3F", "?");
  urlChars.replace("%40", "@");
  urlChars.replace("%41", "A");
  urlChars.replace("%42", "B");
  urlChars.replace("%43", "C");
  urlChars.replace("%44", "D");
  urlChars.replace("%45", "E");
  urlChars.replace("%46", "F");
  urlChars.replace("%47", "G");
  urlChars.replace("%48", "H");
  urlChars.replace("%49", "I");
  urlChars.replace("%4A", "J");
  urlChars.replace("%4B", "K");
  urlChars.replace("%4C", "L");
  urlChars.replace("%4D", "M");
  urlChars.replace("%4E", "N");
  urlChars.replace("%4F", "O");
  urlChars.replace("%50", "P");
  urlChars.replace("%51", "Q");
  urlChars.replace("%52", "R");
  urlChars.replace("%53", "S");
  urlChars.replace("%54", "T");
  urlChars.replace("%55", "U");
  urlChars.replace("%56", "V");
  urlChars.replace("%57", "W");
  urlChars.replace("%58", "X");
  urlChars.replace("%59", "Y");
  urlChars.replace("%5A", "Z");
  urlChars.replace("%5B", "[");
  urlChars.replace("%5C", String(char(65)));
  urlChars.replace("%5D", "]");
  urlChars.replace("%5E", "^");
  urlChars.replace("%5F", "_");
  urlChars.replace("%60", "`");
  urlChars.replace("%61", "a");
  urlChars.replace("%62", "b");
  urlChars.replace("%63", "c");
  urlChars.replace("%64", "d");
  urlChars.replace("%65", "e");
  urlChars.replace("%66", "f");
  urlChars.replace("%67", "g");
  urlChars.replace("%68", "h");
  urlChars.replace("%69", "i");
  urlChars.replace("%6A", "j");
  urlChars.replace("%6B", "k");
  urlChars.replace("%6C", "l");
  urlChars.replace("%6D", "m");
  urlChars.replace("%6E", "n");
  urlChars.replace("%6F", "o");
  urlChars.replace("%70", "p");
  urlChars.replace("%71", "q");
  urlChars.replace("%72", "r");
  urlChars.replace("%73", "s");
  urlChars.replace("%74", "t");
  urlChars.replace("%75", "u");
  urlChars.replace("%76", "v");
  urlChars.replace("%77", "w");
  urlChars.replace("%78", "x");
  urlChars.replace("%79", "y");
  urlChars.replace("%7A", "z");
  urlChars.replace("%7B", String(char(123)));
  urlChars.replace("%7C", "|");
  urlChars.replace("%7D", String(char(125)));
  urlChars.replace("%7E", "~");
  urlChars.replace("%7F", "Â");
  urlChars.replace("%80", "`");
  urlChars.replace("%81", "Â");
  urlChars.replace("%82", "â");
  urlChars.replace("%83", "Æ");
  urlChars.replace("%84", "â");
  urlChars.replace("%85", "â¦");
  urlChars.replace("%86", "â");
  urlChars.replace("%87", "â¡");
  urlChars.replace("%88", "Ë");
  urlChars.replace("%89", "â°");
  urlChars.replace("%8A", "Å");
  urlChars.replace("%8B", "â¹");
  urlChars.replace("%8C", "Å");
  urlChars.replace("%8D", "Â");
  urlChars.replace("%8E", "Å½");
  urlChars.replace("%8F", "Â");
  urlChars.replace("%90", "Â");
  urlChars.replace("%91", "â");
  urlChars.replace("%92", "â");
  urlChars.replace("%93", "â");
  urlChars.replace("%94", "â");
  urlChars.replace("%95", "â¢");
  urlChars.replace("%96", "â");
  urlChars.replace("%97", "â");
  urlChars.replace("%98", "Ë");
  urlChars.replace("%99", "â¢");
  urlChars.replace("%9A", "Å¡");
  urlChars.replace("%9B", "âº");
  urlChars.replace("%9C", "Å");
  urlChars.replace("%9D", "Â");
  urlChars.replace("%9E", "Å¾");
  urlChars.replace("%9F", "Å¸");
  urlChars.replace("%A0", "Â");
  urlChars.replace("%A1", "Â¡");
  urlChars.replace("%A2", "Â¢");
  urlChars.replace("%A3", "Â£");
  urlChars.replace("%A4", "Â¤");
  urlChars.replace("%A5", "Â¥");
  urlChars.replace("%A6", "Â¦");
  urlChars.replace("%A7", "Â§");
  urlChars.replace("%A8", "Â¨");
  urlChars.replace("%A9", "Â©");
  urlChars.replace("%AA", "Âª");
  urlChars.replace("%AB", "Â«");
  urlChars.replace("%AC", "Â¬");
  urlChars.replace("%AE", "Â®");
  urlChars.replace("%AF", "Â¯");
  urlChars.replace("%B0", "Â°");
  urlChars.replace("%B1", "Â±");
  urlChars.replace("%B2", "Â²");
  urlChars.replace("%B3", "Â³");
  urlChars.replace("%B4", "Â´");
  urlChars.replace("%B5", "Âµ");
  urlChars.replace("%B6", "Â¶");
  urlChars.replace("%B7", "Â·");
  urlChars.replace("%B8", "Â¸");
  urlChars.replace("%B9", "Â¹");
  urlChars.replace("%BA", "Âº");
  urlChars.replace("%BB", "Â»");
  urlChars.replace("%BC", "Â¼");
  urlChars.replace("%BD", "Â½");
  urlChars.replace("%BE", "Â¾");
  urlChars.replace("%BF", "Â¿");
  urlChars.replace("%C0", "Ã");
  urlChars.replace("%C1", "Ã");
  urlChars.replace("%C2", "Ã");
  urlChars.replace("%C3", "Ã");
  urlChars.replace("%C4", "Ã");
  urlChars.replace("%C5", "Ã");
  urlChars.replace("%C6", "Ã");
  urlChars.replace("%C7", "Ã");
  urlChars.replace("%C8", "Ã");
  urlChars.replace("%C9", "Ã");
  urlChars.replace("%CA", "Ã");
  urlChars.replace("%CB", "Ã");
  urlChars.replace("%CC", "Ã");
  urlChars.replace("%CD", "Ã");
  urlChars.replace("%CE", "Ã");
  urlChars.replace("%CF", "Ã");
  urlChars.replace("%D0", "Ã");
  urlChars.replace("%D1", "Ã");
  urlChars.replace("%D2", "Ã");
  urlChars.replace("%D3", "Ã");
  urlChars.replace("%D4", "Ã");
  urlChars.replace("%D5", "Ã");
  urlChars.replace("%D6", "Ã");
  urlChars.replace("%D7", "Ã");
  urlChars.replace("%D8", "Ã");
  urlChars.replace("%D9", "Ã");
  urlChars.replace("%DA", "Ã");
  urlChars.replace("%DB", "Ã");
  urlChars.replace("%DC", "Ã");
  urlChars.replace("%DD", "Ã");
  urlChars.replace("%DE", "Ã");
  urlChars.replace("%DF", "Ã");
  urlChars.replace("%E0", "Ã");
  urlChars.replace("%E1", "Ã¡");
  urlChars.replace("%E2", "Ã¢");
  urlChars.replace("%E3", "Ã£");
  urlChars.replace("%E4", "Ã¤");
  urlChars.replace("%E5", "Ã¥");
  urlChars.replace("%E6", "Ã¦");
  urlChars.replace("%E7", "Ã§");
  urlChars.replace("%E8", "Ã¨");
  urlChars.replace("%E9", "Ã©");
  urlChars.replace("%EA", "Ãª");
  urlChars.replace("%EB", "Ã«");
  urlChars.replace("%EC", "Ã¬");
  urlChars.replace("%ED", "Ã­");
  urlChars.replace("%EE", "Ã®");
  urlChars.replace("%EF", "Ã¯");
  urlChars.replace("%F0", "Ã°");
  urlChars.replace("%F1", "Ã±");
  urlChars.replace("%F2", "Ã²");
  urlChars.replace("%F3", "Ã³");
  urlChars.replace("%F4", "Ã´");
  urlChars.replace("%F5", "Ãµ");
  urlChars.replace("%F6", "Ã¶");
  urlChars.replace("%F7", "Ã·");
  urlChars.replace("%F8", "Ã¸");
  urlChars.replace("%F9", "Ã¹");
  urlChars.replace("%FA", "Ãº");
  urlChars.replace("%FB", "Ã»");
  urlChars.replace("%FC", "Ã¼");
  urlChars.replace("%FD", "Ã½");
  urlChars.replace("%FE", "Ã¾");
  urlChars.replace("%FF", "Ã¿");

  return urlChars;
}


/**************************************************************                                              
 IDEAS, TEST, ETC.                                     
 **************************************************************/

    //http.begin("http://api.higrow.tech/api/records");
    //http.addHeader("Content-Type", "application/json");
    //int httpResponseCode = http.POST(body);
    //Serial.println(httpResponseCode);
    //esp_deep_sleep_enable_timer_wakeup(DEEPSLEEP_SECONDS * uS_TO_S_FACTOR);
    //esp_deep_sleep_start();

  // while(!Serial) {
  //   ; // wait for serial port to connect. Needed for native USB port only
  // }
  // esp_deep_sleep_enable_timer_wakeup(1800 * uS_TO_S_FACTOR);
  // esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);



