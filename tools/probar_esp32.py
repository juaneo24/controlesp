#!/usr/bin/env python3
"""
Consola para probar el ESP32 sin LabVIEW (mismo protocolo que usará LabVIEW).

  Por USB:   python probar_esp32.py --serial COM3          (Linux/Mac: /dev/ttyUSB0)
  Por WiFi:  python probar_esp32.py --ip 192.168.1.50

Escribe comandos (PING, INFO, GET, SET 0 1, STREAM 500, STREAM 0) y verás la respuesta.
Para --serial hace falta:  pip install pyserial
"""
import argparse
import socket
import sys
import threading
import time


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--serial", help="puerto serie, p. ej. COM3 o /dev/ttyUSB0")
    g.add_argument("--ip", help="IP del ESP32 en tu red")
    p.add_argument("--tcp-port", type=int, default=5000)
    p.add_argument("--baudios", type=int, default=115200)
    a = p.parse_args()

    if a.serial:
        import serial  # pip install pyserial
        con = serial.Serial(a.serial, a.baudios, timeout=0.1)
        print("Abriendo puerto... (el ESP32 puede reiniciarse, esperando 2 s)")
        time.sleep(2)
        leer = lambda: con.read(256)
        escribir = lambda b: con.write(b)
    else:
        con = socket.create_connection((a.ip, a.tcp_port), timeout=5)
        con.settimeout(0.1)

        def leer():
            try:
                datos = con.recv(256)
            except socket.timeout:
                return b""
            if not datos:
                print("\nEl ESP32 cerró la conexión.")
                sys.exit(1)
            return datos

        escribir = lambda b: con.sendall(b)

    def lector():
        while True:
            datos = leer()
            if datos:
                sys.stdout.write(datos.decode(errors="replace").replace("\r", ""))
                sys.stdout.flush()

    threading.Thread(target=lector, daemon=True).start()
    print("Conectado. Escribe comandos (Ctrl+C para salir):")
    try:
        for linea in sys.stdin:
            escribir((linea.strip() + "\n").encode())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
