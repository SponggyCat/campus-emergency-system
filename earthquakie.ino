#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// --- RADIO LIBRARIES ---
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// --- NETWORK SETTINGS ---
const char* ssid = "CAMPUS-SAFETY-192.168.4.1";
const char* password = "";

// --- HARDWARE PINS ---
#define PIN_BUZZER      2
#define PIN_SW420       32  

// --- RADIO PINS (Matches Transmitter Setup) ---
#define CE_PIN          17
#define CSN_PIN         5
// SCK  = 18
// MISO = 19
// MOSI = 23

// --- OBJECTS ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);
RF24 radio(CE_PIN, CSN_PIN);
HardwareSerial espSerial(1);

// --- RADIO SETTINGS ---
const uint64_t radioAddress = 0xE8E8F0F0E1LL; 

// --- STATUS VARIABLES ---
int local_Vibration = 0;
int liveShakeCount = 0; 
const int SHAKE_THRESHOLD = 23;
String uart_Gate1 = "SAFE";
String uart_Gate2 = "SAFE";
int    uart_Creek = 0;
// --- QUAKE CROSS-VALIDATION ---
const unsigned long QUAKE_CONFIRM_WINDOW = 5000; // both must fire within 5 seconds
unsigned long localQuakeTime  = 0;  // last time local SW420 triggered
unsigned long remoteQuakeTime = 0;  // last time remote radio triggered
bool confirmedQuake = false;


// Remote Status Variables (Updated via Radio)
int remote_Fire_Cook = 0; // Maps to room1
int remote_Fire_Cant = 0; // Maps to room2
int remote_Quake = 0;     // Maps to earthquake

unsigned long lastSensorRead = 0;


void handleData() {
    StaticJsonDocument<300> doc;

    doc["vibration"]       = liveShakeCount;       
    doc["confirmed_quake"] = confirmedQuake ? 1 : 0; // only 1 when BOTH sensors agree

    doc["d_cook"] = remote_Fire_Cook;
    doc["d_cant"] = remote_Fire_Cant;

    doc["gate1"] = uart_Gate1;
    doc["gate2"] = uart_Gate2;
    doc["creek"] = uart_Creek;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

// --- WEB SERVER: SERVE HTML ---
void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Index missing");
  }
}

// --- RADIO READING ---
void checkRadio() {
    if (radio.available()) {
        char incomingPayload[32] = "";
        radio.read(&incomingPayload, sizeof(incomingPayload));

        StaticJsonDocument<64> doc;
        DeserializationError error = deserializeJson(doc, incomingPayload);

        if (error) {
            Serial.print(F("JSON Parse Failed: "));
            Serial.println(error.f_str());
            return;
        }

        // r1/r2: 0=SAFE, 1=ORANGE, 2=RED
        int r1 = doc["r1"] | 0;
        int r2 = doc["r2"] | 0;
        int eq = doc["eq"] | 0;

        remote_Fire_Cook = (r1 >= 1) ? 1 : 0;  // ORANGE or RED = fire
        remote_Fire_Cant = (r2 >= 1) ? 1 : 0;
        remote_Quake     = (eq == 1) ? 1 : 0;
        Serial.printf("[RADIO] Updated -> Cook: %d, Cant: %d, Quake: %d\n", 
                      remote_Fire_Cook, remote_Fire_Cant, remote_Quake);
    }
}

void readSensors() {
    int currentReadCount = 0;
    unsigned long startCount = millis();

    while (millis() - startCount < 200) {
        server.handleClient();
        if (digitalRead(PIN_SW420) == HIGH) {
            Serial.println("sw420 high");
            currentReadCount++;
            delay(5);
        }
    }

    liveShakeCount = currentReadCount;
    local_Vibration = (liveShakeCount > SHAKE_THRESHOLD) ? 1 : 0;
}
// --- SENSOR READING ---
void processLogic() {
    bool alarmActive = false;
    String alarmMsg = "";

    // --- QUAKE CROSS-VALIDATION ---
    // Record the last time each sensor fired
    if (local_Vibration == 1)  localQuakeTime  = millis();
    if (remote_Quake == 1)     remoteQuakeTime = millis();

    // Only confirm if BOTH have fired within the window and at least once each
    confirmedQuake = (localQuakeTime > 0) &&
                     (remoteQuakeTime > 0) &&
                     (millis() - localQuakeTime  < QUAKE_CONFIRM_WINDOW) &&
                     (millis() - remoteQuakeTime < QUAKE_CONFIRM_WINDOW);

    // --- PRIORITY LOGIC (earthquake only triggers if confirmed) ---
    if      (confirmedQuake)        { alarmActive = true; alarmMsg = "EARTHQUAKE!"; }
    else if (remote_Fire_Cook == 1) { alarmActive = true; alarmMsg = "FIRE: COOKHOUSE"; }
    else if (remote_Fire_Cant == 1) { alarmActive = true; alarmMsg = "FIRE: CANTEEN"; }

    static String lastMsg  = "";
    static bool   lastAlarm = false;

    if (alarmActive) {
        if (alarmMsg != lastMsg) {
            lcd.clear();
            lcd.setCursor(0,0); lcd.print("!! WARNING !!");
            lcd.setCursor(0,1); lcd.print(alarmMsg);
            lastMsg = alarmMsg;
        }
        digitalWrite(PIN_BUZZER, HIGH);
        lastAlarm = true;
    } else {
        if (lastAlarm) {
            lcd.setCursor(0,0); lcd.print("SYSTEM READY    ");
            lcd.setCursor(0,1); lcd.print("MONITORING...   ");
            lastMsg = "";
        }
        digitalWrite(PIN_BUZZER, LOW);
        lastAlarm = false;
    }
}

void setup() {
  Serial.begin(115200);
  espSerial.begin(
    9600,
    SERIAL_8N1,
    16,     // RX pin (USED)
    -1      // TX pin (NOT USED)
  );
  
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SW420, INPUT);
  
  digitalWrite(PIN_BUZZER, LOW);

  lcd.init(); lcd.backlight();
  lcd.print("BOOTING SYSTEM");

  // --- RADIO SETUP ---
  // Explicitly start SPI on correct pins for ESP32 VSPI
  // SCK=18, MISO=19, MOSI=23, SS=5(CSN)
  SPI.begin(18, 19, 23, 5); 
  
  if (!radio.begin()) {
    Serial.println("Radio Hardware not responding!");
    lcd.setCursor(0,1); lcd.print("RADIO FAIL");
    delay(2000);
  }
  
  radio.openReadingPipe(1, radioAddress); 
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.startListening(); // Set as Receiver

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
  }

  // WiFi Setup
  WiFi.softAP(ssid, password);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.begin();
  
  delay(1000);
  lcd.clear(); 
}
String mapGateStatus(String raw) {
    if (raw.startsWith("RED"))    return "DANGER";
    if (raw.startsWith("ORANGE")) return "WARNING";
    if (raw.startsWith("YELLOW")) return "WARNING";
    return "SAFE"; // "GREEN (Normal)" or anything unexpected
}
void loop() {
  server.handleClient();
  
  // Check radio frequently (non-blocking)
  checkRadio();

  if (espSerial.available()) {
    String line = espSerial.readStringUntil('\n');
    Serial.println(espSerial.readStringUntil('\n'));
    // line looks like: "C:4|G1:GREEN (Normal)|G2:RED (Dangerous)"

    // Extract creek
    int cIdx = line.indexOf("C:");
    int cEnd = line.indexOf("|G1:");
    if (cIdx != -1 && cEnd != -1)
        uart_Creek = line.substring(cIdx + 2, cEnd).toInt();

    // Extract Gate 1
    int g1Idx = line.indexOf("G1:");
    int g1End = line.indexOf("|G2:");
    if (g1Idx != -1 && g1End != -1) {
        String raw = line.substring(g1Idx + 3, g1End);
        uart_Gate1 = mapGateStatus(raw); // see step 3
    }

    // Extract Gate 2
    int g2Idx = line.indexOf("G2:");
    if (g2Idx != -1) {
        String raw = line.substring(g2Idx + 3);
        raw.trim();
        uart_Gate2 = mapGateStatus(raw);
    }
}

  // Every 500ms, update sensours and screen
  if (millis() - lastSensorRead > 500) { 
    lastSensorRead = millis();
    readSensors();
    processLogic();
  }
}