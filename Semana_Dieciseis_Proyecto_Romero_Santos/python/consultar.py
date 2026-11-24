"""
CONSULTA DE LO ALMACENADO EN INFLUXDB
Muestra cuantos puntos hay por aula, origen (real, simulado, anomalia) y variable.
Sirve para llenar los datos del informe (cantidad de puntos reales y simulados).

Uso:
    python consultar.py
"""

from influxdb_client import InfluxDBClient

import config

client = InfluxDBClient(url=config.INFLUX_URL, token=config.INFLUX_TOKEN, org=config.INFLUX_ORG)
flux = f'''
from(bucket: "{config.INFLUX_BUCKET}")
  |> range(start: 0)
  |> filter(fn: (r) => r._measurement == "ambiente")
  |> group(columns: ["aula", "origen", "_field"])
  |> count()
'''
tablas = client.query_api().query(flux, org=config.INFLUX_ORG)

filas = []
for t in tablas:
    for r in t.records:
        filas.append((r.values.get("aula"), r.values.get("origen"), r.get_field(), r.get_value()))

print(f"Bucket: {config.INFLUX_BUCKET}\n")
print(f"{'aula':<8}{'origen':<12}{'variable':<14}{'puntos':>8}")
for aula, origen, campo, n in sorted(filas):
    print(f"{aula:<8}{origen:<12}{campo:<14}{n:>8}")

totales = {}
for aula, origen, campo, n in filas:
    totales[origen] = totales.get(origen, 0) + n
print("\nTotal de puntos por origen (suma de todas las variables):")
for origen, n in sorted(totales.items()):
    print(f"  {origen:<12}{n:>8}")
client.close()
