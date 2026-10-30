/*
 * SEMANA 12 - Monitor de Aula Conectado (Blynk)
 * Proyecto Integrador: Monitoreo Inteligente de Aula
 *
 * Base: codigo de la Semana 10 (WiFi + millis + histeresis + MQTT).
 * Nuevo: broker cloud autenticado (blynk.cloud) y control remoto
 *        del LED rojo y del buzzer desde el dashboard.
 *
 * Librerias: PubSubClient (Nick O'Leary), DHT sensor library (Adafruit)
 *            + Adafruit Unified Sensor.
 * Credenciales: se leen de config.h (NO se sube a GitHub).
 *
 * Datastreams en Blynk (el nombre debe coincidir EXACTO):
 *   temperatura (Double)  -> ESP32 publica
 *   humedad     (Double)  -> ESP32 publica
 *   luminosidad (Integer) -> ESP32 publica
 *   led_rojo    (Integer 0/1) -> dashboard controla
 *   buzzer      (Integer 0/1) -> dashboard controla
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "DHT.h"
#include "config.h"

// ==========================================
// 1. CONFIGURACION
// ==========================================
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* mqtt_server = "blynk.cloud";

#if BLYNK_USAR_TLS
  const int mqtt_port = 8883;      // MQTT sobre TLS
  WiFiClientSecure espClient;
#else
  const int mqtt_port = 1883;      // sin cifrado (solo pruebas)
  WiFiClient espClient;
#endif

// Topicos de Blynk (usan el NOMBRE del datastream)
const char* topic_temp      = "ds/temperatura";
const char* topic_hum       = "ds/humedad";
const char* topic_luz       = "ds/luminosidad";
const char* topic_dl_led    = "downlink/ds/led_rojo";   // comandos desde el dashboard
const char* topic_dl_buzzer = "downlink/ds/buzzer";     // comandos desde el dashboard
const char* topic_get       = "get/ds";                 // pedir ultimo valor guardado

// ==========================================
// 2. PINES
// ==========================================
#define DHTPIN 4
#define DHTTYPE DHT11
#define LDR_PIN 34
#define LED_AMARILLO 5
#define LED_ROJO 18
#define LED_VERDE 19
#define LED_BLANCO 22
#define BUZZER_PIN 23

DHT dht(DHTPIN, DHTTYPE);
PubSubClient client(espClient);
String clientId;

// ==========================================
// 3. VARIABLES DE TIEMPO (millis)
// ==========================================
unsigned long tiempoAnteriorDHT = 0;
unsigned long tiempoAnteriorAmarillo = 0;
unsigned long tiempoAnteriorBuzzer = 0;
unsigned long tiempoAnteriorWiFi = 0;
unsigned long tiempoAnteriorMQTT = 0;
unsigned long ultimoEnvio = 0;
const unsigned long INTERVALO_ENVIO = INTERVALO_ENVIO_MS;

bool estadoAmarillo = LOW;
bool estadoBuzzer = LOW;
bool ventilacionActiva = false;
bool datosValidos = false;   // true cuando ya hubo una lectura correcta del DHT11

// Ordenes recibidas desde el dashboard (interruptores)
bool remotoLedRojo = false;
bool remotoBuzzer = false;

float humedad = 0.0;
float temperatura = 0.0;
int valorLuminosidad = 0;

// ==========================================
// 4. WIFI
// ==========================================
void conectarWiFi() {
  Serial.println("\n=== Conexion WiFi ESP32 ===");
  Serial.print("Conectando a: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] WiFi conectado");
    Serial.print("Direccion IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n[ERROR] No se pudo conectar");
    Serial.print("Estado: ");
    Serial.println(WiFi.status());
  }
}

// ==========================================
// 5. MQTT (BLYNK)
// ==========================================

// Callback: se ejecuta cuando llega un comando desde el dashboard
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();
  bool encendido = (msg.toFloat() > 0);

  if (strcmp(topic, topic_dl_led) == 0) {
    remotoLedRojo = encendido;
    Serial.print("[CMD] LED rojo desde el dashboard -> ");
    Serial.println(encendido ? "ON" : "OFF");
  } else if (strcmp(topic, topic_dl_buzzer) == 0) {
    remotoBuzzer = encendido;
    Serial.print("[CMD] Buzzer desde el dashboard -> ");
    Serial.println(encendido ? "ON" : "OFF");
  }
}

// Intento unico (no bloqueante). Se llama cada 5 s desde loop().
void reconectarMQTT() {
  Serial.print("[MQTT] Conectando a ");
  Serial.print(mqtt_server);
  Serial.print("... ");

  // Blynk: usuario fijo "device" y contrasena = Auth Token
  if (client.connect(clientId.c_str(), "device", BLYNK_AUTH_TOKEN)) {
    Serial.println("OK");
    client.subscribe(topic_dl_led);
    client.subscribe(topic_dl_buzzer);
    // Pedir a Blynk el ultimo estado conocido de los interruptores
    client.publish(topic_get, "led_rojo,buzzer");
  } else {
    Serial.print("Fallo, rc=");
    Serial.println(client.state());
  }
}

void publicarDatos() {
  char buf[16];

  dtostrf(temperatura, 1, 1, buf);
  client.publish(topic_temp, buf);

  dtostrf(humedad, 1, 1, buf);
  client.publish(topic_hum, buf);

  snprintf(buf, sizeof(buf), "%d", valorLuminosidad);
  client.publish(topic_luz, buf);

  Serial.println("--- Datos publicados ---");
  Serial.print("[MQTT] "); Serial.print(topic_temp); Serial.print(" -> "); Serial.println(temperatura, 1);
  Serial.print("[MQTT] "); Serial.print(topic_hum);  Serial.print(" -> "); Serial.println(humedad, 1);
  Serial.print("[MQTT] "); Serial.print(topic_luz);  Serial.print(" -> "); Serial.println(valorLuminosidad);
}

// ==========================================
// 6. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  clientId = "ESP32_AulaUTS_" + String((uint32_t)ESP.getEfuseMac(), HEX);

  conectarWiFi();

#if BLYNK_USAR_TLS
  espClient.setInsecure();   // TLS sin validar certificado (suficiente para la practica)
#endif

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(30);

  dht.begin();

  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_BLANCO, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_BLANCO, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  if (WiFi.status() == WL_CONNECTED) {
    reconectarMQTT();
  }

  Serial.println("Sistema Iniciado. Temporizadores, Histeresis, WiFi y Blynk activos.");
}

// ==========================================
// 7. LOOP
// ==========================================
void loop() {
  unsigned long tiempoActual = millis();

  // --- Reconexion WiFi cada 10 s ---
  if ((WiFi.status() != WL_CONNECTED) && (tiempoActual - tiempoAnteriorWiFi >= 10000)) {
    Serial.println("\n[ALERTA] Conexion WiFi perdida. Intentando reconectar...");
    WiFi.disconnect();
    WiFi.reconnect();
    tiempoAnteriorWiFi = tiempoActual;
  }

  // --- Mantener sesion MQTT (sin bloquear los LEDs) ---
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      if (tiempoActual - tiempoAnteriorMQTT >= 5000) {
        tiempoAnteriorMQTT = tiempoActual;
        reconectarMQTT();
      }
    } else {
      client.loop();
    }
  }

  // --- Lectura de sensores cada 2 s ---
  if (tiempoActual - tiempoAnteriorDHT >= 2000) {
    tiempoAnteriorDHT = tiempoActual;

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h) && !isnan(t)) {
      humedad = h;
      temperatura = t;
      valorLuminosidad = analogRead(LDR_PIN);
      datosValidos = true;

      Serial.print("Temp: "); Serial.print(temperatura);
      Serial.print(" C | Hum: "); Serial.print(humedad);
      Serial.print(" % | Luz: "); Serial.println(valorLuminosidad);
    } else {
      Serial.println("Error: Fallo al leer el sensor DHT11!");
    }
  }

  // --- Publicacion periodica ---
  if (client.connected() && datosValidos && (tiempoActual - ultimoEnvio >= INTERVALO_ENVIO)) {
    ultimoEnvio = tiempoActual;
    publicarDatos();
  }

  // ==========================================
  // LOGICA DE CONTROL (S10) + ORDENES REMOTAS
  // ==========================================
  if (temperatura > 30.0) {
    ventilacionActiva = true;
  } else if (temperatura <= 26.0) {
    ventilacionActiva = false;
  }
  // LED rojo: se enciende por histeresis O por el interruptor del dashboard
  digitalWrite(LED_ROJO, (ventilacionActiva || remotoLedRojo) ? HIGH : LOW);

  if (!ventilacionActiva && humedad >= 40.0 && humedad <= 75.0) {
    digitalWrite(LED_VERDE, HIGH);
  } else {
    digitalWrite(LED_VERDE, LOW);
  }

  if (valorLuminosidad < 1000) {
    digitalWrite(LED_BLANCO, HIGH);
  } else {
    digitalWrite(LED_BLANCO, LOW);
  }

  if (humedad > 75.0) {
    if (tiempoActual - tiempoAnteriorAmarillo >= 300) {
      tiempoAnteriorAmarillo = tiempoActual;
      estadoAmarillo = !estadoAmarillo;
      digitalWrite(LED_AMARILLO, estadoAmarillo);
    }
  } else if (humedad < 40.0) {
    digitalWrite(LED_AMARILLO, HIGH);
  } else {
    digitalWrite(LED_AMARILLO, LOW);
  }

  // Buzzer: alarma local (S10) O interruptor del dashboard
  if (temperatura > 32.0) {
    if (tiempoActual - tiempoAnteriorBuzzer >= 200) {
      tiempoAnteriorBuzzer = tiempoActual;
      estadoBuzzer = !estadoBuzzer;
      digitalWrite(BUZZER_PIN, estadoBuzzer);
    }
  } else if (temperatura > 30.0 || humedad < 40.0) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, remotoBuzzer ? HIGH : LOW);
  }
}
