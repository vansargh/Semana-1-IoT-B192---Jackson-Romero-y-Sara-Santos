// ==========================================================
// config.example.h  ->  COPIA este archivo como "config.h"
// y rellena tus datos reales. "config.h" NO se sube a GitHub.
// ==========================================================
#pragma once

#define WIFI_SSID        "TU_WIFI"
#define WIFI_PASSWORD    "TU_CLAVE_WIFI"

// Blynk: Auth Token del dispositivo (Blynk.Console > Dispositivos)
#define BLYNK_AUTH_TOKEN "TU_AUTH_TOKEN"

// Adafruit IO: usuario y AIO Key (llave amarilla en io.adafruit.com)
#define AIO_USERNAME     "TU_USUARIO_ADAFRUIT"
#define AIO_KEY          "TU_AIO_KEY"

// Cada cuantos milisegundos se publica en cada plataforma
#define AIO_INTERVALO_MS   15000    // 4 feeds cada 15 s = 16 datos/min (limite gratuito: 30/min)
#define BLYNK_INTERVALO_MS 30000    // 4 datastreams cada 30 s ~ 11.500 mensajes al dia
