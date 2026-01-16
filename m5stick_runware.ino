#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>

static const int WIDTH  = 240;
static const int HEIGHT = 135;

const char* RUNWARE_API_KEY = "getakeyfromrunware";
const char* RUNWARE_API_URL = "https://api.runware.ai/v1";

// Timing
unsigned long lastImageTime = 0;
const unsigned long IMAGE_INTERVAL = 10000; // 10 seconds

// WiFi setup
Preferences prefs;
WebServer server(80);
String savedSSID;
String savedPass;
bool apMode = false;

// Image counter for varied prompts
int imageCount = 0;

// HTML page for WiFi setup
const char* setupPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; background: #1a1a2e; color: #fff; padding: 20px; }
    .container { max-width: 300px; margin: 0 auto; }
    h1 { color: #0f0; font-size: 24px; }
    input { width: 100%; padding: 12px; margin: 8px 0; box-sizing: border-box; 
            border-radius: 8px; border: none; font-size: 16px; }
    input[type=submit] { background: #0f0; color: #000; cursor: pointer; 
                         font-weight: bold; }
    input[type=submit]:hover { background: #0c0; }
  </style>
</head>
<body>
  <div class="container">
    <h1>M5 Runware Setup</h1>
    <form action="/save" method="POST">
      <input type="text" name="ssid" placeholder="WiFi Name" required>
      <input type="password" name="pass" placeholder="WiFi Password" required>
      <input type="submit" value="Connect">
    </form>
  </div>
</body>
</html>
)rawliteral";

const char* successPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; background: #1a1a2e; color: #fff; padding: 20px; 
           text-align: center; }
    h1 { color: #0f0; }
  </style>
</head>
<body>
  <h1>Saved!</h1>
  <p>Device is restarting...</p>
</body>
</html>
)rawliteral";

void drawScreen(const String &text) {
  M5Canvas canvas(&M5.Display);
  canvas.createSprite(WIDTH, HEIGHT);
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextFont(2);

  int lineCount = 1;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text[i] == '\n') lineCount++;
  }

  int lineHeight = canvas.fontHeight() + 4;
  int startY = (HEIGHT - lineCount * lineHeight) / 2 + lineHeight / 2;

  int lineNum = 0;
  int lineStart = 0;
  for (unsigned int i = 0; i <= text.length(); i++) {
    if (i == text.length() || text[i] == '\n') {
      String line = text.substring(lineStart, i);
      canvas.drawString(line, WIDTH / 2, startY + lineNum * lineHeight);
      lineNum++;
      lineStart = i + 1;
    }
  }

  canvas.pushSprite(0, 0);
  canvas.deleteSprite();
}

// =============== WIFI SETUP ===============

void handleRoot() {
  Serial.println("Serving setup page");
  server.send(200, "text/html", setupPage);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  
  Serial.println("Received credentials:");
  Serial.println("  SSID: " + ssid);
  Serial.println("  Pass: " + String(pass.length()) + " chars");
  
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  
  Serial.println("Credentials saved to Preferences");
  
  server.send(200, "text/html", successPage);
  
  delay(2000);
  ESP.restart();
}

void startAPMode() {
  Serial.println("\n========== STARTING AP MODE ==========");
  
  apMode = true;
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("M5Runware-Setup");
  
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(ip);
  
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  
  Serial.println("Web server started");
  Serial.println("=======================================\n");
  
  drawScreen("WiFi Setup\n\nConnect to:\nM5Runware-Setup\n\nThen go to:\n192.168.4.1");
}

bool connectToWiFi() {
  prefs.begin("wifi", true);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();
  
  Serial.println("\n========== WIFI CONNECTION ==========");
  Serial.println("Stored SSID: " + savedSSID);
  Serial.println("Stored pass: " + String(savedPass.length()) + " chars");
  
  if (savedSSID.length() == 0) {
    Serial.println("No credentials stored");
    Serial.println("======================================\n");
    return false;
  }
  
  drawScreen("Connecting to\n" + savedSSID + "...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("======================================\n");
    return true;
  }
  
  Serial.println("\nFailed to connect");
  Serial.println("======================================\n");
  return false;
}

void resetCredentials() {
  Serial.println("\n========== RESETTING CREDENTIALS ==========");
  
  drawScreen("Resetting...");
  
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  
  Serial.println("Credentials cleared, restarting...");
  Serial.println("============================================\n");
  
  delay(1000);
  ESP.restart();
}

// =============== RUNWARE IMAGE GENERATION ===============

String generateUUID() {
  // Simple UUID generation for taskUUID
  String uuid = "";
  const char* hex = "0123456789abcdef";
  int pattern[] = {8, 4, 4, 4, 12};
  
  for (int p = 0; p < 5; p++) {
    if (p > 0) uuid += "-";
    for (int i = 0; i < pattern[p]; i++) {
      uuid += hex[random(16)];
    }
  }
  return uuid;
}

String getPrompt() {
  // Rotate through different prompts for variety
  const char* prompts[] = {
    "abstract colorful swirls, vibrant digital art",
    "geometric patterns, neon colors, futuristic",
    "dreamy landscape, purple sunset, mountains",
    "glowing orbs floating in space, ethereal",
    "circuit board patterns, glowing traces, tech art",
    "ocean waves, turquoise water, golden light",
    "aurora borealis, night sky, stars",
    "crystal formations, iridescent, magical",
    "fractal patterns, infinite depth, colorful",
    "bioluminescent forest, glowing mushrooms"
  };
  
  int numPrompts = sizeof(prompts) / sizeof(prompts[0]);
  return String(prompts[imageCount % numPrompts]);
}

bool fetchRunwareImage() {
  Serial.println("\n========== FETCHING RUNWARE IMAGE ==========");
  
  drawScreen("Generating\nimage...");
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  Serial.println("Connecting to Runware API...");
  http.begin(client, RUNWARE_API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + RUNWARE_API_KEY);
  http.setTimeout(60000);
  
  String taskUUID = generateUUID();
  String prompt = getPrompt();
  
  Serial.println("Prompt: " + prompt);
  Serial.println("TaskUUID: " + taskUUID);
  
  // Build JSON request - 128x128 PNG for better compatibility
  String body = "[{"
    "\"taskType\":\"imageInference\","
    "\"numberResults\":1,"
    "\"outputFormat\":\"PNG\","
    "\"width\":128,"
    "\"height\":128,"
    "\"CFGScale\":3.5,"
    "\"scheduler\":\"FlowMatchEulerDiscreteScheduler\","
    "\"includeCost\":true,"
    "\"outputType\":[\"URL\"],"
    "\"acceleration\":\"high\","
    "\"model\":\"runware:400@1\","
    "\"positivePrompt\":\"" + prompt + "\","
    "\"taskUUID\":\"" + taskUUID + "\""
  "}]";
  
  Serial.println("Request body: " + body);
  
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  if (httpCode != 200) {
    String error = http.getString();
    Serial.println("Error response: " + error);
    http.end();
    drawScreen("API Error\n" + String(httpCode));
    return false;
  }
  
  String response = http.getString();
  http.end();
  
  Serial.println("Runware response:");
  Serial.println(response);
  
  // Parse image URL from response
  // Looking for "imageURL":"https://..."
  int urlIdx = response.indexOf("\"imageURL\"");
  if (urlIdx < 0) {
    // Try alternate key name
    urlIdx = response.indexOf("\"url\"");
  }
  
  if (urlIdx < 0) {
    Serial.println("ERROR: No image URL in response!");
    drawScreen("No image URL\nin response");
    return false;
  }
  
  int start = response.indexOf("http", urlIdx);
  if (start < 0) {
    Serial.println("ERROR: Could not find URL start!");
    drawScreen("Parse error");
    return false;
  }
  
  int end = response.indexOf("\"", start);
  if (end < 0) {
    Serial.println("ERROR: Could not find URL end!");
    drawScreen("Parse error");
    return false;
  }
  
  String imageUrl = response.substring(start, end);
  Serial.println("Image URL: " + imageUrl);
  
  // Download and display the image
  drawScreen("Downloading...");
  
  Serial.println("Fetching image from URL...");
  Serial.println(imageUrl);
  
  // Download JPEG data with fresh client
  WiFiClientSecure imgClient;
  imgClient.setInsecure();
  HTTPClient imgHttp;
  imgHttp.begin(imgClient, imageUrl);
  int imgHttpCode = imgHttp.GET();
  
  if (imgHttpCode != 200) {
    Serial.printf("ERROR: Image download failed with code %d\n", imgHttpCode);
    imgHttp.end();
    drawScreen("Download\nfailed");
    return false;
  }
  
  int imgSize = imgHttp.getSize();
  Serial.printf("Image size: %d bytes\n", imgSize);
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  if (imgSize <= 0 || imgSize > 80000) {
    Serial.println("ERROR: Invalid image size!");
    imgHttp.end();
    drawScreen("Invalid size");
    return false;
  }
  
  // Allocate buffer
  uint8_t* imgBuffer = (uint8_t*)malloc(imgSize);
  if (!imgBuffer) {
    Serial.println("ERROR: malloc failed!");
    imgHttp.end();
    drawScreen("Memory error");
    return false;
  }
  
  // Read all data
  WiFiClient* stream = imgHttp.getStreamPtr();
  size_t bytesRead = 0;
  while (bytesRead < (size_t)imgSize) {
    if (stream->available()) {
      imgBuffer[bytesRead++] = stream->read();
    }
  }
  imgHttp.end();
  
  Serial.printf("Downloaded %d bytes\n", bytesRead);
  Serial.printf("Header: %02X %02X %02X %02X\n", 
    imgBuffer[0], imgBuffer[1], imgBuffer[2], imgBuffer[3]);
  
  // Draw PNG
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.startWrite();
  bool result = M5.Display.drawPng(imgBuffer, (uint32_t)bytesRead, 0, 0, WIDTH, HEIGHT, 0, 0, 1.0f, 1.0f);
  M5.Display.endWrite();
  
  Serial.printf("drawPng returned: %d\n", result);
  
  free(imgBuffer);
  imageCount++;
  
  Serial.println("Draw complete");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=============================================\n");
  return true;
}

// =============== SETUP & LOOP ===============

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("     M5StickC Plus2 Runware Display");
  Serial.println("========================================\n");

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  
  // Seed random for UUID generation
  randomSeed(analogRead(0) + millis());

  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  // Try to connect with saved credentials
  if (!connectToWiFi()) {
    startAPMode();
    return;
  }

  drawScreen("Connected!\n\nFetching first\nimage...");
  Serial.println("\nReady! Images will refresh every 10 seconds.");
  Serial.println("Press A for immediate refresh.");
  Serial.println("Long press B to reset WiFi settings.\n");
  
  // Fetch first image immediately
  fetchRunwareImage();
  lastImageTime = millis();
}

void loop() {
  M5.update();

  // Handle AP mode web server
  if (apMode) {
    server.handleClient();
    
    // Still check for reset in AP mode
    if (M5.BtnB.pressedFor(3000)) {
      resetCredentials();
    }
    return;
  }

  // Long press B to reset WiFi credentials
  if (M5.BtnB.pressedFor(3000)) {
    resetCredentials();
  }

  // Button A for immediate image refresh
  if (M5.BtnA.wasClicked()) {
    Serial.println("\n*** BUTTON A - MANUAL REFRESH ***\n");
    fetchRunwareImage();
    lastImageTime = millis();
  }

  // Auto-refresh every 10 seconds
  if (millis() - lastImageTime >= IMAGE_INTERVAL) {
    Serial.println("\n*** AUTO REFRESH (10s interval) ***\n");
    fetchRunwareImage();
    lastImageTime = millis();
  }

  delay(20);
}
