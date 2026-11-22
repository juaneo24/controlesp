/*
 * controlesp — Control de luces y lectura de sensores con ESP32 desde LabVIEW.
 *
 * El ESP32 entiende el MISMO protocolo de texto por dos vías:
 *   - USB / Serial  (LabVIEW: VISA Serial, 115200 baudios)
 *   - WiFi / TCP    (LabVIEW: TCP Open Connection, puerto 5000)
 *
 * Cada comando es una línea terminada en '\n' (o \r\n). Cada respuesta termina en \r\n.
 *   PING              -> PONG
 *   INFO              -> INFO,<nSalidas>,<nEntradas>,<nombre1>,<nombre2>,...
 *   GET               -> DATA,<salida0>,...,<salidaN>,<entrada0>,...,<entradaM>
 *   SET <id> <valor>  -> OK,<id>,<valor>     (valor 0/1, o 0-255 si la salida es PWM)
 *   STREAM <ms>       -> OK,STREAM,<ms>      y luego envía una línea DATA cada <ms>
 *   STREAM 0          -> OK,STREAM,0         (detiene el envío automático)
 *   (error)           -> ERR,<motivo>
 * Las líneas que empiezan con '#' son mensajes informativos: LabVIEW debe ignorarlas.
 *
 * Extra: también sirve una página web en http://<ip>/ y una API JSON (/api/estado, /api/salida).
 *
 * Placa:  "ESP32 Dev Module" (core de Espressif para Arduino, 2.x o 3.x)
 * No necesita librerías externas.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "pagina.h"

// ====================== CONFIGURACIÓN — EDITA ESTO ======================
// Si solo vas a usar USB, puedes dejar el WiFi así: el ESP32 sigue funcionando por Serial.
const char* WIFI_SSID     = "TU_RED_WIFI";
const char* WIFI_PASSWORD = "TU_CONTRASENA";
const char* NOMBRE_MDNS   = "controlesp";   // http://controlesp.local
const uint16_t PUERTO_TCP = 5000;           // puerto para LabVIEW por WiFi
const unsigned long BAUDIOS = 115200;

// Salidas (luces, relés, LEDs). El orden define el <id> (0, 1, 2...).
//   invertida = true  -> para módulos de relé que se activan con LOW (la mayoría).
//   pwm       = true  -> salida con intensidad 0-255 (LED dimmable con MOSFET, etc.).
// Usa nombres sin tildes ni comas (LabVIEW los recibe separados por comas).
struct Salida {
  const char* nombre;
  uint8_t gpio;
  bool pwm;
  bool invertida;
  int valor;          // estado lógico actual: 0/1, o 0-255 si es PWM
};

Salida salidas[] = {
  { "Luz sala",    23, false, true,  0 },
  { "Luz cocina",  22, false, true,  0 },
  { "LED placa",    2, false, false, 0 },
  { "Tira LED",    21, true,  false, 0 },
};

// Entradas (sensores). Analógicas: usa GPIO 32-39 (ADC1, funcionan con WiFi activo).
enum TipoEntrada { DIGITAL, ANALOGICA };
struct Entrada {
  const char* nombre;
  uint8_t gpio;
  TipoEntrada tipo;
};

Entrada entradas[] = {
  { "Sensor luz LDR", 34, ANALOGICA },   // 0-4095
  { "Puerta boton",    4, DIGITAL   },   // INPUT_PULLUP: 1 = abierto, 0 = cerrado a GND
};
// ========================================================================

const size_t N_SALIDAS  = sizeof(salidas)  / sizeof(salidas[0]);
const size_t N_ENTRADAS = sizeof(entradas) / sizeof(entradas[0]);

WebServer server(80);
WiFiServer servidorTCP(PUERTO_TCP);
WiFiClient clienteTCP;

// Estado de cada canal de comandos (Serial y TCP).
struct Canal {
  String buffer;
  unsigned long periodoStream = 0;   // 0 = sin envío automático
  unsigned long ultimoEnvio = 0;
};
Canal canalSerial, canalTCP;

// ======================= Entradas / salidas =======================

void aplicarSalida(Salida& s) {
  if (s.pwm) {
    int v = constrain(s.valor, 0, 255);
    analogWrite(s.gpio, s.invertida ? 255 - v : v);
  } else {
    bool encendida = s.valor != 0;
    digitalWrite(s.gpio, (encendida != s.invertida) ? HIGH : LOW);
  }
}

int leerEntrada(const Entrada& e) {
  return e.tipo == ANALOGICA ? analogRead(e.gpio) : digitalRead(e.gpio);
}

// Cambia una salida. Devuelve false si el id no existe.
bool cambiarSalida(long id, long valor) {
  if (id < 0 || id >= (long)N_SALIDAS) return false;
  Salida& s = salidas[id];
  s.valor = s.pwm ? constrain(valor, 0, 255) : (valor ? 1 : 0);
  aplicarSalida(s);
  return true;
}

// ================== Protocolo de texto (LabVIEW) ==================

String lineaDatos() {
  String r = "DATA";
  for (size_t i = 0; i < N_SALIDAS; i++)  { r += ','; r += salidas[i].valor; }
  for (size_t i = 0; i < N_ENTRADAS; i++) { r += ','; r += leerEntrada(entradas[i]); }
  return r;
}

String lineaInfo() {
  String r = "INFO," + String(N_SALIDAS) + "," + String(N_ENTRADAS);
  for (size_t i = 0; i < N_SALIDAS; i++)  { r += ','; r += salidas[i].nombre; }
  for (size_t i = 0; i < N_ENTRADAS; i++) { r += ','; r += entradas[i].nombre; }
  return r;
}

// Interpreta una línea. Acepta espacios o comas como separador: "SET 0 1" o "SET,0,1".
String procesarComando(String linea, Canal& canal) {
  linea.trim();
  linea.replace(',', ' ');
  char cmd[16] = {0};
  long a = 0, b = 0;
  int n = sscanf(linea.c_str(), "%15s %ld %ld", cmd, &a, &b);
  String c = String(cmd);
  c.toUpperCase();

  if (n < 1)          return "ERR,linea vacia";
  if (c == "PING")    return "PONG";
  if (c == "INFO")    return lineaInfo();
  if (c == "GET")     return lineaDatos();

  if (c == "SET") {
    if (n < 3)                  return "ERR,uso: SET <id> <valor>";
    if (!cambiarSalida(a, b))   return "ERR,salida no existe";
    return "OK," + String(a) + "," + String(salidas[a].valor);
  }

  if (c == "STREAM") {
    if (n < 2) return "ERR,uso: STREAM <ms>";
    canal.periodoStream = (a <= 0) ? 0 : max(a, 20L);   // mínimo 20 ms
    canal.ultimoEnvio = millis();
    return "OK,STREAM," + String(canal.periodoStream);
  }

  return "ERR,comando desconocido";
}

// Lee caracteres de un Stream (Serial o cliente TCP), arma líneas y responde.
void atenderCanal(Stream& s, Canal& canal) {
  while (s.available()) {
    char ch = (char)s.read();
    if (ch == '\n' || ch == '\r') {
      if (canal.buffer.length()) {
        s.print(procesarComando(canal.buffer, canal));
        s.print("\r\n");
        canal.buffer = "";
      }
    } else if (canal.buffer.length() < 64) {
      canal.buffer += ch;
    }
  }
  if (canal.periodoStream && millis() - canal.ultimoEnvio >= canal.periodoStream) {
    canal.ultimoEnvio = millis();
    s.print(lineaDatos());
    s.print("\r\n");
  }
}

void atenderTCP() {
  // Un cliente a la vez: si se conecta uno nuevo (p. ej. LabVIEW se reinició), reemplaza al anterior.
  WiFiClient nuevo = servidorTCP.available();
  if (nuevo) {
    if (clienteTCP) clienteTCP.stop();
    clienteTCP = nuevo;
    clienteTCP.setNoDelay(true);
    canalTCP = Canal();
    Serial.printf("# Cliente TCP conectado: %s\n", clienteTCP.remoteIP().toString().c_str());
  }
  if (clienteTCP && clienteTCP.connected()) atenderCanal(clienteTCP, canalTCP);
}

// ======================= Web (opcional) =======================

void enviarCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void enviarError(int codigo, const char* mensaje) {
  enviarCORS();
  server.send(codigo, "application/json", String("{\"error\":\"") + mensaje + "\"}");
}

void manejarRaiz() {
  server.send_P(200, "text/html; charset=utf-8", PAGINA_HTML);
}

void manejarEstado() {
  String json;
  json.reserve(768);
  json += "{\"salidas\":[";
  for (size_t i = 0; i < N_SALIDAS; i++) {
    const Salida& s = salidas[i];
    if (i) json += ',';
    json += "{\"id\":" + String(i);
    json += ",\"nombre\":\"" + String(s.nombre) + "\"";
    json += ",\"gpio\":" + String(s.gpio);
    json += ",\"pwm\":" + String(s.pwm ? "true" : "false");
    json += ",\"valor\":" + String(s.valor) + "}";
  }
  json += "],\"entradas\":[";
  for (size_t i = 0; i < N_ENTRADAS; i++) {
    const Entrada& e = entradas[i];
    if (i) json += ',';
    json += "{\"id\":" + String(i);
    json += ",\"nombre\":\"" + String(e.nombre) + "\"";
    json += ",\"gpio\":" + String(e.gpio);
    json += ",\"tipo\":\"" + String(e.tipo == ANALOGICA ? "analogica" : "digital") + "\"";
    json += ",\"valor\":" + String(leerEntrada(e)) + "}";
  }
  json += "],\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"uptime_s\":" + String(millis() / 1000);
  json += "}";

  enviarCORS();
  server.send(200, "application/json", json);
}

void manejarSalida() {
  if (!server.hasArg("id") || !server.hasArg("valor")) {
    enviarError(400, "faltan parametros id y valor");
    return;
  }
  long id = server.arg("id").toInt();
  if (!cambiarSalida(id, server.arg("valor").toInt())) {
    enviarError(404, "salida no existe");
    return;
  }
  enviarCORS();
  server.send(200, "application/json",
              "{\"id\":" + String(id) + ",\"valor\":" + String(salidas[id].valor) + "}");
}

void manejarNoEncontrado() {
  enviarCORS();
  if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
  server.send(404, "application/json", "{\"error\":\"ruta no encontrada\"}");
}

// ======================= WiFi =======================

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("# Conectando a %s...\n", WIFI_SSID);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("# WiFi OK. IP: %s  (TCP puerto %u, web http://%s/)\n",
                  WiFi.localIP().toString().c_str(), PUERTO_TCP,
                  WiFi.localIP().toString().c_str());
  } else {
    // Sin WiFi el ESP32 sigue funcionando por USB; reintenta conectarse en segundo plano.
    Serial.println("# Sin WiFi: funcionando solo por USB/Serial");
  }
}

// ======================= Arduino =======================

void setup() {
  Serial.begin(BAUDIOS);
  delay(200);

  for (size_t i = 0; i < N_SALIDAS; i++) {
    if (!salidas[i].pwm) pinMode(salidas[i].gpio, OUTPUT);
    aplicarSalida(salidas[i]);              // arranca todo apagado
  }
  for (size_t i = 0; i < N_ENTRADAS; i++) {
    if (entradas[i].tipo == DIGITAL) pinMode(entradas[i].gpio, INPUT_PULLUP);
  }
  analogReadResolution(12);                 // 0-4095

  conectarWiFi();
  if (MDNS.begin(NOMBRE_MDNS)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, manejarRaiz);
  server.on("/api/estado", HTTP_GET, manejarEstado);
  server.on("/api/salida", HTTP_POST, manejarSalida);
  server.on("/api/salida", HTTP_GET, manejarSalida);
  server.onNotFound(manejarNoEncontrado);
  server.begin();
  servidorTCP.begin();

  Serial.println("# Listo. Comandos: PING, INFO, GET, SET <id> <valor>, STREAM <ms>");
}

void loop() {
  atenderCanal(Serial, canalSerial);   // LabVIEW por USB (VISA)
  atenderTCP();                        // LabVIEW por WiFi (TCP)
  server.handleClient();               // página web

  static unsigned long ultimoIntento = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - ultimoIntento > 10000) {
    ultimoIntento = millis();
    WiFi.reconnect();
  }
}
