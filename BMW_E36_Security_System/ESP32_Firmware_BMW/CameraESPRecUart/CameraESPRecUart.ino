#include "esp_camera.h"
#include <WiFi.h>

// Select camera model
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

#define LED_GPIO_NUM 4      
#define RELAY_PIN    13     

const char* ssid = "LOPEZ";
const char* password = "L21GIANNA";

//const char* ssid = "TIGO-22E1";
//const char* password = "2NJ555302863";

// --- CONFIGURACIÓN IP ---
IPAddress local_IP(192, 168, 0, 222);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);

void startCameraServer();
extern void app_facerecog_loop();

unsigned long relay_timer = 0;
bool relay_active = false;
const int RELAY_OPEN_TIME = 3000;

// Función para abrir la puerta
void openDoor(String nombreUsuario) {
  // Activar relé local
  digitalWrite(RELAY_PIN, HIGH);
  relay_active = true;
  relay_timer = millis();
  
  // Enviamos confirmación al ESP32 Principal
  Serial.print("FACE_MATCH:");
  Serial.println(nombreUsuario);
  
  // Log visual si está conectado a PC
  // Serial.println("Log: Puerta activada para " + nombreUsuario);
}

// Sobrecarga para apertura manual
void openDoor() {
  openDoor("Manual/Web");
}

void setup() {
  // Iniciamos Serial a 115200
  // IMPORTANTE: El pin U0RX (GPIO 3) debe estar conectado al TX (GPIO 1) del ESP32 Principal
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  
  s->set_framesize(s, FRAMESIZE_CIF);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  ledcSetup(LEDC_CHANNEL_4, 5000, 8);
  ledcAttachPin(LED_GPIO_NUM, LEDC_CHANNEL_4);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("Fallo al configurar IP Estática (STA)");
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  // 1. Lógica del Relé (Cierre automático)
  if (relay_active && (millis() - relay_timer > RELAY_OPEN_TIME)) {
    digitalWrite(RELAY_PIN, LOW);
    relay_active = false;
    // No imprimimos nada aquí para no ensuciar la comunicación serial con el maestro
  }

  // 2. NUEVA FUNCIÓN: Escuchar comandos del ESP32 Maestro (MQTT)
  if (Serial.available()) {
    String comandoRecibido = Serial.readStringUntil('\n');
    comandoRecibido.trim(); // Quitamos espacios y saltos de línea extra

    if (comandoRecibido == "CMD_ABRIR_PUERTA") {
      openDoor("Comando MQTT");
    }
  }

  // 3. Loop de reconocimiento facial
  app_facerecog_loop();
  
  delay(20); 
}