"""
INTEGRACION DE DATOS: Adafruit IO -> InfluxDB
Lee los feeds configurados (temperatura, humedad, luminosidad) por la API REST de
Adafruit IO y los escribe en InfluxDB como la medicion "ambiente".

Los puntos de InfluxDB se identifican por medicion + etiquetas + marca de tiempo,
asi que volver a escribir el mismo dato NO crea duplicados.

Uso:
    python ingesta_influx.py            # bucle continuo (Ctrl+C para detener)
    python ingesta_influx.py --una-vez  # una sola consulta
"""

import json
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

import config


def pedir_datos(feed):
    """Ultimos datos de un feed de Adafruit IO (lista de dicts)."""
    url = (
        f"https://io.adafruit.com/api/v2/{config.AIO_USERNAME}"
        f"/feeds/{feed}/data?limit={config.LIMITE}"
    )
    req = urllib.request.Request(url, headers={"X-AIO-Key": config.AIO_KEY})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read().decode("utf-8"))


def a_fecha(texto):
    """'2026-11-19T15:00:03Z' -> datetime con zona horaria UTC."""
    return datetime.fromisoformat(texto.replace("Z", "+00:00"))


def construir_puntos(campo, datos):
    """Convierte los datos de Adafruit IO en puntos de InfluxDB."""
    puntos = []
    for d in datos:
        try:
            valor = float(d["value"])
        except (ValueError, TypeError, KeyError):
            continue  # se ignoran datos no numericos
        puntos.append(
            Point("ambiente")
            .tag("aula", config.AULA_REAL)
            .tag("origen", "real")
            .field(campo, valor)
            .time(a_fecha(d["created_at"]), WritePrecision.S)
        )
    return puntos


def ciclo(write_api):
    for feed, campo in config.FEEDS.items():
        try:
            datos = pedir_datos(feed)
            puntos = construir_puntos(campo, datos)
            write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=puntos)
            print(f"[OK] {feed}: {len(datos)} datos recibidos, {len(puntos)} escritos en InfluxDB")
        except urllib.error.HTTPError as e:
            print(f"[ERROR] {feed}: Adafruit IO respondio {e.code} (usuario, AIO Key o el feed no existe)")
        except Exception as e:
            print(f"[ERROR] {feed}: {e}")


def main():
    client = InfluxDBClient(url=config.INFLUX_URL, token=config.INFLUX_TOKEN, org=config.INFLUX_ORG)
    write_api = client.write_api(write_options=SYNCHRONOUS)

    if "--una-vez" in sys.argv:
        ciclo(write_api)
        client.close()
        return

    print(f"Ingesta Adafruit IO -> InfluxDB. Cada {config.INTERVALO_SEGUNDOS} s. Ctrl+C para detener.")
    try:
        while True:
            ciclo(write_api)
            time.sleep(config.INTERVALO_SEGUNDOS)
    except KeyboardInterrupt:
        print("\nIngesta detenida.")
    finally:
        client.close()


if __name__ == "__main__":
    main()
