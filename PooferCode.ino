#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <ArtnetWifi.h>
#include <ESP8266WebServer.h>

// WiFi settings for Access Point
const char* ap_ssid = "ArtnetDevice";
const char* ap_password = "password123";

// Set DMX channel which will trigger relay
const int igniteOnChannel = 0;
const int tapOnChannel = 1;
const int poofOnChannel = 2;

// Set duration of relay trigger before reset to closed
const int tapDuration = 100;
const int poofDuration = 500;

// Set GPIO IDs for relays.
const int igniteRelay = 4;
const int poofRelay = 5;

// Create Udp and ArtNet objects.
WiFiUDP UdpSend;
ArtnetWifi artnet;
ESP8266WebServer server(80);

// EEPROM addresses for storing SSID and password
const int ssidAddress = 0;
const int passwordAddress = 32;
const int maxStringLength = 32;

// Function prototypes
void saveWiFiCredentials(const char* ssid, const char* password);
void loadWiFiCredentials(char* ssid, char* password);
bool connectToLastWiFi();
String urldecode(const String &input);
bool isHexDigit(char c);

void saveWiFiCredentials(const char* ssid, const char* password) {
  EEPROM.begin(64);
  for (int i = 0; i < maxStringLength; ++i) {
    EEPROM.write(ssidAddress + i, (i < strlen(ssid)) ? ssid[i] : 0);
    EEPROM.write(passwordAddress + i, (i < strlen(password)) ? password[i] : 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadWiFiCredentials(char* ssid, char* password) {
  EEPROM.begin(64);
  for (int i = 0; i < maxStringLength; ++i) {
    ssid[i] = EEPROM.read(ssidAddress + i);
    password[i] = EEPROM.read(passwordAddress + i);
  }
  EEPROM.end();
}

bool connectToLastWiFi() {
  char ssid[maxStringLength] = {0};
  char password[maxStringLength] = {0};
  loadWiFiCredentials(ssid, password);
  
  if (strlen(ssid) > 0) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    Serial.print("Using password: ");
    Serial.println(password);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.println("WiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return true;
    } else {
      Serial.println("");
      Serial.println("Failed to connect to last known WiFi network.");
    }
  } else {
    Serial.println("No stored WiFi credentials found.");
  }
  return false;
}

String urldecode(const String &input) {
  String decoded = "";
  char a, b;
  for (size_t i = 0; i < input.length(); ++i) {
    if (input[i] == '%') {
      if (isHexDigit(input[i + 1]) && isHexDigit(input[i + 2])) {
        a = input[i + 1];
        b = input[i + 2];
        if (a >= 'A') a = (a & 0xDF) - 'A' + 10;
        else a -= '0';
        if (b >= 'A') b = (b & 0xDF) - 'A' + 10;
        else b -= '0';
        decoded += 16 * a + b;
        i += 2;
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

void handleRoot() {
  String html = "<html><body>";

  html += "<script>";

  html += "function IgniteOn() {";
  html += "fetch('/IgniteOn')";
  html += "}";

  html += "function IgniteOff() {";
  html += "fetch('/IgniteOff')";
  html += "}";
  
  html += "function Tap() {";
  html += "fetch('/Tap')";
  html += "}";
  
  html += "function Poof() {";
  html += "fetch('/Poof')";
  html += "}";

  html += "function scan() {";
  html += "fetch('/scan').then(response => response.json()).then(data => {";
  html += "let networksDiv = document.getElementById('networks');";
  html += "networksDiv.innerHTML = '';";
  html += "data.networks.forEach((network) => {";
  html += "networksDiv.innerHTML += `<p>${network.ssid} (${network.rssi}dB) <button onclick=\"connect('${network.ssid}')\">Connect</button></p>`;";
  html += "});";
  html += "});";
  html += "}";

  html += "function connect(ssid) {";
  html += "let password = prompt('Enter password for ' + ssid);";
  html += "if (password === null) { password = ''; }";  // Handle case when password is null
  html += "fetch('/connect', {";
  html += "method: 'POST',";
  html += "headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  html += "body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)"; // Encode SSID and password
  html += "}).then(response => response.text()).then(data => {";
  html += "alert(data);";
  html += "});";
  html += "}";

  html += "</script>";

  html += "<h1>ArtNet Device Setup</h1>";

  html += "<button onclick=\"IgniteOn()\">Ignite On</button>";
  html += "<button onclick=\"IgniteOff()\">Ignite Off</button>";
  html += "<button onclick=\"Tap()\">Tap</button>";
  html += "<button onclick=\"Poof()\">Poof</button>";

  html += "<button onclick=\"scan()\">WiFi Config</button>";
  html += "<div id='networks'></div>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "{ \"networks\": [";
  for (int i = 0; i < n; ++i) {
    if (i) json += ", ";
    json += "{ \"ssid\": \"" + String(WiFi.SSID(i)) + "\", \"rssi\": " + String(WiFi.RSSI(i)) + " }";
  }
  json += "] }";

  server.send(200, "application/json", json);
}

void handleConnect() {

  if (server.method() != HTTP_POST) {
    server.send(405, "Method Not Allowed");
    return;
  }

  // Print raw request body for debugging
  Serial.println("Raw POST body:");
  Serial.println(server.arg("plain"));

  String ssid = server.arg("ssid");
  String password = server.arg("password");

  // Debug prints to check received values
  Serial.println("Received SSID: " + ssid);
  Serial.println("Received Password: " + password);

  // Decode URL-encoded password
  password = urldecode(password);

  Serial.println("DecodePass: " + password);

  saveWiFiCredentials(ssid.c_str(), password.c_str());
  connectToLastWiFi();

  // Attempt to connect to the provided WiFi network
  if (WiFi.status() == WL_CONNECTED) {
    // Save WiFi credentials if successfully connected
    saveWiFiCredentials(ssid.c_str(), password.c_str());
    
    // Debug prints for successful connection
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Send response to client
    server.send(200, "text/plain", "Connected to " + ssid + ". IP address: " + WiFi.localIP().toString());
  } else {
    // Debug prints for failed connection attempt
    Serial.println("WiFi connection failed.");
    // Send error response to client
    server.send(500, "text/plain", "Failed to connect to " + ssid);
  }
}

void setup() {
  // Setup serial for debug output
  Serial.begin(115200);

  // Set the hostname
  WiFi.hostname("Artnet_Device");

  // Try to connect to the last known WiFi network
  bool connected = connectToLastWiFi();

  // If connection to last known network fails, set up Access Point
  if (!connected) {
    WiFi.softAP(ap_ssid, ap_password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
  }

  // Configure server routes
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/IgniteOn", IgniteOn);
  server.on("/IgniteOff", IgniteOff);
  server.on("/Tap", Tap);
  server.on("/Poof", Poof);

  // Start HTTP server
  server.begin();
  Serial.println("HTTP server started");

  // Set mode of the relay pin to OUTPUT to prevent serial signals being sent on it.
  pinMode(igniteRelay, OUTPUT);
  pinMode(poofRelay, OUTPUT);

  digitalWrite(igniteRelay, LOW);
  digitalWrite(poofRelay, LOW);

  // Initialize ArtNet
  artnet.setArtDmxCallback(onDmxFrame);
  artnet.begin();
}

void loop() {
  // Handle web server requests
  server.handleClient();

  // Check for new Art-Net packets
  artnet.read();
}

void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
  // Set state of relay based on channel value
  if (data[igniteOnChannel] > 128) {
    Serial.println("Artnet Ignite On");  
    digitalWrite(igniteRelay, HIGH);
  } else {
    Serial.println("Artnet Ignite Off");  
    digitalWrite(igniteRelay, LOW);
  }

  // Set state of relay based on channel value
  if (data[tapOnChannel] > 128) {
    Serial.println("Artnet Tap");  
    digitalWrite(poofRelay, HIGH);
    delay(tapDuration);
    digitalWrite(poofRelay, LOW);
  } else {
    Serial.println("Artnet Tap Off");  
    digitalWrite(poofRelay, LOW);
  }

  // Set state of relay based on channel value
  if (data[poofOnChannel] > 128) {
    Serial.println("Artnet Poof");  
    digitalWrite(poofRelay, HIGH);
    delay(poofDuration);
    digitalWrite(poofRelay, LOW);
  } else {
    Serial.println("Artnet Poof Off");
    digitalWrite(poofRelay, LOW);
  }
}

void IgniteOn(){
  Serial.println("IgniteOn");
  digitalWrite(igniteRelay, HIGH);
  server.send(200, "text/html", "IgniteOn");
}

void IgniteOff(){
  Serial.println("IgniteOff");
  digitalWrite(igniteRelay, LOW);
  server.send(200, "text/html", "IgniteOff");
}

void Tap(){
  Serial.println("Tap");
  digitalWrite(poofRelay, HIGH);
  delay(tapDuration);
  digitalWrite(poofRelay, LOW);
  server.send(200, "text/html", "Tap");
}

void Poof(){
  Serial.println("Poof");
  digitalWrite(poofRelay, HIGH);
  delay(poofDuration);
  digitalWrite(poofRelay, LOW);
  server.send(200, "text/html", "Poof");
}



