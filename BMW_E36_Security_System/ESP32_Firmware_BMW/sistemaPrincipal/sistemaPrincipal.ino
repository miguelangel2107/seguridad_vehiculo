/*
 * SISTEMA DE SEGURIDAD VEHICULAR BMW E36 - ESP32 MAESTRO
 * Integración Completa: A9G, Huella, OLED, Web App, CAMARA.
 * * CAMBIOS RECIENTES:
 * - Mensajes de registro de huella tambien en LCD.
 * - Comando TOGGLE_DATA para activar/desactivar envio de JSON.
 * - Serial Principal (UART0) remapeado para CAMARA.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
// SE ELIMINÓ LA LIBRERÍA CONFLICTIVA SoftwareSerial

// --- PINES ---
#define A9G_RX_PIN 26 
#define A9G_TX_PIN 27 

#define FINGER_RX_PIN 18 
#define FINGER_TX_PIN 19 

// CAMARA (Usaremos el UART0 remapeado aqui)
#define CAM_RX_PIN 34 // Leemos confirmacion de rostro
#define CAM_TX_PIN 21 // Enviamos comando ABRIR PUERTA

// RELÉS
#define RELAY_STARTER   32  
#define RELAY_IGNITION  2   // IGN
#define RELAY_ACC       33  // ACC

// LEDS
#define LED_LOCKED      25      
#define LED_RUNNING     14    
#define LED_ERROR       12       

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

// RED
const char* APN = "internet.tigo.bo";
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT = 1883; 
const char* MQTT_CLIENT_ID = "BMW_E36_David_ESP";

// --- OBJETOS ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HardwareSerial SerialA9G(2);     
HardwareSerial SerialFinger(1);  
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&SerialFinger);

// --- VARIABLES ---
int vehicleStage = 0; // 0:Locked, 1:ACC, 2:IGN, 3:Run
bool mqttEnabled = true;
bool jsonSendingEnabled = true; // Controla el envio periodico de JSON
int fingerprintFailCount = 0;
String currentGPS = "-16.5000,-68.1500"; 
String mqttState = "Disc";
String lastLog = "Init";
String serialBuffer[4]; 

int starterTimeMs = 800; 

// Timers
unsigned long lastBlink = 0;
unsigned long lastMQTTPing = 0;
unsigned long lastButtonPress = 0;
unsigned long lastStatusSend = 0;
bool ledState = false;

// MQTT Topics
const char* MQTT_TOPIC_PUB_JSON = "vehiculo/estado_json";
const char* MQTT_TOPIC_SUB = "vehiculo/control";

// Menu
enum MenuState { MAIN_MENU, SUB_MENU, CONFIG_TIME, SHOW_LOGS, ENROLL_FINGER };
MenuState currentMenuState = MAIN_MENU;
int mainMenuIndex = 0;
int subMenuIndex = 0;
const int MAIN_MENU_ITEMS = 5;
String mainMenuItems[] = {"Control", "Red/MQTT", "GPS", "Monitor", "Config"};

// Prototipos
void changeStage(int newStage);
void updateOutputs();
void sendStatusJSON();
void connectA9G_MQTT();
void requestGPS();
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

void setup() {
  // Serial Principal remapeado a pines de CAMARA
  Serial.begin(115200, SERIAL_8N1, CAM_RX_PIN, CAM_TX_PIN);
  
  pinMode(RELAY_STARTER, OUTPUT);
  pinMode(RELAY_IGNITION, OUTPUT);
  pinMode(RELAY_ACC, OUTPUT);
  pinMode(LED_LOCKED, OUTPUT);
  pinMode(LED_RUNNING, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  
  digitalWrite(RELAY_STARTER, LOW);  
  digitalWrite(RELAY_IGNITION, LOW); 
  digitalWrite(RELAY_ACC, LOW);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_CENTER, INPUT_PULLUP);

  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("BMW SECURITY");
  
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { }
  oled.clearDisplay(); oled.display();

  SerialA9G.begin(115200, SERIAL_8N1, A9G_RX_PIN, A9G_TX_PIN);
  SerialA9G.setTimeout(10); 
  
  SerialFinger.begin(57600, SERIAL_8N1, FINGER_RX_PIN, FINGER_TX_PIN);
  if (finger.verifyPassword()) logMsg("Huella OK");
  else logMsg("Huella ERROR");

  for(int i=0; i<4; i++) serialBuffer[i] = "";

  connectA9G_MQTT();
  delay(1000);
  changeStage(0); 
  logMsg("Sistema Listo");
}

void loop() {
  handleMenuNavigation();
  
  if (currentMenuState != ENROLL_FINGER) {
    handleA9G();          
    handleCamera();       
    handleFingerprint(); 
  }

  handleStatusLeds();
  handleMQTTKeepAlive();
}

// --- LOGICA PRINCIPAL ---

void changeStage(int newStage) {
  vehicleStage = newStage;
  updateOutputs();
  sendStatusJSON();
}

void updateOutputs() {
  switch (vehicleStage) {
    case 0: // LOCKED
      digitalWrite(RELAY_ACC, LOW); digitalWrite(RELAY_IGNITION, LOW);
      digitalWrite(RELAY_STARTER, LOW); digitalWrite(LED_RUNNING, LOW);
      updateLCD("SISTEMA", "BLOQUEADO");
      break;
    case 1: // ACC
      digitalWrite(RELAY_ACC, HIGH); digitalWrite(RELAY_IGNITION, LOW);
      digitalWrite(RELAY_STARTER, LOW); digitalWrite(LED_RUNNING, LOW);
      updateLCD("BIENVENIDO", "ACC ACTIVADO");
      break;
    case 2: // IGN
      digitalWrite(RELAY_ACC, HIGH); digitalWrite(RELAY_IGNITION, HIGH);
      digitalWrite(RELAY_STARTER, LOW); digitalWrite(LED_RUNNING, LOW);
      updateLCD("IGNICION ON", "LISTO ARRANQUE");
      break;
    case 3: // RUN
      digitalWrite(RELAY_ACC, HIGH); digitalWrite(RELAY_IGNITION, HIGH);
      digitalWrite(RELAY_STARTER, LOW); digitalWrite(LED_RUNNING, HIGH);
      updateLCD("MOTOR", "EN MARCHA");
      break;
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

// --- CAMARA ---
void handleCamera() {
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg.startsWith("FACE_MATCH:")) {
      String user = msg.substring(11);
      logMsg("Rostro: " + user);
      if (vehicleStage == 0) changeStage(1); // Desbloquea a ACC
    }
  }
}

// --- MENU OLED ---
void updateOLEDMenu() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  
  oled.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setCursor(2, 1);
  String st = (vehicleStage==0)?"LOCKED":(vehicleStage==1)?"ACC":(vehicleStage==2)?"IGN":"RUN";
  oled.print(st);
  oled.setCursor(60, 1); oled.print(mqttState);
  
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

// --- COMUNICACION MQTT/A9G ---

void sendStatusJSON() {
  if(!mqttEnabled || !jsonSendingEnabled) return; // Si toggle data esta off, no envia nada
  
  String modeStr = (vehicleStage==0)?"LOCKED":(vehicleStage==1)?"ACC_ON":(vehicleStage==2)?"IGN_ON":"RUNNING";
  String json = "{'mode':'"+modeStr+"','locked':"+String(vehicleStage==0)+",'acc':"+String(vehicleStage>=1)+",'ign':"+String(vehicleStage>=2)+",'run':"+String(vehicleStage==3)+",'gps':'"+currentGPS+"'}";
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
  else if (cmdLine.indexOf("GET_STATUS") != -1) sendStatusJSON();
}

void handleA9G() {
  if(!mqttEnabled) return;
  while (SerialA9G.available()) {
    String line = SerialA9G.readStringUntil('\n'); line.trim();
    if (line.length() > 0 && line.indexOf("+MQTTPUBLISH") != -1) processMQTTCommand(line);
  }
  if(millis() - lastStatusSend > 5000) { lastStatusSend = millis(); sendStatusJSON(); }
}

void connectA9G_MQTT() {
  logMsg("Conectando...");
  sendAT(SerialA9G, "ATE0", 500); 
  sendAT(SerialA9G, "AT+CGATT=1", 2000);
  sendAT(SerialA9G, "AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 1000);
  sendAT(SerialA9G, "AT+CGACT=1,1", 2000);
  sendAT(SerialA9G, "AT+MQTTCONN=\"" + String(MQTT_BROKER) + "\"," + String(MQTT_PORT) + ",\"" + String(MQTT_CLIENT_ID) + "\",120,0", 5000);
  sendAT(SerialA9G, "AT+MQTTSUB=\"" + String(MQTT_TOPIC_SUB) + "\",1,0", 2000);
  sendAT(SerialA9G, "AT+GPS=1", 1000);
  mqttState = "OK"; updateOLEDMenu(); logMsg("Online");
}

void requestGPS() {
  while(SerialA9G.available()) SerialA9G.read();
  SerialA9G.println("AT+LOCATION=2");
  long t = millis(); String resp = "";
  while (millis() - t < 2000) if (SerialA9G.available()) resp += (char)SerialA9G.read();
  
  if (resp.indexOf("GPS NOT FIX") >= 0) logMsg("GPS No Fix");
  else {
    int idx = resp.indexOf(",");
    if(idx > 0 && resp.length() > 10) {
        int firstDigit = -1;
        for(int i=0; i<resp.length(); i++) if(isDigit(resp.charAt(i)) || resp.charAt(i)=='-') { firstDigit = i; break; }
        if(firstDigit != -1) { currentGPS = resp.substring(firstDigit); currentGPS.trim(); }
    }
  }
  sendStatusJSON();
}

// --- UTILS ---
void resetA9GModule() { sendAT(SerialA9G, "AT+RST=1", 1000); connectA9G_MQTT(); }
void sendAT(Stream& serial, String cmd, int waitMs) { serial.println(cmd); delay(waitMs); }
void logMsg(String msg) {
  lastLog = msg;
  for(int i=0; i<3; i++) serialBuffer[i] = serialBuffer[i+1];
  serialBuffer[3] = msg.substring(0, 15);
}
void updateLCD(String line1, String line2) { lcd.clear(); lcd.setCursor(0,0); lcd.print(line1); lcd.setCursor(0,1); lcd.print(line2); }
void handleStatusLeds() {
  if (vehicleStage == 0) {
    if (millis() - lastBlink >= 500) { lastBlink = millis(); ledState = !ledState; digitalWrite(LED_LOCKED, ledState); }
  } else digitalWrite(LED_LOCKED, LOW);
}
void handleMQTTKeepAlive() { if (mqttEnabled && millis() - lastMQTTPing >= 30000) { lastMQTTPing = millis(); SerialA9G.println("AT+CSQ"); } }
void handleFingerprint() {
  if (vehicleStage == 0) return; 
  int p = finger.getImage(); if (p!=FINGERPRINT_OK) return;
  p = finger.image2Tz(); if (p!=FINGERPRINT_OK) return;
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    fingerprintFailCount = 0; logMsg("Huella OK");
    if (vehicleStage == 1) changeStage(2); 
    else if (vehicleStage == 2) performStarter();
    else if (vehicleStage == 3) changeStage(1);
  } else {
    fingerprintFailCount++; digitalWrite(LED_ERROR, HIGH); delay(500); digitalWrite(LED_ERROR, LOW);
    if (fingerprintFailCount >= 3) { changeStage(0); logMsg("Robo Detectado"); }
  }
}

// --- FUNCIONES HUELLA COMPLETAS ---
void deleteLastFingerprint() {
  int idToDelete = -1;
  oled.clearDisplay(); oled.setCursor(0,0); oled.print("Buscando Ultima..."); oled.display();
  updateLCD("BORRAR HUELLA", "BUSCANDO...");
  
  for (int i = 127; i >= 1; i--) {
      if (finger.loadModel(i) == FINGERPRINT_OK) { idToDelete = i; break; }
  }
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
  
  for (int i = 1; i <= 127; i++) {
    if (finger.loadModel(i) != FINGERPRINT_OK) { id = i; break; }
  }
  if (id == -1) { logMsg("Memoria Llena!"); updateLCD("ERROR", "MEMORIA LLENA"); return; }
  
  oled.clearDisplay(); oled.setCursor(0,0); oled.print("NUEVA ID:"); oled.print(id);
  oled.setCursor(0,20); oled.print("Poner dedo..."); oled.display();
  updateLCD("REGISTRO ID:" + String(id), "PONER DEDO");

  int p = -1; unsigned long start = millis();
  while (p != FINGERPRINT_OK) {
    if(millis() - start > 10000) return; 
    p = finger.getImage();
  }
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
  // Restaurar pantalla principal si no estamos en menu
  if(vehicleStage == 0) updateLCD("SISTEMA", "BLOQUEADO");
}