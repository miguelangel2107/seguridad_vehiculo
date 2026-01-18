/*
 * SISTEMA DE SEGURIDAD VEHICULAR BMW E36 - ESP32 MAESTRO V3.9.3
 * Integración: A9G, Huella, OLED, LCD, CAMARA, Bateria, Historial GPS.
 * * * CORRECCIÓN V3.9.3:
 * - Restauradas funciones CRÍTICAS parseCLK() y processGPSLine() que faltaban.
 * - Código completo (~950 líneas).
 * * * CAMBIOS PREVIOS:
 * - V3.9.2: Fix connectA9G.
 * - V3.9.1: Hora Real.
 * - V3.9: GPS Asíncrono.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <time.h> 

// --- PINES ---
// A9G
#define A9G_RX_PIN 26 
#define A9G_TX_PIN 27 

// HUELLA
#define FINGER_RX_PIN 18 
#define FINGER_TX_PIN 19 

// CAMARA (RX=34, TX=1/TX0)
#define CAM_RX_PIN 34 
#define CAM_TX_PIN 1  

// I/O
#define PIN_CHAPA_OUTPUT 14 
#define RELAY_STARTER   32   
#define RELAY_IGNITION  2    
#define RELAY_ACC       33   
#define LED_LOCKED       12 

// BOTONES
#define BTN_UP      13
#define BTN_DOWN    15
#define BTN_LEFT    4 
#define BTN_RIGHT   5 
#define BTN_CENTER  23

// PANTALLAS
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// CONFIGURACION
const char* APN = "internet.tigo.bo";
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT = 1883; 
const char* MQTT_CLIENT_ID = "BMW_E36_David_ESP_V2";

// --- ESTRUCTURAS DE DATOS ---
struct LocationPoint {
  unsigned long timestamp; 
  float lat;
  float lon;
  int speed;
};

const int HISTORY_SIZE = 100;
LocationPoint history[HISTORY_SIZE];
int historyHead = 0; 
bool historyFull = false;

// --- OBJETOS ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HardwareSerial SerialA9G(2);      
HardwareSerial SerialFinger(1);   
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&SerialFinger);

// --- VARIABLES GLOBALES ---
int vehicleStage = 0; 
bool mqttEnabled = true;
bool jsonSendingEnabled = true;
int fingerprintFailCount = 0;
String currentGPS = "-16.5000,-68.1500"; 
float lastLat = 0.0; 
float lastLon = 0.0;
String mqttState = "Disc";
String lastLog = "Init";
String serialBuffer[4]; 

int starterTimeMs = 800; 

// Variables Batería
float batteryVoltage = 0.0;
String batteryStatus = "UNKNOWN";

// --- TIEMPO REAL ---
time_t globalEpoch = 0; 
unsigned long lastEpochSync = 0; 
const long TIMEZONE_OFFSET = -14400; // -4 Horas (Bolivia)

// --- CONTROL DE SALUD (HEALTH CHECK) ---
// Displays
bool displaysEnabled = true;  
bool displaysHealthy = true;  
int i2cErrorCount = 0;

// Camara
bool cameraEnabled = true;    
bool cameraHealthy = true;
unsigned long lastCamHeartbeat = 0; 
int camErrorCount = 0;        

// Huella
bool fingerEnabled = true;    
bool fingerHealthy = true;
int fingerCommErrors = 0;
int fingerResetCount = 0;     

// Sistema
bool originalKeyEnabled = false; 
bool gpsEnabled = true; 
int mqttErrorCount = 0;

// Timers
unsigned long lastBlink = 0;
unsigned long lastMQTTPing = 0;
unsigned long lastButtonPress = 0;
unsigned long lastStatusSend = 0;
unsigned long lastConnCheck = 0; 
unsigned long lastDisplayCheck = 0; 
unsigned long lastHistorySave = 0; 
unsigned long lastGPSRequest = 0; 
unsigned long lastTimeRequest = 0; 
bool ledState = false;

// MQTT Topics
const char* MQTT_TOPIC_PUB_JSON = "vehiculo/estado_json";
const char* MQTT_TOPIC_PUB_HIST = "vehiculo/historial";
const char* MQTT_TOPIC_SUB = "vehiculo/control";

// Menu
enum MenuState { MAIN_MENU, SUB_MENU, CONFIG_TIME, SHOW_LOGS, ENROLL_FINGER };
MenuState currentMenuState = MAIN_MENU;
int mainMenuIndex = 0;
int subMenuIndex = 0;
const int MAIN_MENU_ITEMS = 5;
String mainMenuItems[] = {"Control", "Red/MQTT", "GPS", "Monitor", "Config"};

// --- PROTOTIPOS ---
void changeStage(int newStage);
void updateOutputs();
void sendStatusJSON();
void connectA9G_MQTT();
void requestGPS();
void requestTime(); 
void sendDoorCommand();
void logMsg(String msg);
void updateLCD(String line1, String line2);
void updateOLEDMenu(); 
void sendAT(Stream& serial, String cmd, int waitMs = 500);
void performStarter();
void deleteLastFingerprint();
void enrollFingerprintUI();
void resetA9GModule();
void processMQTTCommand(String line);
void handleA9G();
void handleCamera();
void handleFingerprint();
void handleStatusLeds(); 
void handleMQTTKeepAlive();
void handleMenuNavigation();
void executeAction();
void toggleWiFiCAM(bool state); 
void checkHardwareHealth(); 
void toggleDisplays(bool state);
void handleConnectionWatchdog(); 
void handleBatteryLogic(float volts); 
void saveLocationHistory(float lat, float lon, int speed); 
void sendHistoryMQTT(int count); 
void toggleGPS(bool state); 
void sendAlert(String type, String msg); 
void processGPSLine(String line); 
void parseCLK(String line); 

void setup() {
  Serial.begin(115200, SERIAL_8N1, CAM_RX_PIN, CAM_TX_PIN);
  Serial.setTimeout(20); 
  
  pinMode(RELAY_STARTER, OUTPUT);
  pinMode(RELAY_IGNITION, OUTPUT);
  pinMode(RELAY_ACC, OUTPUT);
  pinMode(LED_LOCKED, OUTPUT);
  pinMode(PIN_CHAPA_OUTPUT, OUTPUT); 
  digitalWrite(PIN_CHAPA_OUTPUT, LOW);
  
  digitalWrite(RELAY_STARTER, LOW);  
  digitalWrite(RELAY_IGNITION, LOW); 
  digitalWrite(RELAY_ACC, LOW);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);

  Wire.begin();
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    lcd.init(); lcd.backlight();
    lcd.setCursor(0, 0); lcd.print("BMW SECURITY");
  } else {
    displaysHealthy = false;
  }
  
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
  } else {
     oled.clearDisplay(); oled.display();
  }

  SerialA9G.begin(115200, SERIAL_8N1, A9G_RX_PIN, A9G_TX_PIN);
  SerialA9G.setTimeout(10); 
  
  SerialFinger.begin(57600, SERIAL_8N1, FINGER_RX_PIN, FINGER_TX_PIN);
  if (finger.verifyPassword()) {
    logMsg("Huella OK");
    fingerHealthy = true;
  } else {
    logMsg("Huella ERROR");
    fingerHealthy = false;
  }

  for(int i=0; i<4; i++) serialBuffer[i] = "";

  lastCamHeartbeat = millis(); 

  connectA9G_MQTT();
  delay(1000);
  changeStage(0); 
  logMsg("Sistema Listo");
}

void loop() {
  handleMenuNavigation();
  handleA9G();          
  handleCamera();       
  
  if (currentMenuState != ENROLL_FINGER) {
    handleFingerprint(); 
  }

  handleStatusLeds(); 
  handleMQTTKeepAlive();
  checkHardwareHealth();    
  handleConnectionWatchdog();

  // GPS Update (10s)
  if (gpsEnabled && (millis() - lastGPSRequest > 10000)) { 
    lastGPSRequest = millis();
    requestGPS();
  }

  // Time Sync Update (60s)
  if (mqttState == "OK" && (millis() - lastTimeRequest > 60000)) {
    lastTimeRequest = millis();
    requestTime();
  }
}

// --- LOGICA PRINCIPAL ---

void changeStage(int newStage) {
  vehicleStage = newStage;
  updateOutputs();
  sendStatusJSON();
}

void updateOutputs() {
  switch (vehicleStage) {
    case 0: 
      digitalWrite(RELAY_ACC, LOW); digitalWrite(RELAY_IGNITION, LOW);
      digitalWrite(RELAY_STARTER, LOW); 
      updateLCD("SISTEMA", "BLOQUEADO");
      break;
    case 1: 
      digitalWrite(RELAY_ACC, HIGH); digitalWrite(RELAY_IGNITION, LOW);
      digitalWrite(RELAY_STARTER, LOW); 
      updateLCD("BIENVENIDO", "ACC ACTIVADO");
      break;
    case 2: 
      digitalWrite(RELAY_ACC, HIGH); digitalWrite(RELAY_IGNITION, HIGH);
      digitalWrite(RELAY_STARTER, LOW); 
      updateLCD("IGNICION ON", "LISTO ARRANQUE");
      break;
    case 3: 
      digitalWrite(RELAY_ACC, HIGH); digitalWrite(RELAY_IGNITION, HIGH);
      digitalWrite(RELAY_STARTER, LOW); 
      updateLCD("MOTOR", "EN MARCHA");
      break;
  }
  
  if (vehicleStage == 0) {
      digitalWrite(PIN_CHAPA_OUTPUT, LOW);
  } else {
      if (originalKeyEnabled) digitalWrite(PIN_CHAPA_OUTPUT, HIGH); 
      else digitalWrite(PIN_CHAPA_OUTPUT, LOW);
  }

  updateOLEDMenu();
}

void performStarter() {
  if (vehicleStage != 2) return;
  updateLCD("ARRANCANDO", "...");
  digitalWrite(RELAY_STARTER, HIGH);
  delay(starterTimeMs);
  digitalWrite(RELAY_STARTER, LOW);
  changeStage(3);
}

void sendDoorCommand() {
  logMsg("CMD Puerta enviado");
  Serial.println("CMD_ABRIR_PUERTA"); 
  updateLCD("COMANDO", "ABRIR PUERTA");
  delay(500); 
  if(vehicleStage == 0) updateLCD("SISTEMA", "BLOQUEADO"); 
}

void checkHardwareHealth() {
  unsigned long now = millis();

  if (displaysEnabled && (now - lastDisplayCheck > 2000)) {
    lastDisplayCheck = now;
    Wire.beginTransmission(0x27);
    if (Wire.endTransmission() != 0) {
      i2cErrorCount++;
      if (i2cErrorCount >= 3) {
        displaysEnabled = false; 
        displaysHealthy = false;
        logMsg("DISP APAGADO (ERR)");
        sendAlert("DISPLAY_FAIL", "Pantallas Desactivadas por fallo");
      }
    } else {
      if (!displaysHealthy) {
        displaysHealthy = true;
        i2cErrorCount = 0;
        logMsg("Pantallas OK");
      }
      i2cErrorCount = 0;
    }
  }

  if (cameraEnabled && (now - lastCamHeartbeat > 25000)) {
     camErrorCount++;
     if (camErrorCount >= 3) {
         cameraEnabled = false; 
         cameraHealthy = false;
         logMsg("CAM APAGADA (ERR)");
         sendAlert("CAM_FAIL", "Camara Desactivada por timeout");
     } else {
         lastCamHeartbeat = now;
         logMsg("Camara Timeout " + String(camErrorCount));
     }
  }

  if (fingerEnabled && fingerResetCount >= 3) {
      fingerEnabled = false; 
      fingerHealthy = false;
      logMsg("HUELLA OFF (ERR)");
      sendAlert("FINGER_FAIL", "Sensor Huella Desactivado");
  }
}

void handleCamera() {
  if (!cameraEnabled) return; 

  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    
    if (msg.length() > 3) {
       lastCamHeartbeat = millis();
       camErrorCount = 0; 
       
       if (!cameraHealthy) {
          cameraHealthy = true;
          logMsg("Camara: ONLINE");
       }

       if (msg.startsWith("FACE_MATCH:")) {
          String user = msg.substring(11);
          logMsg("Rostro: " + user);
          if (vehicleStage == 0) {
              changeStage(1); 
              for(int i=0; i<3; i++) { digitalWrite(LED_LOCKED, HIGH); delay(100); digitalWrite(LED_LOCKED, LOW); delay(100); }
              sendAlert("FACE_UNLOCK", user); 
          }
       }
       else if (msg.startsWith("VOLT:")) {
          float v = msg.substring(5).toFloat();
          handleBatteryLogic(v);
       }
    }
  }
}

void handleFingerprint() {
  if (!fingerEnabled) return; 

  int p = finger.getImage();
  
  if (p == FINGERPRINT_PACKETRECIEVEERR) {
     fingerCommErrors++;
     if (fingerCommErrors > 5) {
        fingerResetCount++;
        fingerCommErrors = 0;
        fingerHealthy = false;
        logMsg("Huella Fallo " + String(fingerResetCount));
     }
     return;
  } else {
     fingerCommErrors = 0;
     fingerResetCount = 0; 
     fingerHealthy = true;
  }

  if (p != FINGERPRINT_OK) return;

  p = finger.image2Tz(); 
  if (p != FINGERPRINT_OK) return;

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    fingerprintFailCount = 0; logMsg("Huella OK");
    if (vehicleStage == 1) changeStage(2); 
    else if (vehicleStage == 2) performStarter();
    else if (vehicleStage == 3) changeStage(1);
  } else {
    fingerprintFailCount++; 
    for(int i=0; i<5; i++) { digitalWrite(LED_LOCKED, HIGH); delay(50); digitalWrite(LED_LOCKED, LOW); delay(50); }
    if (fingerprintFailCount >= 3) { changeStage(0); logMsg("Robo Detectado"); }
  }
}

// --- UTILS ---
void sendAlert(String type, String msg) {
   if(!mqttEnabled) return;
   String json;
   // JSON SIMPLE CON COMILLAS SIMPLES (COMPATIBLE A9G)
   if (type == "FACE_UNLOCK") {
      json = "{'alert':'" + type + "','user':'" + msg + "'}";
   } else {
      json = "{'alert':'" + type + "','msg':'" + msg + "'}";
   }
   sendAT(SerialA9G, "AT+MQTTPUB=\""+String(MQTT_TOPIC_PUB_JSON)+"\",\""+json+"\",0,0,0", 150);
}

void toggleWiFiCAM(bool state) {
  if(state) Serial.println("CMD_WIFI_ON");
  else Serial.println("CMD_WIFI_OFF");
  logMsg(state ? "Wifi CAM ON" : "Wifi CAM OFF");
}

void handleBatteryLogic(float volts) {
  batteryVoltage = volts;
  
  if (volts < 11.5) batteryStatus = "BAJA!";
  else if (volts <= 12.8) batteryStatus = "OFF";
  else if (volts <= 14.8) batteryStatus = "ON/CHG";
  else batteryStatus = "HIGH!";
  
  if (vehicleStage == 2 && volts > 13.2) {
      logMsg("Motor Detectado");
      changeStage(3); 
  }
  else if (vehicleStage == 3 && volts < 12.8) {
      logMsg("Motor Detenido");
      changeStage(2); 
  }
  
  static String lastBatStatus = "";
  if(lastBatStatus != batteryStatus) {
     if(currentMenuState == MAIN_MENU && displaysEnabled) updateOLEDMenu();
     lastBatStatus = batteryStatus;
  }
}

void toggleDisplays(bool state) {
  displaysEnabled = state;
  if (!displaysEnabled) {
    lcd.noBacklight();
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
  } else {
    Wire.begin();
    lcd.init(); lcd.backlight();
    lcd.setCursor(0,0); lcd.print("REACTIVADO");
    oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    oled.display();
    displaysHealthy = true; 
    i2cErrorCount = 0;
    logMsg("DISP RESET OK");
  }
}

void updateLCD(String line1, String line2) { 
  if(!displaysEnabled || !displaysHealthy) return;
  lcd.clear(); lcd.setCursor(0,0); lcd.print(line1); lcd.setCursor(0,1); lcd.print(line2); 
}

void handleConnectionWatchdog() {
  if (millis() - lastConnCheck < 30000) return;
  lastConnCheck = millis();

  if (mqttState != "OK" && mqttState != "Conn...") {
    static int retryCount = 0;
    retryCount++;
    logMsg("Watchdog: Recon " + String(retryCount));
    
    if (retryCount <= 3) {
      connectA9G_MQTT();
    } else {
      logMsg("RESET A9G (Fallo Conn)");
      resetA9GModule();
      retryCount = 0;
    }
  } 
}

void toggleGPS(bool state) {
  gpsEnabled = state;
  if(state) {
    sendAT(SerialA9G, "AT+GPS=1", 1000);
    logMsg("GPS: ACTIVADO");
  } else {
    sendAT(SerialA9G, "AT+GPS=0", 1000);
    logMsg("GPS: DESACTIVADO");
    currentGPS = "GPS OFF";
  }
  sendStatusJSON();
}

void sendStatusJSON() {
  if(!mqttEnabled || !jsonSendingEnabled) return;
  
  String modeStr = (vehicleStage==0)?"LOCKED":(vehicleStage==1)?"ACC_ON":(vehicleStage==2)?"IGN_ON":"RUNNING";
  String chapaStr = originalKeyEnabled ? "ON" : "OFF";
  String gpsStr = gpsEnabled ? "ON" : "OFF";
  
  String json = "{'mode':'"+modeStr+
                "','batt':'"+String(batteryVoltage, 1)+
                "','batt_st':'"+batteryStatus+
                "','chapa':'"+chapaStr+
                "','gps_st':'"+gpsStr+
                "','gps':'"+currentGPS+"'}";
                
  sendAT(SerialA9G, "AT+MQTTPUB=\""+String(MQTT_TOPIC_PUB_JSON)+"\",\""+json+"\",0,0,0", 150);
}

void processMQTTCommand(String line) {
  String cmdLine = line; cmdLine.toUpperCase(); 
  logMsg("MQTT CMD RX");
  
  if (cmdLine.indexOf("DESBLOQUEAR") != -1) changeStage(1);
  else if (cmdLine.indexOf("BLOQUEAR") != -1) changeStage(0);
  else if (cmdLine.indexOf("IGNICION") != -1) changeStage(2);
  else if (cmdLine.indexOf("ARRANCAR") != -1) { 
    if (vehicleStage == 2) performStarter();
    else if (vehicleStage == 1) { changeStage(2); delay(500); performStarter(); }
  }
  else if (cmdLine.indexOf("APAGAR") != -1) changeStage(1);
  else if (cmdLine.indexOf("ABRIR_PUERTA") != -1) sendDoorCommand();
  else if (cmdLine.indexOf("NUEVA_HUELLA") != -1) enrollFingerprintUI();
  else if (cmdLine.indexOf("TOGGLE_DATA") != -1) {
      jsonSendingEnabled = !jsonSendingEnabled;
      logMsg(jsonSendingEnabled ? "Datos: ON" : "Datos: OFF");
      updateLCD("DATOS MOVILES", jsonSendingEnabled ? "ACTIVADOS" : "DESACTIVADOS");
  }
  else if (cmdLine.indexOf("CMD_WIFI_ON") != -1) toggleWiFiCAM(true);   
  else if (cmdLine.indexOf("CMD_WIFI_OFF") != -1) toggleWiFiCAM(false); 
  else if (cmdLine.indexOf("DISPLAY_OFF_ALL") != -1) toggleDisplays(false); 
  else if (cmdLine.indexOf("DISPLAY_ON_ALL") != -1) toggleDisplays(true);   
  else if (cmdLine.indexOf("ENABLE_KEY_LOGIC") != -1) { 
      originalKeyEnabled = true;
      logMsg("Llave Manual: ON");
      updateOutputs(); 
      sendStatusJSON();
  } 
  else if (cmdLine.indexOf("DISABLE_KEY_LOGIC") != -1) { 
      originalKeyEnabled = false;
      logMsg("Llave Manual: OFF");
      updateOutputs(); 
      sendStatusJSON();
  }
  else if (cmdLine.indexOf("ENABLE_CAM") != -1) {
      cameraEnabled = true;
      camErrorCount = 0;
      lastCamHeartbeat = millis(); 
      logMsg("Camara: RESET");
  }
  else if (cmdLine.indexOf("ENABLE_FINGER") != -1) {
      fingerEnabled = true;
      fingerResetCount = 0;
      logMsg("Huella: RESET");
  }
  
  else if (cmdLine.indexOf("CMD_GPS_ON") != -1) toggleGPS(true);
  else if (cmdLine.indexOf("CMD_GPS_OFF") != -1) toggleGPS(false);
  else if (cmdLine.indexOf("GET_HISTORY_ALL") != -1) sendHistoryMQTT(HISTORY_SIZE);
  else if (cmdLine.indexOf("GET_HISTORY_LAST_50") != -1) sendHistoryMQTT(50);
  else if (cmdLine.indexOf("CLEAR_HISTORY") != -1) { historyHead = 0; historyFull = false; logMsg("Hist. Borrado"); }
  else if (cmdLine.indexOf("GET_STATUS") != -1) sendStatusJSON();
}

void handleA9G() {
  if(!mqttEnabled) return;
  while (SerialA9G.available()) {
    String line = SerialA9G.readStringUntil('\n'); line.trim();
    if (line.length() > 0) {
       if (line.indexOf("+MQTTPUBLISH") != -1) {
           processMQTTCommand(line);
           mqttState = "OK"; 
           mqttErrorCount = 0; 
       }
       else if (line.indexOf("RDY") != -1 || line.indexOf("+CIEV") != -1) {
           mqttState = "Disc";
           logMsg("A9G Reiniciado!!");
           mqttErrorCount = 5; 
       }
       else if (line.indexOf("MQTT DISCONNECTED") != -1) {
           mqttState = "Disc";
       }
       else if (line.indexOf("ERROR") != -1) {
           mqttErrorCount++;
           if(mqttErrorCount >= 5) {
               mqttState = "Error"; 
               mqttErrorCount = 0;
               logMsg("Fallo Conn (Errors)");
           }
       }
       else if (line == "OK") {
           if(mqttErrorCount > 0) mqttErrorCount--;
       }
       else if (line.indexOf("+CCLK:") != -1) { 
           parseCLK(line);
       }
       else {
           processGPSLine(line);
       }
    }
  }
  if(millis() - lastStatusSend > 5000) { lastStatusSend = millis(); sendStatusJSON(); }
}

// --- PARSERS RESTAURADOS ---
void parseCLK(String line) {
    int q1 = line.indexOf('"');
    if (q1 > 0) {
        String dt = line.substring(q1 + 1); 
        if (dt.length() >= 17) {
            struct tm t;
            t.tm_year = dt.substring(0, 2).toInt() + 100;
            t.tm_mon = dt.substring(3, 5).toInt() - 1;
            t.tm_mday = dt.substring(6, 8).toInt();
            t.tm_hour = dt.substring(9, 11).toInt();
            t.tm_min = dt.substring(12, 14).toInt();
            t.tm_sec = dt.substring(15, 17).toInt();
            t.tm_isdst = 0;
            
            time_t utcEpoch = mktime(&t);
            if (utcEpoch > 0) {
                globalEpoch = utcEpoch + TIMEZONE_OFFSET; 
                lastEpochSync = millis();
            }
        }
    }
}

void processGPSLine(String line) {
    // Parser GPS (Ignora ecos y errores)
    if (line.indexOf(',') > 0 && line.length() > 10 && line.indexOf('{') == -1 && line.indexOf("AT+") == -1) {
        bool validChars = true;
        for(int i=0; i<line.length(); i++) {
            char c = line.charAt(i);
            if (!isDigit(c) && c != ',' && c != '.' && c != '-') {
                validChars = false; 
                break;
            }
        }
        
        if (validChars) {
            if (line.indexOf("0.0,0.0") == -1) { 
                currentGPS = line;
                int cIdx = currentGPS.indexOf(',');
                float lat = currentGPS.substring(0, cIdx).toFloat();
                float lon = currentGPS.substring(cIdx+1).toFloat();
                saveLocationHistory(lat, lon, 0); 
            }
        }
    }
}

// --- FUNCIONES QUE FALTABAN ---
void connectA9G_MQTT() {
  logMsg("Conectando...");
  mqttState = "Conn..."; 
  mqttErrorCount = 0; 
  updateOLEDMenu();
  
  sendAT(SerialA9G, "ATE0", 500); 
  sendAT(SerialA9G, "AT+CGATT=1", 2000);
  sendAT(SerialA9G, "AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 1000);
  sendAT(SerialA9G, "AT+CGACT=1,1", 2000);
  sendAT(SerialA9G, "AT+MQTTCONN=\"" + String(MQTT_BROKER) + "\"," + String(MQTT_PORT) + ",\"" + String(MQTT_CLIENT_ID) + "\",120,0", 5000);
  sendAT(SerialA9G, "AT+MQTTSUB=\"" + String(MQTT_TOPIC_SUB) + "\",1,0", 2000);
  
  if(gpsEnabled) sendAT(SerialA9G, "AT+GPS=1", 1000);
  else sendAT(SerialA9G, "AT+GPS=0", 1000);
  
  mqttState = "OK"; 
  updateOLEDMenu(); 
  logMsg("Online");
}

void requestTime() {
    SerialA9G.println("AT+CCLK?");
}

void requestGPS() {
  if(!gpsEnabled) return; 
  SerialA9G.println("AT+LOCATION=2");
}

void resetA9GModule() { 
  logMsg("Resetting A9G...");
  sendAT(SerialA9G, "AT+RST=1", 1000); 
  mqttState = "Reset";
  delay(12000); 
  connectA9G_MQTT(); 
}

void sendAT(Stream& serial, String cmd, int waitMs) { serial.println(cmd); delay(waitMs); }

void logMsg(String msg) {
  lastLog = msg;
  for(int i=0; i<3; i++) serialBuffer[i] = serialBuffer[i+1];
  serialBuffer[3] = msg.substring(0, 15);
}

void handleStatusLeds() {
  if (vehicleStage == 0) {
    if (millis() - lastBlink >= 800) { 
        lastBlink = millis(); 
        ledState = !ledState; 
        digitalWrite(LED_LOCKED, ledState); 
    }
  } else {
    digitalWrite(LED_LOCKED, LOW);
  }
}

void handleMQTTKeepAlive() { if (mqttEnabled && millis() - lastMQTTPing >= 30000) { lastMQTTPing = millis(); SerialA9G.println("AT+CSQ"); } }

void deleteLastFingerprint() {
  int idToDelete = -1;
  oled.clearDisplay(); oled.setCursor(0,0); oled.print("Buscando Ultima..."); oled.display();
  updateLCD("BORRAR HUELLA", "BUSCANDO...");
  for (int i = 127; i >= 1; i--) { if (finger.loadModel(i) == FINGERPRINT_OK) { idToDelete = i; break; } }
  oled.setCursor(0,20); 
  if (idToDelete != -1) {
      if (finger.deleteModel(idToDelete) == FINGERPRINT_OK) {
          oled.print("Borrado ID: "); oled.print(idToDelete); logMsg("Borrada ID:"+String(idToDelete));
          updateLCD("HUELLA BORRADA", "ID: " + String(idToDelete));
      } else { oled.print("Error Borrando"); updateLCD("ERROR", "FALLO BORRADO"); }
  } else { oled.print("Base Vacia"); updateLCD("ERROR", "BASE VACIA"); }
  oled.display(); delay(2000);
}

void enrollFingerprintUI() {
  int id = -1; 
  oled.clearDisplay(); oled.setCursor(0,0); oled.print("Buscando ID..."); oled.display();
  updateLCD("NUEVA HUELLA", "BUSCANDO ID...");
  for (int i = 1; i <= 127; i++) { if (finger.loadModel(i) != FINGERPRINT_OK) { id = i; break; } }
  if (id == -1) { logMsg("Memoria Llena!"); updateLCD("ERROR", "MEMORIA LLENA"); return; }
  
  oled.clearDisplay(); oled.setCursor(0,0); oled.print("NUEVA ID:"); oled.print(id);
  oled.setCursor(0,20); oled.print("Poner dedo..."); oled.display();
  updateLCD("REGISTRO ID:" + String(id), "PONER DEDO");

  int p = -1; unsigned long start = millis();
  while (p != FINGERPRINT_OK) { if(millis() - start > 10000) return; p = finger.getImage(); }
  p = finger.image2Tz(1); if (p != FINGERPRINT_OK) return;

  oled.setCursor(0,40); oled.print("Quitar dedo"); oled.display(); 
  updateLCD("REGISTRO", "QUITAR DEDO");
  delay(2000);
  p = 0; while (p != FINGERPRINT_NOFINGER) { p = finger.getImage(); }

  oled.setCursor(0,40); oled.print("Poner mismo dedo"); oled.display();
  updateLCD("REGISTRO", "CONFIRMAR DEDO");
  p = -1; start = millis();
  while (p != FINGERPRINT_OK) { if(millis() - start > 10000) return; p = finger.getImage(); }

  p = finger.image2Tz(2); if (p != FINGERPRINT_OK) return;
  p = finger.createModel(); if (p != FINGERPRINT_OK) return;
  p = finger.storeModel(id);
  
  if (p == FINGERPRINT_OK) {
    oled.clearDisplay(); oled.setCursor(10, 20); oled.print("GUARDADA!"); oled.display(); 
    updateLCD("EXITO", "HUELLA GUARDADA");
    delay(2000);
  } else {
    logMsg("Error Guardar");
    updateLCD("ERROR", "FALLO GUARDADO");
  }
  updateOLEDMenu();
  if(vehicleStage == 0) updateLCD("SISTEMA", "BLOQUEADO");
}

void updateOLEDMenu() {
  if(!displaysEnabled || !displaysHealthy) return;

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  
  oled.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setCursor(2, 1);
  String st = (vehicleStage==0)?"LCK":(vehicleStage==1)?"ACC":(vehicleStage==2)?"IGN":"RUN";
  oled.print(st);
  oled.setCursor(40, 1); oled.print(batteryStatus); 
  oled.setCursor(90, 1); oled.print(mqttState);
  
  oled.setTextColor(SSD1306_WHITE);
  int yStart = 14;

  if (currentMenuState == MAIN_MENU) {
    for (int i = 0; i < MAIN_MENU_ITEMS; i++) {
      if (i == mainMenuIndex) {
        oled.fillRect(0, yStart + (i*10), 128, 10, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
      } else oled.setTextColor(SSD1306_WHITE);
      oled.setCursor(5, yStart + (i*10) + 1); oled.print(mainMenuItems[i]);
    }
  }
  else if (currentMenuState == SUB_MENU) {
      oled.setCursor(0, yStart-2);
      oled.print("< " + mainMenuItems[mainMenuIndex]); 
      oled.drawLine(0, yStart+8, 128, yStart+8, SSD1306_WHITE);
      yStart += 10;
      
      String subItems[5];
      int count = 0;
      
      if (mainMenuIndex == 0) { // CONTROL
          subItems[0]="Desbloquear"; subItems[1]="Bloquear"; subItems[2]="Ignicion"; subItems[3]="Arrancar"; subItems[4]="Puerta"; count=5;
      } else if (mainMenuIndex == 1) { // RED
          subItems[0]="Conectar"; subItems[1]="Reset A9G"; count=2;
      } else if (mainMenuIndex == 2) { // GPS
          subItems[0]="Ver GPS"; subItems[1]="Enviar MQTT"; count=2;
      } else if (mainMenuIndex == 3) { 
      } else if (mainMenuIndex == 4) { // CONFIG
          subItems[0]="T.Arranque"; subItems[1]="Nueva Huella"; subItems[2]="Borrar Ult."; count=3;
      }

      for (int i = 0; i < count; i++) {
          if (i == subMenuIndex) {
            oled.fillRect(0, yStart + (i*10), 128, 10, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
          } else oled.setTextColor(SSD1306_WHITE);
          oled.setCursor(5, yStart + (i*10) + 1); oled.print(subItems[i]);
      }
  }
  else if (currentMenuState == CONFIG_TIME) {
    oled.setCursor(0, yStart); oled.println("< TIEMPO ARRANQUE");
    oled.setTextSize(2); oled.setCursor(20, 35); 
    oled.print(starterTimeMs); oled.print(" ms");
  }
  else if (currentMenuState == SHOW_LOGS) {
     for(int i=0; i<4; i++) {
       oled.setCursor(0, yStart + (i*10)); oled.print(serialBuffer[i]);
     }
  }
  oled.display();
}

void executeAction() {
  if (mainMenuIndex == 0) { // CONTROL
     if (subMenuIndex == 0) changeStage(1);
     else if (subMenuIndex == 1) changeStage(0);
     else if (subMenuIndex == 2) changeStage(2);
     else if (subMenuIndex == 3) performStarter();
     else if (subMenuIndex == 4) sendDoorCommand(); 
  }
  else if (mainMenuIndex == 1) { // RED
     if(subMenuIndex == 0) connectA9G_MQTT();
     else if(subMenuIndex == 1) resetA9GModule();
  }
  else if (mainMenuIndex == 2) { // GPS
     requestGPS();
  }
  else if (mainMenuIndex == 4) { // CONFIG
     if(subMenuIndex == 0) currentMenuState = CONFIG_TIME;
     else if(subMenuIndex == 1) enrollFingerprintUI();
     else if(subMenuIndex == 2) deleteLastFingerprint();
  }
}

void handleMenuNavigation() {
  if (millis() - lastButtonPress < 200) return;
  bool update = false;
  if (digitalRead(BTN_UP) == LOW) {
    if (currentMenuState == MAIN_MENU) { mainMenuIndex--; if (mainMenuIndex < 0) mainMenuIndex = MAIN_MENU_ITEMS - 1; }
    else if (currentMenuState == SUB_MENU) { subMenuIndex--; if (subMenuIndex < 0) subMenuIndex = 0; }
    else if (currentMenuState == CONFIG_TIME) { starterTimeMs += 50; if (starterTimeMs > 3000) starterTimeMs = 3000; }
    update = true; lastButtonPress = millis();
  }
  if (digitalRead(BTN_DOWN) == LOW) {
    if (currentMenuState == MAIN_MENU) { mainMenuIndex++; if (mainMenuIndex >= MAIN_MENU_ITEMS) mainMenuIndex = 0; }
    else if (currentMenuState == SUB_MENU) { subMenuIndex++; } 
    else if (currentMenuState == CONFIG_TIME) { starterTimeMs -= 50; if (starterTimeMs < 100) starterTimeMs = 100; }
    update = true; lastButtonPress = millis();
  }
  if (digitalRead(BTN_RIGHT) == LOW) { 
    if (currentMenuState == MAIN_MENU) {
      if (mainMenuIndex == 3) currentMenuState = SHOW_LOGS;
      else { currentMenuState = SUB_MENU; subMenuIndex = 0; }
      update = true;
    }
    lastButtonPress = millis();
  }
  if (digitalRead(BTN_LEFT) == LOW) { 
    currentMenuState = MAIN_MENU; update = true; lastButtonPress = millis();
  }
  if (digitalRead(BTN_CENTER) == LOW) { 
    if (currentMenuState == SUB_MENU) { executeAction(); update = true; }
    else if (currentMenuState == CONFIG_TIME) { currentMenuState = SUB_MENU; update = true; }
    lastButtonPress = millis();
  }
  if (update) updateOLEDMenu();
}

void saveLocationHistory(float lat, float lon, int speed) {
  unsigned long now = millis();
  
  unsigned long timestampToSave;
  if (globalEpoch > 0) {
      timestampToSave = globalEpoch + (now - lastEpochSync) / 1000;
  } else {
      timestampToSave = now;
  }

  bool moved = (abs(lat - lastLat) > 0.0001 || abs(lon - lastLon) > 0.0001);
  bool timeElapsed = (now - lastHistorySave > 300000); 
  
  if (moved || (speed > 5 && timeElapsed)) {
    history[historyHead].timestamp = timestampToSave; 
    history[historyHead].lat = lat;
    history[historyHead].lon = lon;
    history[historyHead].speed = speed;
    historyHead = (historyHead + 1) % HISTORY_SIZE;
    if (historyHead == 0) historyFull = true;
    lastLat = lat; lastLon = lon;
    lastHistorySave = now;
  }
}

void sendHistoryMQTT(int count) {
  if (!mqttEnabled) return;
  if (mqttState != "OK") {
      logMsg("Hist. Pendiente (No Conn)");
      return; 
  }

  int availableItems = historyFull ? HISTORY_SIZE : historyHead;
  
  if (availableItems == 0) {
      logMsg("Historial Vacio");
      delay(200); 
      sendAT(SerialA9G, "AT+MQTTPUB=\""+String(MQTT_TOPIC_PUB_JSON)+"\",\"{'alert':'INFO','msg':'Historial Vacio'}\",0,0,0", 250);
      return;
  }

  int startIdx = historyHead - count;
  if (startIdx < 0) startIdx += HISTORY_SIZE; 
  
  if (count > availableItems) count = availableItems;

  int currentIdx = historyHead - count;
  if (currentIdx < 0) currentIdx += HISTORY_SIZE;

  for (int i = 0; i < count; i++) {
    int idx = (currentIdx + i) % HISTORY_SIZE;
    
    String json = "{'t':" + String(history[idx].timestamp) + 
                  ",'lat':" + String(history[idx].lat, 6) + 
                  ",'lon':" + String(history[idx].lon, 6) + "}";
    
    sendAT(SerialA9G, "AT+MQTTPUB=\""+String(MQTT_TOPIC_PUB_HIST)+"\",\""+json+"\",0,0,0", 100);
    delay(300); 
  }
  logMsg("Historial Enviado");
}
