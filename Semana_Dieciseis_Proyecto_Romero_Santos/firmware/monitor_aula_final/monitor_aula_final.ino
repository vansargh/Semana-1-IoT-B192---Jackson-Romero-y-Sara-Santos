/*
 * PROYECTO FINAL - Monitoreo Inteligente de Aula (ESP32)
 * Sensores: DHT11 (temperatura y humedad), LDR (luminosidad), MQ-2 (calidad de aire)
 * Actuadores: 4 LEDs + buzzer
 *
 * Flujo:
 *   Sensores -> ESP32 (control local) -> Blynk (dashboard + alarma remota, TLS 8883, cada 30 s)
 *                                     -> Adafruit IO (4 feeds, 1883, cada 15 s)
 *   Adafruit IO -> IFTTT (alertas push) y -> script Python -> InfluxDB -> Grafana
 *
 * Librerias: PubSubClient (Nick O'Leary), DHT sensor library + Adafruit Unified Sensor.
 * Credenciales: se leen de config.h (NO se sube a GitHub).
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "DHT.h"
#include "config.h"

// ==========================================
// 1. PINES
// ==========================================
#define DHTPIN 4
#define DHTTYPE DHT11
#define LDR_PIN 34        // ADC1 (funciona con WiFi activo)
#define MQ2_PIN 35        // ADC1, a traves del divisor de tension 10k/20k
#define LED_AMARILLO 5
#define LED_ROJO 18
#define LED_VERDE 19
#define LED_BLANCO 22
#define BUZZER_PIN 23

// ==========================================
// 2. UMBRALES DE LA LOGICA LOCAL
// ==========================================
const float TEMP_ALTA    = 30.0;   // ventilacion ON por encima de este valor
const float TEMP_BAJA    = 26.0;   // ventilacion OFF al bajar a este valor (histeresis)
const float TEMP_CRITICA = 32.0;   // buzzer intermitente rapido
const float HUM_MIN      = 40.0;
const float HUM_MAX      = 75.0;
const int   LUZ_MINIMA   = 1000;   // por debajo se enciende el LED blanco

// MQ-2: estados segun el incremento sobre la linea base
const int MQ2_REGULAR = 300;
const int MQ2_MALA    = 700;
const unsigned long MQ2_CALENTAMIENTO_MS = 120000;  // 2 minutos antes de tomar la linea base

// ==========================================
// 3. CONEXIONES
// ==========================================
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Blynk (MQTT con TLS). Si no conecta, prueba con "ny3.blynk.cloud".
const char* BLYNK_SERVER = "blynk.cloud";
const int   BLYNK_PORT   = 8883;
const char* TOPIC_B_TEMP   = "ds/temperatura";
const char* TOPIC_B_HUM    = "ds/humedad";
const char* TOPIC_B_LUZ    = "ds/luminosidad";
const char* TOPIC_B_AIRE   = "ds/calidad_aire";
const char* TOPIC_B_ALARMA = "downlink/ds/alarma_remota";   // comando desde el dashboard
const char* TOPIC_B_GET    = "get/ds";                       // pedir el ultimo valor guardado

// Adafruit IO (MQTT sin cifrar). Topico de un feed: usuario/feeds/nombre
const char* AIO_SERVER = "io.adafruit.com";
const int   AIO_PORT   = 1883;
String topicATemp = String(AIO_USERNAME) + "/feeds/temperatura";
String topicAHum  = String(AIO_USERNAME) + "/feeds/humedad";
String topicALuz  = String(AIO_USERNAME) + "/feeds/luminosidad";
String topicAAire = String(AIO_USERNAME) + "/feeds/calidad-aire";

WiFiClientSecure secureClient;
WiFiClient plainClient;
PubSubClient mqttBlynk(secureClient);
PubSubClient mqttAio(plainClient);
DHT dht(DHTPIN, DHTTYPE);

String clientIdBase;

// ==========================================
// 4. ESTADO
// ==========================================
enum EstadoAire { CALENTANDO, BUENA, REGULAR, MALA };
EstadoAire estadoAire = CALENTANDO;
EstadoAire estadoAireAnterior = CALENTANDO;

float temperatura = 0.0;
float humedad = 0.0;
int valorLuz = 0;
int indiceAire = 0;          // incremento del MQ-2 sobre la linea base
int lineaBaseMQ2 = 0;
bool baseLista = false;
bool datosValidos = false;

bool ventilacionActiva = false;
bool remotoAlarma = false;   // interruptor alarma_remota de Blynk

bool estadoAmarillo = LOW;
bool estadoBuzzer = LOW;

unsigned long tWiFi = 0, tMqttBlynk = 0, tMqttAio = 0, tLectura = 0;
unsigned long tAmarillo = 0, tBuzzer = 0;
unsigned long ultimoEnvioBlynk = 0, ultimoEnvioAio = 0;

// ==========================================
// 5. SENSORES
// ==========================================
int leerPromedio(int pin, int muestras) {
  long suma = 0;
  for (int i = 0; i < muestras; i++) {
    suma += analogRead(pin);
    delay(2);
  }
  return (int)(suma / muestras);
}

void actualizarAire(unsigned long ahora) {
  int lectura = leerPromedio(MQ2_PIN, 10);

  // Tras el calentamiento se toma la linea base (aire limpio)
  if (!baseLista && ahora >= MQ2_CALENTAMIENTO_MS) {
    lineaBaseMQ2 = leerPromedio(MQ2_PIN, 20);
    baseLista = true;
    Serial.print("[MQ-2] Linea base fijada: ");
    Serial.println(lineaBaseMQ2);
  }

  if (!baseLista) {
    estadoAire = CALENTANDO;
    indiceAire = 0;
    return;
  }

  indiceAire = lectura - lineaBaseMQ2;
  if (indiceAire < 0) indiceAire = 0;

  if (indiceAire >= MQ2_MALA)         estadoAire = MALA;
  else if (indiceAire >= MQ2_REGULAR) estadoAire = REGULAR;
  else                                estadoAire = BUENA;

  if (estadoAire == MALA && estadoAireAnterior != MALA) {
    Serial.print("[AIRE] Aire malo: ventilar (indice ");
    Serial.print(indiceAire);
    Serial.println(")");
  }
  estadoAireAnterior = estadoAire;
}

const char* nombreAire() {
  switch (estadoAire) {
    case CALENTANDO: return "CALENTANDO";
    case BUENA:      return "BUENA";
    case REGULAR:    return "REGULAR";
    default:         return "MALA";
  }
}

// ==========================================
// 6. RED
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
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n[ERROR] No se pudo conectar al WiFi");
  }
}

// Comando recibido desde Blynk (interruptor alarma_remota)
void callbackBlynk(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  if (strcmp(topic, TOPIC_B_ALARMA) == 0) {
    remotoAlarma = (msg.toFloat() > 0);
    Serial.print("[CMD] Alarma remota desde Blynk -> ");
    Serial.println(remotoAlarma ? "ON" : "OFF");
  }
}

// Un solo intento (no bloqueante en el bucle): se llama cada 5 s
void reconectarBlynk() {
  Serial.print("[Blynk] Conectando... ");
  // Blynk: usuario fijo "device" y contrasena = Auth Token
  if (mqttBlynk.connect((clientIdBase + "_B").c_str(), "device", BLYNK_AUTH_TOKEN)) {
    Serial.println("OK");
    mqttBlynk.subscribe(TOPIC_B_ALARMA);
    mqttBlynk.publish(TOPIC_B_GET, "alarma_remota");   // recuperar el ultimo estado del interruptor
  } else {
    Serial.print("Fallo, rc=");
    Serial.println(mqttBlynk.state());
  }
}

void reconectarAio() {
  Serial.print("[Adafruit IO] Conectando... ");
  // Adafruit IO: usuario = nombre de usuario, contrasena = AIO Key
  if (mqttAio.connect((clientIdBase + "_A").c_str(), AIO_USERNAME, AIO_KEY)) {
    Serial.println("OK");
  } else {
    Serial.print("Fallo, rc=");
    Serial.println(mqttAio.state());
  }
}

void publicarBlynk() {
  char buf[16];
  dtostrf(temperatura, 1, 1, buf); mqttBlynk.publish(TOPIC_B_TEMP, buf);
  dtostrf(humedad, 1, 1, buf);     mqttBlynk.publish(TOPIC_B_HUM, buf);
  snprintf(buf, sizeof(buf), "%d", valorLuz);    mqttBlynk.publish(TOPIC_B_LUZ, buf);
  if (baseLista) {
    snprintf(buf, sizeof(buf), "%d", indiceAire); mqttBlynk.publish(TOPIC_B_AIRE, buf);
  }
  Serial.println("[Blynk] Datos publicados");
}

void publicarAio() {
  char buf[16];
  dtostrf(temperatura, 1, 1, buf); mqttAio.publish(topicATemp.c_str(), buf);
  dtostrf(humedad, 1, 1, buf);     mqttAio.publish(topicAHum.c_str(), buf);
  snprintf(buf, sizeof(buf), "%d", valorLuz);    mqttAio.publish(topicALuz.c_str(), buf);
  if (baseLista) {
    snprintf(buf, sizeof(buf), "%d", indiceAire); mqttAio.publish(topicAAire.c_str(), buf);
  }
  Serial.println("[Adafruit IO] Datos publicados");
}

// ==========================================
// 7. CONTROL LOCAL
// ==========================================
void controlarBuzzer(unsigned long ahora) {
  // Prioridad: alarma remota > aire malo > temperatura critica > aire regular > temp/humedad
  if (remotoAlarma) {
    digitalWrite(BUZZER_PIN, HIGH);
    return;
  }
  unsigned long periodo = 0;   // 0 = sin parpadeo
  bool continuo = false;
  if (estadoAire == MALA)                   periodo = 1000;   // buzzer lento
  else if (temperatura > TEMP_CRITICA)      periodo = 200;    // intermitente rapido
  else if (estadoAire == REGULAR)           periodo = 400;    // intermitente
  else if (temperatura > TEMP_ALTA || humedad < HUM_MIN) continuo = true;

  if (periodo > 0) {
    if (ahora - tBuzzer >= periodo) {
      tBuzzer = ahora;
      estadoBuzzer = !estadoBuzzer;
      digitalWrite(BUZZER_PIN, estadoBuzzer);
    }
  } else {
    estadoBuzzer = LOW;
    digitalWrite(BUZZER_PIN, continuo ? HIGH : LOW);
  }
}

void controlLocal(unsigned long ahora) {
  if (!datosValidos) return;   // evita alarmas falsas antes de la primera lectura

  // Histeresis de la ventilacion
  if (temperatura > TEMP_ALTA) ventilacionActiva = true;
  else if (temperatura <= TEMP_BAJA) ventilacionActiva = false;

  // LED rojo: ventilacion, aire malo o alarma remota
  digitalWrite(LED_ROJO, (ventilacionActiva || estadoAire == MALA || remotoAlarma) ? HIGH : LOW);

  // LED verde: confort (sin ventilacion, humedad normal y aire bueno)
  bool confort = !ventilacionActiva && humedad >= HUM_MIN && humedad <= HUM_MAX &&
                 (estadoAire == BUENA || estadoAire == CALENTANDO);
  digitalWrite(LED_VERDE, confort ? HIGH : LOW);

  // LED blanco: poca luz
  digitalWrite(LED_BLANCO, valorLuz < LUZ_MINIMA ? HIGH : LOW);

  // LED amarillo: humedad alta (parpadeo) o baja (fijo)
  if (humedad > HUM_MAX) {
    if (ahora - tAmarillo >= 300) {
      tAmarillo = ahora;
      estadoAmarillo = !estadoAmarillo;
      digitalWrite(LED_AMARILLO, estadoAmarillo);
    }
  } else if (humedad < HUM_MIN) {
    digitalWrite(LED_AMARILLO, HIGH);
  } else {
    digitalWrite(LED_AMARILLO, LOW);
  }

  controlarBuzzer(ahora);
}

// ==========================================
// 8. SETUP Y LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_BLANCO, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_BLANCO, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  clientIdBase = "ESP32_Aula_" + String((uint32_t)ESP.getEfuseMac(), HEX);

  dht.begin();
  conectarWiFi();

  secureClient.setInsecure();   // TLS sin validar certificado (suficiente para el proyecto)
  mqttBlynk.setServer(BLYNK_SERVER, BLYNK_PORT);
  mqttBlynk.setCallback(callbackBlynk);
  mqttBlynk.setKeepAlive(30);
  mqttBlynk.setSocketTimeout(5);
  mqttAio.setServer(AIO_SERVER, AIO_PORT);
  mqttAio.setKeepAlive(30);
  mqttAio.setSocketTimeout(5);

  Serial.println("Sistema iniciado. El MQ-2 se calienta 2 minutos: manten el aire limpio.");
}

void loop() {
  unsigned long ahora = millis();

  // --- Reconexion WiFi cada 10 s ---
  if (WiFi.status() != WL_CONNECTED && ahora - tWiFi >= 10000) {
    Serial.println("\n[ALERTA] WiFi perdido. Reconectando...");
    WiFi.disconnect();
    WiFi.reconnect();
    tWiFi = ahora;
  }

  // --- Sesiones MQTT (cada una reintenta cada 5 s) ---
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttBlynk.connected()) {
      if (ahora - tMqttBlynk >= 5000) { tMqttBlynk = ahora; reconectarBlynk(); }
    } else {
      mqttBlynk.loop();
    }
    if (!mqttAio.connected()) {
      if (ahora - tMqttAio >= 5000) { tMqttAio = ahora; reconectarAio(); }
    } else {
      mqttAio.loop();
    }
  }

  // --- Lectura de sensores cada 2 s ---
  if (ahora - tLectura >= 2000) {
    tLectura = ahora;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      humedad = h;
      temperatura = t;
      datosValidos = true;
    } else {
      Serial.println("Error: fallo al leer el DHT11");
    }
    valorLuz = leerPromedio(LDR_PIN, 10);
    actualizarAire(ahora);

    Serial.print("T: "); Serial.print(temperatura, 1);
    Serial.print(" C | H: "); Serial.print(humedad, 1);
    Serial.print(" % | Luz: "); Serial.print(valorLuz);
    Serial.print(" | Aire: indice "); Serial.print(indiceAire);
    Serial.print(" ("); Serial.print(nombreAire()); Serial.println(")");
  }

  // --- Publicaciones periodicas ---
  if (datosValidos && mqttAio.connected() && ahora - ultimoEnvioAio >= AIO_INTERVALO_MS) {
    ultimoEnvioAio = ahora;
    publicarAio();
  }
  if (datosValidos && mqttBlynk.connected() && ahora - ultimoEnvioBlynk >= BLYNK_INTERVALO_MS) {
    ultimoEnvioBlynk = ahora;
    publicarBlynk();
  }

  // --- Logica local (no depende de la nube) ---
  controlLocal(ahora);
}
