"""
DATOS SIMULADOS para completar el historico y la segunda aula
El proyecto solo tiene un ESP32 real (aula1). Este script genera datos sinteticos
con un patron diario realista (temperatura que sube por la tarde, luz de dia) y los
marca con la etiqueta origen="simulado" para distinguirlos siempre de los reales.

Uso:
    # 1) Historico de 7 dias para las dos aulas (una sola vez)
    python simular_aulas.py --dias 7 --aulas aula1 aula2

    # 2) Aula 2 "en vivo": escribe un dato nuevo cada 15 s (Ctrl+C para detener)
    python simular_aulas.py --en-vivo --aulas aula2
"""

import argparse
import math
import random
import time
from datetime import datetime, timedelta, timezone

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

import config

# Parametros de cada aula: temperatura base, amplitud diaria y luz maxima
PERFIL = {
    "aula1": {"temp": 27.5, "amp": 2.0, "luz": 3500},
    "aula2": {"temp": 24.5, "amp": 1.5, "luz": 2800},
}


def valores(aula, ts):
    """Devuelve (temperatura, humedad, luminosidad, calidad_aire) simuladas para un instante."""
    p = PERFIL.get(aula, PERFIL["aula2"])
    hora = ts.hour + ts.minute / 60.0                       # hora UTC
    hora_local = (hora - 5) % 24                            # Colombia: UTC-5
    temp = p["temp"] + p["amp"] * math.sin((hora_local - 9) / 24 * 2 * math.pi) + random.gauss(0, 0.3)
    hum = min(90, max(30, 70 - 3 * (temp - p["temp"]) + random.gauss(0, 1.5)))
    sol = max(0.0, math.sin((hora_local - 6) / 12 * math.pi))   # luz solo entre 6 y 18 h
    luz = max(0, p["luz"] * sol + random.gauss(0, 60)) + 80
    # calidad de aire (indice MQ-2): casi siempre bajo, con mas ocupacion en horas de clase
    ocupacion = 1.0 if 7 <= hora_local <= 17 else 0.3
    aire = max(0.0, random.gauss(60 * ocupacion, 25))
    if random.random() < 0.01:
        aire += random.uniform(150, 350)                    # picos ocasionales
    return round(temp, 2), round(hum, 1), float(int(luz)), round(aire, 1)


def punto(aula, ts):
    t, h, l, a = valores(aula, ts)
    return (
        Point("ambiente")
        .tag("aula", aula)
        .tag("origen", "simulado")
        .field("temperatura", t)
        .field("humedad", h)
        .field("luminosidad", l)
        .field("calidad_aire", a)
        .time(ts, WritePrecision.S)
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dias", type=int, default=7)
    ap.add_argument("--paso-min", type=int, default=10)
    ap.add_argument("--hasta-horas", type=float, default=0, help="termina N horas antes de ahora")
    ap.add_argument("--aulas", nargs="+", default=["aula1", "aula2"])
    ap.add_argument("--en-vivo", action="store_true")
    ap.add_argument("--intervalo", type=int, default=15)
    args = ap.parse_args()

    random.seed(42)
    client = InfluxDBClient(url=config.INFLUX_URL, token=config.INFLUX_TOKEN, org=config.INFLUX_ORG)
    write_api = client.write_api(write_options=SYNCHRONOUS)

    if args.en_vivo:
        print(f"Simulacion en vivo de {args.aulas}. Ctrl+C para detener.")
        try:
            while True:
                ahora = datetime.now(timezone.utc).replace(microsecond=0)
                for aula in args.aulas:
                    write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=punto(aula, ahora))
                print(f"[OK] {ahora.isoformat()} datos simulados escritos")
                time.sleep(args.intervalo)
        except KeyboardInterrupt:
            print("\nSimulacion detenida.")
    else:
        fin = datetime.now(timezone.utc).replace(microsecond=0) - timedelta(hours=args.hasta_horas)
        inicio = fin - timedelta(days=args.dias)
        for aula in args.aulas:
            lote, ts, total = [], inicio, 0
            while ts <= fin:
                lote.append(punto(aula, ts))
                ts += timedelta(minutes=args.paso_min)
                if len(lote) == 500:
                    write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=lote)
                    total += len(lote)
                    lote = []
            if lote:
                write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=lote)
                total += len(lote)
            print(f"[OK] {aula}: {total} instantes simulados ({args.dias} dias, cada {args.paso_min} min)")

    client.close()


if __name__ == "__main__":
    main()
