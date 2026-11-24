"""
SIMULACION DE ANOMALIA
Inyecta manualmente en InfluxDB varios datos de temperatura muy altos (con la etiqueta
origen="anomalia") para comprobar que el script de alertas dinamicas dispara la alerta.

Uso:
    python inyectar_anomalia.py                         # aula1, 38.5 C, 5 datos
    python inyectar_anomalia.py --aula aula2 --valor 36 --cantidad 8
Despues ejecuta:   python alerta_dinamica.py --una-vez
"""

import argparse
from datetime import datetime, timedelta, timezone

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

import config


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--aula", default="aula1")
    ap.add_argument("--valor", type=float, default=38.5)
    ap.add_argument("--cantidad", type=int, default=5)
    ap.add_argument("--separacion", type=int, default=20, help="segundos entre datos")
    args = ap.parse_args()

    client = InfluxDBClient(url=config.INFLUX_URL, token=config.INFLUX_TOKEN, org=config.INFLUX_ORG)
    write_api = client.write_api(write_options=SYNCHRONOUS)

    ahora = datetime.now(timezone.utc).replace(microsecond=0)
    puntos = []
    for i in range(args.cantidad):
        ts = ahora - timedelta(seconds=i * args.separacion)
        puntos.append(
            Point("ambiente").tag("aula", args.aula).tag("origen", "anomalia")
            .field("temperatura", args.valor + (i % 2) * 0.4)
            .time(ts, WritePrecision.S)
        )
    write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=puntos)
    client.close()
    print(f"[OK] {args.cantidad} datos de {args.valor} C inyectados en {args.aula}.")
    print("Ahora ejecuta: python alerta_dinamica.py --una-vez")


if __name__ == "__main__":
    main()
