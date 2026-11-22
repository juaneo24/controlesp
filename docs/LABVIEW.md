# Conectar el ESP32 con LabVIEW

El ESP32 y LabVIEW se comunican con **líneas de texto**: LabVIEW envía un
comando, el ESP32 responde con una línea. Funciona igual por **USB** (VISA) o
por **WiFi** (TCP); elige la vía que prefieras.

| Vía   | Bloques de LabVIEW                         | Cuándo usarla                             |
|-------|--------------------------------------------|-------------------------------------------|
| USB   | VISA (paleta *Instrument I/O → Serial*)    | ESP32 conectado al PC. La más sencilla.   |
| WiFi  | TCP (paleta *Data Communication → Protocols → TCP*) | Sin cable; ESP32 en la misma red. |

Requisitos para USB: tener instalado **NI-VISA** (viene con LabVIEW o se
descarga gratis de NI) y el driver USB del ESP32 (CP210x o CH340, según tu
placa). El puerto aparece como `COM3`, `COM4`…

## 1. El protocolo

Cada comando termina en salto de línea (`\n`). Cada respuesta termina en `\r\n`.

| Envías (LabVIEW)  | Responde el ESP32                         | Significado                                   |
|-------------------|-------------------------------------------|-----------------------------------------------|
| `PING`            | `PONG`                                    | Comprobar la conexión                         |
| `INFO`            | `INFO,4,2,Luz sala,Luz cocina,LED placa,Tira LED,Sensor luz LDR,Puerta boton` | Cuántas salidas/entradas hay y sus nombres |
| `GET`             | `DATA,1,0,0,128,2210,1`                   | Valores actuales (ver abajo)                  |
| `SET 0 1`         | `OK,0,1`                                  | Enciende la salida 0 (Luz sala)               |
| `SET 0 0`         | `OK,0,0`                                  | Apaga la salida 0                             |
| `SET 3 200`       | `OK,3,200`                                | Salida PWM 3 (Tira LED) a 200 de 255          |
| `STREAM 200`      | `OK,STREAM,200` y luego `DATA,...` cada 200 ms | Envío automático de datos               |
| `STREAM 0`        | `OK,STREAM,0`                             | Detiene el envío automático                   |
| (algo inválido)   | `ERR,motivo`                              | Error                                         |

**Formato de `DATA`** con la configuración por defecto:

```
DATA, salida0, salida1, salida2, salida3, entrada0, entrada1
DATA,    1   ,    0   ,    0   ,   128  ,   2210  ,    1
       Luz sala Luz coc. LED plac Tira LED  LDR(0-4095) Puerta(0/1)
```

Primero van todas las salidas (en el orden de `salidas[]` del firmware) y
después todas las entradas (orden de `entradas[]`). Si agregas o quitas
elementos en el firmware, cambian las columnas; `INFO` te dice cuántas hay.

Las líneas que empiezan con `#` son mensajes informativos del ESP32 (por
ejemplo al arrancar): ignóralas.

> Antes de ir a LabVIEW puedes comprobar que todo responde con el Monitor Serie
> de Arduino (115200 baudios, "Nueva línea") escribiendo `PING` o `GET`, o con
> `python tools/probar_esp32.py --serial COM3`.

## 2. VI por USB (VISA) — paso a paso

Diseño "pregunta-respuesta": en cada vuelta del bucle se envía `GET` y se lee
la respuesta. Es el más fácil de depurar.

### Panel frontal

- `VISA resource name` (control) → elige el COM del ESP32.
- 3 interruptores booleanos: **Luz sala**, **Luz cocina**, **LED placa**.
- 1 *slide* numérico 0–255 (entero U8/I32): **Tira LED**.
- 1 *Waveform Chart*: **LDR**; 1 LED indicador: **Puerta**.
- 1 indicador de texto: **Respuesta** (útil para depurar).
- Botón **STOP**.

### Diagrama de bloques

```
VISA Configure Serial Port ──► Wait 2000 ms ──► VISA Flush I/O Buffer ──► [ While Loop ] ──► VISA Close ──► Simple Error Handler
```

1. **VISA Configure Serial Port**
   - `baud rate` = **115200**, `data bits` = 8, `parity` = None, `stop bits` = 1.0, `flow control` = None.
   - `Enable Termination Char` = **True**, `termination char` = **10** (`\n`, es el valor por defecto).
   - `timeout` = 2000 ms.
2. **Wait (ms)** 2000 → al abrir el puerto la placa ESP32 se reinicia sola;
   hay que darle tiempo a arrancar.
3. **VISA Flush I/O Buffer** (mask = 16, *receive buffer discard*) para tirar
   los mensajes de arranque.
4. **While Loop** (pasa el `VISA resource` y el `error` por *shift registers*):

   **a) Enviar órdenes a las salidas.** Para cada interruptor, mete el
   valor en un *shift register* y compáralo con el de la vuelta anterior
   (`Not Equal?`). Si cambió, en un *Case Structure* (True):
   - `Boolean To (0,1)` → **Format Into String** con formato `SET 0 %d\n`
     (usa `SET 1 %d\n` para Luz cocina, `SET 3 %d\n` para el slide, etc.).

     > Para que LabVIEW interprete `\n` como salto de línea, en la
     > constante de formato haz clic derecho → **'\' Codes Display**. O
     > concatena la constante **Line Feed** de la paleta *String*.

   - **VISA Write** con ese texto.
   - **VISA Read** (`byte count` = 100) → la respuesta `OK,0,1`.

   Es importante leer la respuesta de cada `SET`, para que la siguiente
   lectura de `GET` no reciba un `OK` que se había quedado en el buffer.

   *Alternativa más limpia:* un **Event Structure** con un caso "Value
   Change" por cada control y un caso **Timeout** (100 ms) para leer `GET`.

   **b) Leer los datos.**
   - **VISA Write** `GET\n`.
   - **VISA Read** (`byte count` = 100). Gracias al carácter de terminación,
     se detiene al final de la línea: obtienes `DATA,1,0,0,128,2210,1\r\n`.
   - **Trim Whitespace** (quita el `\r\n`).
   - **Match Pattern** con la expresión `DATA,` → la salida `after substring`
     es `1,0,0,128,2210,1`. Si `offset past match` es −1, la línea no era de
     datos: ignórala en un *Case Structure*.
   - **Spreadsheet String To Array**: `format string` = `%d`, `delimiter` = `,`,
     `array type` = **constante de arreglo 1D de I32** (así devuelve 1D).
   - **Index Array** con el arreglo expandido para sacar los elementos 0…5:
     - elementos 0–3 → estado real de las salidas (para indicadores).
     - elemento 4 → LDR (0–4095). Para pasarlo a voltios: `× 3.3 ÷ 4095`.
       Llévalo al *Waveform Chart*.
     - elemento 5 → Puerta: `≠ 0` → LED indicador.

   **c)** **Wait (ms)** 100 → unas 10 lecturas por segundo.

   **d)** Condición de parada: `STOP` **OR** `status` del error (`Unbundle By Name`).

5. Fuera del bucle: **VISA Close** y **Simple Error Handler**.

### Errores típicos por USB

| Síntoma | Causa / solución |
|---|---|
| Error −1073807339 (timeout) | No llega respuesta: puerto equivocado, baudios ≠ 115200, falta el `\n` al final del comando o no esperaste los 2 s tras abrir. |
| Error −1073807246 (recurso ocupado) | El Monitor Serie de Arduino (u otro programa) tiene abierto el COM. Ciérralo. |
| Llegan textos raros al principio | Mensajes de arranque del ESP32: haz el *Flush* tras la espera o ignora las líneas que no empiecen por `DATA`/`OK`. |
| Los valores están "una vuelta atrasados" | Hay respuestas `OK` sin leer: lee después de cada `VISA Write`. |

## 3. VI por WiFi (TCP)

Igual que el de USB, cambiando los bloques VISA por los de TCP:

| USB (VISA)                  | WiFi (TCP)                                                   |
|-----------------------------|--------------------------------------------------------------|
| VISA Configure Serial Port  | **TCP Open Connection**: `address` = IP del ESP32 (p. ej. `192.168.1.50`), `remote port` = **5000**, `timeout` = 3000 |
| Wait 2000 + Flush           | (no hace falta)                                              |
| VISA Write                  | **TCP Write**                                                |
| VISA Read                   | **TCP Read** con `mode` = **CRLF**, `bytes to read` = 100, `timeout` = 1000 |
| VISA Close                  | **TCP Close Connection**                                     |

- La IP la muestra el ESP32 por el Monitor Serie al arrancar (`# WiFi OK. IP: ...`).
  Reserva esa IP en tu router para que no cambie.
- El modo **CRLF** de *TCP Read* devuelve una línea completa (el ESP32 termina
  cada respuesta con `\r\n`).
- Solo se atiende **un cliente TCP a la vez**; si LabVIEW se vuelve a conectar,
  reemplaza a la conexión anterior.
- Si no conecta: revisa que el PC y el ESP32 estén en la misma red y que el
  firewall de Windows permita a LabVIEW salir por la red.

## 4. Modo "streaming" (opcional, para graficar rápido)

En lugar de enviar `GET` en cada vuelta, envía una sola vez `STREAM 50` al
inicio; el ESP32 mandará una línea `DATA,...` cada 50 ms. En el bucle solo
haces **VISA Read / TCP Read** y procesas igual. Ten en cuenta que entonces las
respuestas `OK,...` de tus `SET` llegan mezcladas con las líneas `DATA`:
procesa cada línea según su prefijo (`DATA` → graficar, `OK` → ignorar o
mostrar). Envía `STREAM 0` antes de cerrar.

## 5. Otra opción: HTTP + JSON

El ESP32 también tiene una API web. Desde LabVIEW (paleta
*Data Communication → Protocols → HTTP Client*):

- **GET.vi** a `http://IP/api/estado` → **Unflatten From JSON** a un *cluster*.
- **POST.vi** a `http://IP/api/salida` con el cuerpo `id=0&valor=1`.

Es más lento que TCP (abre una conexión por petición), pero útil si ya
trabajas con JSON. El formato está en el [README](../README.md#5-api-http-json).
