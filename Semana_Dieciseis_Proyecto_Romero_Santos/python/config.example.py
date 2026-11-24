# ==========================================================
# config.example.py  ->  COPIA este archivo como "config.py"
# y rellena tus datos. "config.py" NO se sube a GitHub.
# ==========================================================

# ---- Adafruit IO ----
AIO_USERNAME = "TU_USUARIO_ADAFRUIT"
AIO_KEY = "TU_AIO_KEY"

# feed de Adafruit IO  ->  campo que se guarda en InfluxDB
FEEDS = {
    "temperatura": "temperatura",
    "humedad": "humedad",
    "luminosidad": "luminosidad",
    "calidad-aire": "calidad_aire",     # MQ-2: incremento sobre la linea base
}
AULA_REAL = "aula1"          # el ESP32 real representa a esta aula
LIMITE = 100                 # datos recientes que se piden por consulta
INTERVALO_SEGUNDOS = 30      # cada cuanto consulta Adafruit IO

# ---- InfluxDB (iguales a los del archivo .env de docker-compose) ----
INFLUX_URL = "http://localhost:8086"
INFLUX_TOKEN = "token-uts-aula-2026"
INFLUX_ORG = "uts"
INFLUX_BUCKET = "aula"

# ---- Base SQLite de la actividad anterior (para migrar el historico) ----
SQLITE_PATH = "../../almacenamiento_avanzado/datos_aula.db"

# ---- Alertas dinamicas ----
AULAS_ALERTA = ["aula1", "aula2"]
K_DESVIACIONES = 2           # umbral = media + K * desviacion estandar
DIAS_HISTORICO = 7           # dias de historia que se usan para calcular media y desviacion
MARGEN_MINUTOS = 30          # el historico termina 30 min antes de ahora (para no contaminarlo con la anomalia)
VENTANA_ACTUAL_MIN = 10      # el "valor actual" es el maximo de los ultimos 10 min
INTERVALO_ALERTA_SEG = 60    # cada cuanto se evalua en modo continuo
LOG_ALERTAS = "alertas.log"

# ---- Correo (opcional). Si EMAIL_ACTIVO = False solo se escribe el log ----
EMAIL_ACTIVO = False
EMAIL_REMITENTE = "tucorreo@gmail.com"
EMAIL_CLAVE_APP = ""         # "contrasena de aplicacion" de Google (no tu clave normal)
EMAIL_DESTINO = "coordinador@correo.com"
ENFRIAMIENTO_MIN = 10        # minimos minutos entre dos correos de la misma aula
