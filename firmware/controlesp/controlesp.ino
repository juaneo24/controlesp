/*
 * controlesp — Control de luces y lectura de sensores con ESP32 desde una página web.
 *
 * Qué hace:
 *   1. El ESP32 se conecta a tu red WiFi.
 *   2. Levanta un servidor web en el puerto 80.
 *   3. Sirve una página (pagina.h) para controlar salidas y ver entradas.
 *   4. Expone una API JSON que puede usar cualquier otra app:
 *        GET  /api/estado               -> estado de todas las salidas y entradas
 *        POST /api/salida  id=N valor=V -> cambia una salida (0/1 o 0-255 si es PWM)
 *
 * Placa:  "ESP32 Dev Module" (core de Espressif para Arduino, 2.x o 3.x)
 * No necesita librerías externas: WiFi, WebServer y ESPmDNS vienen con el core.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "pagina.h"

// ====================== CONFIGURACIÓN — EDITA ESTO ======================
const char* WIFI_SSID     = "TU_RED_WIFI";
const char* WIFI_PASSWORD = "TU_CONTRASENA";
const char* NOMBRE_MDNS   = "controlesp";   // la página quedará en http://controlesp.local

// Salidas (luces, relés, LEDs).
//   invertida = true  -> para módulos de relé que se activan con LOW (la mayoría).
//   pwm       = true  -> salida con intensidad 0-255 (LED dimmable con MOSFET, etc.).
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
  { "Sensor de luz (LDR)", 34, ANALOGICA },
  { "Puerta / botón",       4, DIGITAL   },   // con INPUT_PULLUP: 1 = abierto, 0 = cerrado a GND
};
// ========================================================================

const size_t N_SALIDAS  = sizeof(salidas)  / sizeof(salidas[0]);
const size_t N_ENTRADAS = sizeof(entradas) / sizeof(entradas[0]);

WebServer server(80);

// Aplica el valor lógico de una salida al pin físico.
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

// Cabeceras CORS: permiten llamar a la API desde otra página/app de tu red.
void enviarCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void enviarError(int codigo, const char* mensaje) {
  enviarCORS();
  server.send(codigo, "application/json", String("{\"error\":\"") + mensaje + "\"}");
}

// ---------- Manejadores HTTP ----------

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

// POST /api/salida  con parámetros id y valor (en la URL o en el cuerpo form-urlencoded)
void manejarSalida() {
  if (!server.hasArg("id") || !server.hasArg("valor")) {
    enviarError(400, "faltan parametros id y valor");
    return;
  }
  long id = server.arg("id").toInt();
  long valor = server.arg("valor").toInt();
  if (id < 0 || id >= (long)N_SALIDAS) {
    enviarError(404, "salida no existe");
    return;
  }
  Salida& s = salidas[id];
  s.valor = s.pwm ? constrain(valor, 0, 255) : (valor ? 1 : 0);
  aplicarSalida(s);
  Serial.printf("Salida %ld (%s, GPIO %d) -> %d\n", id, s.nombre, s.gpio, s.valor);

  enviarCORS();
  server.send(200, "application/json",
              "{\"id\":" + String(id) + ",\"valor\":" + String(s.valor) + "}");
}

void manejarOpciones() {   // pre-vuelo CORS del navegador
  enviarCORS();
  server.send(204);
}

void manejarNoEncontrado() {
  if (server.method() == HTTP_OPTIONS) { manejarOpciones(); return; }
  enviarError(404, "ruta no encontrada");
}

// ---------- WiFi ----------

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Conectando a %s", WIFI_SSID);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - inicio > 20000) {          // 20 s sin conexión: reinicia y reintenta
      Serial.println("\nNo se pudo conectar. Reiniciando...");
      ESP.restart();
    }
  }
  Serial.printf("\nConectado. Abre en el navegador: http://%s\n",
                WiFi.localIP().toString().c_str());
}

// ---------- Arduino ----------

void setup() {
  Serial.begin(115200);
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

  if (MDNS.begin(NOMBRE_MDNS)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("También disponible en: http://%s.local\n", NOMBRE_MDNS);
  }

  server.on("/", HTTP_GET, manejarRaiz);
  server.on("/api/estado", HTTP_GET, manejarEstado);
  server.on("/api/salida", HTTP_POST, manejarSalida);
  server.on("/api/salida", HTTP_GET, manejarSalida);   // cómodo para probar desde el navegador
  server.onNotFound(manejarNoEncontrado);
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient();

  // Si se cae el WiFi, intenta reconectar cada 10 s sin bloquear.
  static unsigned long ultimoIntento = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - ultimoIntento > 10000) {
    ultimoIntento = millis();
    WiFi.reconnect();
  }
}
