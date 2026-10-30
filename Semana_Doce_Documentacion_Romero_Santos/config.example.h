// ==========================================================
// config.example.h  ->  COPIA este archivo como "config.h"
// y rellena tus datos reales. "config.h" esta en .gitignore
// y NUNCA debe subirse a GitHub.
// ==========================================================
#pragma once

#define WIFI_SSID        "TU_WIFI"
#define WIFI_PASSWORD    "TU_CLAVE_WIFI"

// Auth Token del dispositivo (Blynk.Console > Devices > tu dispositivo)
#define BLYNK_AUTH_TOKEN "TU_AUTH_TOKEN"

// 1 = MQTT con TLS (puerto 8883, recomendado)
// 0 = MQTT sin cifrado (puerto 1883, solo para pruebas iniciales)
#define BLYNK_USAR_TLS   1

// Cada cuantos milisegundos se publican temperatura, humedad y luminosidad.
// El plan gratuito de Blynk limita los mensajes por mes: no lo bajes de 10000.
#define INTERVALO_ENVIO_MS 15000
