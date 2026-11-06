/*
 * SEMANA 13 - Alerta de luminosidad con IFTTT
 * Flujo: LDR -> ESP32 -> Adafruit IO -> IFTTT -> notificacion push
 *
 * Base: codigo MQTT de las semanas 10 y 12 (WiFi + millis + reconexion).
 * Cambios: se lee solo el LDR (ya no el DHT11) y se publica en el feed
 *          "luminosidad" de Adafruit IO cada 15 segundos.
 *
 * Libreria requerida: PubSubClient (Nick O'Leary)
 * Credenciales: se leen de config.h (NO se sube a GitHub).
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

// ==========================================
// 1. CONFIGURACION
// ==========================================
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* mqtt_server = "io.adafruit.com";
const int   mqtt_port   = 1883;

// En Adafruit IO el topico de un feed es: usuario/feeds/nombre_feed
String topicLuz = String(AIO_USERNAME) + "/feeds/luminosidad";

// ==========================================
// 2. PINES Y OBJETOS
// ==========================================
#define LDR_PIN 34   // entrada analogica (ADC) del divisor de voltaje del LDR

WiFiClient espClient;
PubSubClient client(espClient);
String clientId;

// ==========================================
// 3. VARIABLES DE TIEMPO (millis, sin delay largos)
// ==========================================
unsigned long tiempoAnteriorWiFi = 0;
unsigned long tiempoAnteriorMQTT = 0;
unsigned long tiempoAnteriorLectura = 0;
unsigned long ultimoEnvio = 0;
const unsigned long INTERVALO_ENVIO = INTERVALO_ENVIO_MS;  // 10-15 s recomendado para IFTTT

int valorLuminosidad = 0;
bool hayLectura = false;

// ==========================================
// 4. FUNCIONES
// ==========================================

// Promedio de 10 muestras para suavizar el ruido del ADC
int leerLuminosidad() {
  long suma = 0;
  for (int i = 0; i < 10; i++) {
    suma += analogRead(LDR_PIN);
    delay(2);
  }
  return (int)(suma / 10);
}

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
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n[ERROR] No se pudo conectar al WiFi");
  }
}

// Intento unico y no bloqueante: se llama cada 5 s desde loop()
void reconnect() {
  Serial.print("[MQTT] Conectando a ");
  Serial.print(mqtt_server);
  Serial.print("... ");

  // Adafruit IO: usuario = nombre de usuario, contrasena = AIO Key
  if (client.connect(clientId.c_str(), AIO_USERNAME, AIO_KEY)) {
    Serial.println("OK");
  } else {
    Serial.print("Fallo, rc=");
    Serial.println(client.state());
  }
}

void publicarLuminosidad() {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", valorLuminosidad);

  if (client.publish(topicLuz.c_str(), buf)) {
    Serial.print("[MQTT] Publicado en ");
    Serial.print(topicLuz);
    Serial.print(" -> ");
    Serial.println(valorLuminosidad);
  } else {
    Serial.println("[MQTT] Error al publicar");
  }
}

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  clientId = "ESP32_Luz_" + String((uint32_t)ESP.getEfuseMac(), HEX);

  conectarWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(30);

  Serial.println("Sistema iniciado: LDR -> Adafruit IO (feed luminosidad)");
}

// ==========================================
// 6. LOOP
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

  // --- Mantener la sesion MQTT ---
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      if (tiempoActual - tiempoAnteriorMQTT >= 5000) {
        tiempoAnteriorMQTT = tiempoActual;
        reconnect();
      }
    } else {
      client.loop();
    }
  }

  // --- Lectura del LDR cada 2 s ---
  if (tiempoActual - tiempoAnteriorLectura >= 2000) {
    tiempoAnteriorLectura = tiempoActual;
    valorLuminosidad = leerLuminosidad();
    hayLectura = true;

    Serial.print("Luminosidad (ADC 0-4095): ");
    Serial.println(valorLuminosidad);
  }

  // --- Publicacion periodica ---
  if (client.connected() && hayLectura && (tiempoActual - ultimoEnvio >= INTERVALO_ENVIO)) {
    ultimoEnvio = tiempoActual;
    publicarLuminosidad();
  }
}
