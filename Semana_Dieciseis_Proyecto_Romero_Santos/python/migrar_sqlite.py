"""
MIGRACION: SQLite (actividad anterior) -> InfluxDB
Copia las lecturas guardadas en datos_aula.db a InfluxDB, para no perder el
historico real que ya se habia recolectado.

Uso:
    python migrar_sqlite.py
"""

import sqlite3
from datetime import datetime

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

import config


def main():
    con = sqlite3.connect(config.SQLITE_PATH)
    filas = con.execute("SELECT feed, valor, creado_utc FROM lecturas").fetchall()
    con.close()
    print(f"Lecturas encontradas en SQLite: {len(filas)}")

    client = InfluxDBClient(url=config.INFLUX_URL, token=config.INFLUX_TOKEN, org=config.INFLUX_ORG)
    write_api = client.write_api(write_options=SYNCHRONOUS)

    lote = []
    for feed, valor, creado in filas:
        lote.append(
            Point("ambiente")
            .tag("aula", config.AULA_REAL)
            .tag("origen", "real")
            .field(feed, float(valor))
            .time(datetime.fromisoformat(creado.replace("Z", "+00:00")), WritePrecision.S)
        )
        if len(lote) == 500:
            write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=lote)
            lote = []
    if lote:
        write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=lote)

    client.close()
    print("Migracion terminada.")


if __name__ == "__main__":
    main()
