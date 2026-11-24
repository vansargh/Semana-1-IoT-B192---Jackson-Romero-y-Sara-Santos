"""
ALERTAS DINAMICAS: umbral estadistico = media + K * desviacion estandar
Para cada aula:
  1. Lee de InfluxDB el historico de temperatura (ultimos DIAS_HISTORICO dias, sin contar
     los ultimos MARGEN_MINUTOS para que una anomalia reciente no altere la referencia).
  2. Calcula la media (mu) y la desviacion estandar (sigma) del historico.
  3. Umbral dinamico = mu + K * sigma   (K = 2 por defecto).
  4. Compara el valor actual (maximo de los ultimos VENTANA_ACTUAL_MIN min) con el umbral.
  5. Guarda umbral, valor actual y % del umbral en InfluxDB (alimenta el gauge de Grafana).
  6. Si actual > umbral: ALERTA -> registra en alertas.log y, si esta activo, envia correo.

Uso:
    python alerta_dinamica.py            # evalua en bucle cada INTERVALO_ALERTA_SEG
    python alerta_dinamica.py --una-vez  # evalua una sola vez
"""

import smtplib
import statistics
import sys
import time
from datetime import datetime, timedelta, timezone
from email.message import EmailMessage

from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

import config

ultima_alerta = {}  # aula -> instante del ultimo correo enviado


def calcular_umbral(valores, k):
    """Devuelve (media, desviacion, umbral). Desviacion estandar muestral."""
    media = statistics.mean(valores)
    desv = statistics.stdev(valores) if len(valores) > 1 else 0.0
    return media, desv, media + k * desv


def leer_historico(query_api, aula):
    flux = f'''
from(bucket: "{config.INFLUX_BUCKET}")
  |> range(start: -{config.DIAS_HISTORICO}d, stop: -{config.MARGEN_MINUTOS}m)
  |> filter(fn: (r) => r._measurement == "ambiente" and r._field == "temperatura" and r.aula == "{aula}")
  |> keep(columns: ["_time", "_value"])
'''
    tablas = query_api.query(flux, org=config.INFLUX_ORG)
    return [r.get_value() for t in tablas for r in t.records]


def leer_actual(query_api, aula):
    flux = f'''
from(bucket: "{config.INFLUX_BUCKET}")
  |> range(start: -{config.VENTANA_ACTUAL_MIN}m)
  |> filter(fn: (r) => r._measurement == "ambiente" and r._field == "temperatura" and r.aula == "{aula}")
  |> group()
  |> max()
'''
    tablas = query_api.query(flux, org=config.INFLUX_ORG)
    valores = [r.get_value() for t in tablas for r in t.records]
    return valores[0] if valores else None


def registrar_log(mensaje):
    linea = f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] {mensaje}"
    print(linea)
    with open(config.LOG_ALERTAS, "a", encoding="utf-8") as f:
        f.write(linea + "\n")


def enviar_correo(aula, actual, umbral, media, desv):
    if not config.EMAIL_ACTIVO:
        return
    ahora = datetime.now()
    previo = ultima_alerta.get(aula)
    if previo and ahora - previo < timedelta(minutes=config.ENFRIAMIENTO_MIN):
        return  # evita enviar un correo por cada evaluacion
    msg = EmailMessage()
    msg["Subject"] = f"ALERTA: temperatura anomala en {aula}"
    msg["From"] = config.EMAIL_REMITENTE
    msg["To"] = config.EMAIL_DESTINO
    msg.set_content(
        f"Se detecto una temperatura anomala en {aula}.\n\n"
        f"Valor actual : {actual:.1f} C\n"
        f"Umbral dinamico: {umbral:.1f} C (media {media:.1f} + {config.K_DESVIACIONES} x desviacion {desv:.2f})\n"
        f"Fecha: {ahora.strftime('%Y-%m-%d %H:%M:%S')}\n"
    )
    try:
        with smtplib.SMTP_SSL("smtp.gmail.com", 465, timeout=20) as s:
            s.login(config.EMAIL_REMITENTE, config.EMAIL_CLAVE_APP)
            s.send_message(msg)
        ultima_alerta[aula] = ahora
        registrar_log(f"Correo de alerta enviado a {config.EMAIL_DESTINO}")
    except Exception as e:
        registrar_log(f"No se pudo enviar el correo: {e}")


def evaluar(client, aula):
    query_api = client.query_api()
    write_api = client.write_api(write_options=SYNCHRONOUS)

    historico = leer_historico(query_api, aula)
    if len(historico) < 10:
        print(f"[{aula}] historico insuficiente ({len(historico)} datos). Ejecuta simular_aulas.py o la ingesta.")
        return
    actual = leer_actual(query_api, aula)
    if actual is None:
        print(f"[{aula}] sin datos en los ultimos {config.VENTANA_ACTUAL_MIN} min.")
        return

    media, desv, umbral = calcular_umbral(historico, config.K_DESVIACIONES)
    porcentaje = actual / umbral * 100
    estado = 2 if actual > umbral else (1 if porcentaje >= 90 else 0)  # 0 verde, 1 amarillo, 2 rojo

    write_api.write(
        bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG,
        record=Point("umbral_dinamico").tag("aula", aula)
        .field("umbral", float(umbral)).field("actual", float(actual))
        .field("porcentaje_umbral", float(porcentaje)).field("estado", int(estado)),
    )

    print(f"[{aula}] n={len(historico)} media={media:.2f} desv={desv:.2f} "
          f"umbral={umbral:.2f} actual={actual:.2f} ({porcentaje:.0f} %)")

    if actual > umbral:
        registrar_log(
            f"ALERTA aula={aula} actual={actual:.1f} C > umbral={umbral:.1f} C "
            f"(media={media:.1f}, desv={desv:.2f}, k={config.K_DESVIACIONES})"
        )
        enviar_correo(aula, actual, umbral, media, desv)


def main():
    client = InfluxDBClient(url=config.INFLUX_URL, token=config.INFLUX_TOKEN, org=config.INFLUX_ORG)
    if "--una-vez" in sys.argv:
        for aula in config.AULAS_ALERTA:
            evaluar(client, aula)
        client.close()
        return

    print(f"Alertas dinamicas activas. Evaluacion cada {config.INTERVALO_ALERTA_SEG} s. Ctrl+C para detener.")
    try:
        while True:
            for aula in config.AULAS_ALERTA:
                evaluar(client, aula)
            time.sleep(config.INTERVALO_ALERTA_SEG)
    except KeyboardInterrupt:
        print("\nAlertas detenidas.")
    finally:
        client.close()


if __name__ == "__main__":
    main()
