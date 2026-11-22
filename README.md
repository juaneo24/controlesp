# controlesp

Control de luces y lectura de sensores con un **ESP32** desde una **página web**
(en el celular o la PC), por WiFi.

```
 [Navegador / app]  ── WiFi (HTTP + JSON) ──►  [ESP32]  ──►  relés / LEDs (salidas)
        ▲                                         │
        └────────── estado cada 1 s ◄─────────────┘◄──  sensores (entradas)
```

El propio ESP32 hace de servidor web: sirve la página y una API JSON. No hace
falta ningún servidor aparte ni internet, solo que el ESP32 y tu celular/PC
estén en la misma red WiFi.

## 1. Material

- ESP32 (DevKit / "ESP32 Dev Module")
- Módulo de relés (para luces de 110/220 V) o LEDs con resistencia (para probar)
- Sensores opcionales: LDR, botón, sensor de puerta, potenciómetro…
- Cable USB **de datos** (no solo de carga)

> ⚠️ Si vas a controlar luces de la red eléctrica, hazlo siempre a través de un
> **módulo de relé** y con la instalación desconectada mientras cableas.

## 2. Conexiones por defecto

| Función            | GPIO | Notas                                              |
|--------------------|------|----------------------------------------------------|
| Luz sala (relé)    | 23   | Relé activo en LOW (`invertida = true`)            |
| Luz cocina (relé)  | 22   | Relé activo en LOW                                 |
| LED de la placa    | 2    | Sirve para probar sin cablear nada                 |
| Tira LED (PWM)     | 21   | Intensidad 0-255, vía MOSFET si es tira de 12 V    |
| Sensor de luz LDR  | 34   | Entrada analógica (0-4095). Divisor con 10 kΩ      |
| Puerta / botón     | 4    | `INPUT_PULLUP`: conecta el contacto entre GPIO4 y GND |

Relé: `VCC → 5V (VIN)`, `GND → GND`, `IN1 → GPIO23`, `IN2 → GPIO22`.

Todo esto se cambia en la sección **CONFIGURACIÓN** de
[`firmware/controlesp/controlesp.ino`](firmware/controlesp/controlesp.ino):
para añadir una luz o un sensor basta con agregar una línea a `salidas[]` o
`entradas[]`; la página se genera sola a partir de esa lista.

**Pines a evitar:** 6-11 (flash), 0/12/15 (arranque). Para analógicas usa
**32-39** (ADC1); los pines del ADC2 no funcionan con WiFi activo. 34-39 son
solo entrada.

## 3. Programar el ESP32 (Arduino IDE)

1. Instala [Arduino IDE 2](https://www.arduino.cc/en/software).
2. *Archivo → Preferencias →* "URLs adicionales de gestor de placas":
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. *Herramientas → Placa → Gestor de placas*: busca **esp32** (de Espressif) e instálalo.
4. Abre `firmware/controlesp/controlesp.ino`.
5. Cambia `WIFI_SSID` y `WIFI_PASSWORD` por los de tu red (tiene que ser **2.4 GHz**;
   el ESP32 no se conecta a redes de 5 GHz).
6. *Herramientas → Placa:* **ESP32 Dev Module**; *Puerto:* el COM/ttyUSB del ESP32.
7. Pulsa **Subir**. Si se queda en `Connecting.....`, mantén pulsado el botón
   **BOOT** de la placa hasta que empiece a escribir.
8. Abre el *Monitor Serie* a **115200 baudios** y pulsa **EN/RST**. Verás:
   ```
   Conectado. Abre en el navegador: http://192.168.1.50
   También disponible en: http://controlesp.local
   ```

No hace falta instalar librerías: `WiFi`, `WebServer` y `ESPmDNS` vienen con
el paquete del ESP32.

## 4. Usarlo

Abre en el navegador la IP que mostró el Monitor Serie (o
`http://controlesp.local`; en Android a veces `.local` no funciona, usa la IP).
Verás botones ON/OFF para cada luz, un deslizador para las salidas PWM y los
valores de los sensores actualizándose cada segundo.

Consejo: en tu router asigna una **IP fija (reserva DHCP)** al ESP32 para que
la dirección no cambie.

## 5. API (para conectar tu propia app o página)

| Método | Ruta           | Parámetros              | Respuesta                    |
|--------|----------------|-------------------------|------------------------------|
| GET    | `/api/estado`  | —                       | JSON con salidas y entradas  |
| POST   | `/api/salida`  | `id`, `valor`           | `{"id":0,"valor":1}`         |

`valor` es `0`/`1` en salidas normales y `0`–`255` en PWM. Los parámetros van
como formulario (`application/x-www-form-urlencoded`) o en la URL. También se
acepta GET en `/api/salida` para probar rápido desde el navegador.

Ejemplo de `/api/estado`:

```json
{
  "salidas":  [{"id":0,"nombre":"Luz sala","gpio":23,"pwm":false,"valor":1}, ...],
  "entradas": [{"id":0,"nombre":"Sensor de luz (LDR)","gpio":34,"tipo":"analogica","valor":2210}, ...],
  "ip": "192.168.1.50", "rssi": -58, "uptime_s": 3600
}
```

Probar desde la terminal:

```bash
curl http://192.168.1.50/api/estado
curl -X POST -d "id=0&valor=1" http://192.168.1.50/api/salida   # enciende Luz sala
curl -X POST -d "id=3&valor=128" http://192.168.1.50/api/salida # tira LED al 50 %
```

Desde JavaScript (otra página en tu red):

```js
await fetch('http://192.168.1.50/api/salida', {
  method: 'POST',
  body: new URLSearchParams({ id: 0, valor: 1 })
});
const estado = await (await fetch('http://192.168.1.50/api/estado')).json();
```

La API envía cabeceras CORS, así que otra página de tu red puede llamarla.
Ojo: una página servida por **HTTPS** desde internet (p. ej. alojada en Vercel)
**no puede** llamar a `http://192.168.x.x` — el navegador lo bloquea. Para eso
ve al punto siguiente.

## 6. Control desde fuera de casa (siguiente paso)

Esta versión funciona en tu red local. Para manejarlo desde cualquier lugar,
lo habitual es usar un **broker MQTT** (HiveMQ Cloud, EMQX, Adafruit IO…): el
ESP32 publica las lecturas y se suscribe a órdenes, y la web/app habla con el
broker por WebSocket seguro. Otras opciones: Blynk, Firebase o Supabase
Realtime. La lista de salidas/entradas y la lógica de `aplicarSalida()` se
reutilizan tal cual.

## Estructura

```
firmware/controlesp/
├── controlesp.ino   # WiFi, servidor web, API y configuración de pines
└── pagina.h         # página web (HTML/CSS/JS) guardada en la flash del ESP32
```
