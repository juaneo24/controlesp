# controlesp

Control de luces y lectura de sensores con un **ESP32** desde **LabVIEW**,
por **USB** o por **WiFi**.

```
              USB (VISA, 115200)  ─┐
 [LabVIEW]                          ├──►  [ESP32]  ──►  relés / LEDs (salidas)
              WiFi (TCP, puerto 5000) ┘      │
     ▲                                       │
     └──────── DATA,1,0,0,128,2210,1 ◄───────┘◄──  sensores (entradas)
```

LabVIEW envía comandos de texto sencillos (`GET`, `SET 0 1`…) y el ESP32
responde con una línea separada por comas, fácil de convertir en un arreglo
numérico en LabVIEW.

👉 **Guía de LabVIEW paso a paso: [docs/LABVIEW.md](docs/LABVIEW.md)**

Como extra, el ESP32 también sirve una página web de control en
`http://<ip-del-esp32>/` y una API JSON.

## 1. Material

- ESP32 (DevKit / "ESP32 Dev Module")
- Módulo de relés (para luces de 110/220 V) o LEDs con resistencia (para probar)
- Sensores opcionales: LDR, botón, sensor de puerta, potenciómetro…
- Cable USB **de datos** (no solo de carga)
- LabVIEW con **NI-VISA** instalado (para la conexión USB)

> ⚠️ Si vas a controlar luces de la red eléctrica, hazlo siempre a través de un
> **módulo de relé** y con la instalación desconectada mientras cableas.

## 2. Conexiones por defecto

| id | Función            | GPIO | Notas                                              |
|----|--------------------|------|----------------------------------------------------|
| S0 | Luz sala (relé)    | 23   | Relé activo en LOW (`invertida = true`)            |
| S1 | Luz cocina (relé)  | 22   | Relé activo en LOW                                 |
| S2 | LED de la placa    | 2    | Sirve para probar sin cablear nada                 |
| S3 | Tira LED (PWM)     | 21   | Intensidad 0-255, vía MOSFET si es tira de 12 V    |
| E0 | Sensor de luz LDR  | 34   | Entrada analógica (0-4095). Divisor con 10 kΩ      |
| E1 | Puerta / botón     | 4    | `INPUT_PULLUP`: conecta el contacto entre GPIO4 y GND |

Relé: `VCC → 5V (VIN)`, `GND → GND`, `IN1 → GPIO23`, `IN2 → GPIO22`.

Todo se cambia en la sección **CONFIGURACIÓN** de
[`firmware/controlesp/controlesp.ino`](firmware/controlesp/controlesp.ino):
para añadir una luz o un sensor basta con agregar una línea a `salidas[]` o
`entradas[]`. El `id` de cada salida es su posición en la lista (0, 1, 2…).

**Pines a evitar:** 6-11 (flash), 0/12/15 (arranque). Para analógicas usa
**32-39** (ADC1); los del ADC2 no funcionan con WiFi activo. 34-39 son solo
entrada.

## 3. Programar el ESP32 (Arduino IDE)

1. Instala [Arduino IDE 2](https://www.arduino.cc/en/software).
2. *Archivo → Preferencias →* "URLs adicionales de gestor de placas":
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. *Herramientas → Placa → Gestor de placas*: busca **esp32** (de Espressif) e instálalo.
4. Abre `firmware/controlesp/controlesp.ino`.
5. Si vas a usar WiFi, cambia `WIFI_SSID` y `WIFI_PASSWORD` (red de **2.4 GHz**).
   Si solo usarás USB, puedes dejarlos como están.
6. *Herramientas → Placa:* **ESP32 Dev Module**; *Puerto:* el COM del ESP32.
7. Pulsa **Subir**. Si se queda en `Connecting.....`, mantén pulsado **BOOT**.
8. Abre el *Monitor Serie* a **115200 baudios**, con final de línea **"Nueva línea"**,
   y pulsa **EN/RST**. Verás:
   ```
   # WiFi OK. IP: 192.168.1.50  (TCP puerto 5000, web http://192.168.1.50/)
   # Listo. Comandos: PING, INFO, GET, SET <id> <valor>, STREAM <ms>
   ```
   Escribe `GET` y debe responder `DATA,0,0,0,0,...`. Escribe `SET 2 1`: se
   enciende el LED azul de la placa.
9. **Cierra el Monitor Serie** antes de abrir el puerto desde LabVIEW (solo un
   programa puede usar el COM a la vez).

No hace falta instalar librerías: todo viene con el paquete del ESP32.

## 4. Protocolo (resumen)

| Comando           | Respuesta                    |
|-------------------|------------------------------|
| `PING`            | `PONG`                       |
| `INFO`            | `INFO,<nSal>,<nEnt>,<nombres...>` |
| `GET`             | `DATA,<salidas...>,<entradas...>` |
| `SET <id> <valor>`| `OK,<id>,<valor>`            |
| `STREAM <ms>`     | `OK,STREAM,<ms>` y `DATA,...` cada `<ms>` (0 = parar) |

Comandos terminados en `\n`; respuestas terminadas en `\r\n`. Las líneas que
empiezan por `#` son informativas. Detalles y el VI paso a paso en
[docs/LABVIEW.md](docs/LABVIEW.md).

Para probar sin LabVIEW: `python tools/probar_esp32.py --serial COM3` o
`python tools/probar_esp32.py --ip 192.168.1.50`.

## 5. API HTTP (JSON)

Además de LabVIEW, puedes abrir `http://<ip>/` en el navegador para tener una
página de control, o usar la API:

| Método | Ruta           | Parámetros              | Respuesta                    |
|--------|----------------|-------------------------|------------------------------|
| GET    | `/api/estado`  | —                       | JSON con salidas y entradas  |
| POST   | `/api/salida`  | `id`, `valor`           | `{"id":0,"valor":1}`         |

```bash
curl http://192.168.1.50/api/estado
curl -X POST -d "id=0&valor=1" http://192.168.1.50/api/salida
```

## Estructura

```
firmware/controlesp/
├── controlesp.ino   # configuración de pines, protocolo Serial/TCP, servidor web
└── pagina.h         # página web (opcional) guardada en la flash del ESP32
docs/LABVIEW.md      # cómo armar el VI en LabVIEW (USB y WiFi)
tools/probar_esp32.py# consola de prueba en Python (mismo protocolo)
```
