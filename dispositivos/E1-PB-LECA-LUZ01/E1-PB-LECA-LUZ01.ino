/*
 * AURA - E1-PB-LECA-LUZ01: control de iluminación del aula del LabECA
 * Contrato v4.0, hoja de la mesh ESP-WIFI-MESH (interior).
 *
 * Mide la presencia con un radar LD2420 (UART1) y decide localmente si la luz
 * artificial del aula debe estar encendida (regla_luz.h):
 *
 *   sin presencia → OFF; con presencia → depende de cuánta luz haya:
 *   aporte = luz_ventana/umbral_ventana_lx + luz_aula/umbral_aula_lx, y se
 *   enciende si aporte < 0.95 (histéresis anti-parpadeo).
 *
 * PRUEBAS SOLO CON EL LD2420: por ahora está conectado únicamente el radar de
 * presencia. El código de los BH1750 y del relé NO se borró: está COMENTADO
 * (buscá "ETAPA 2" en el archivo). Cuando lleguen los componentes se
 * descomenta todo lo marcado, se definen los pines en config_local.h y se
 * sigue. Hoy la regla recibe lux en 0 y decide solo con la presencia.
 *
 * Ventana de distancia: la presencia solo cuenta si el radar la mide entre
 * DIST_MIN_CM y DIST_MAX_CM (~1 m: 60..140 cm), y se mantiene hasta 40..160
 * (histéresis contra la precisión ±0.35 m del sensor). El radar igual detecta
 * más lejos; nosotros lo ignoramos. Cambiar las constantes para mover la
 * ventana. El monitor serie imprime la distancia medida.
 *
 * LED de presencia en LED_PRESENCIA_PIN (GPIO14 por defecto): prende mientras
 * la distancia reportada esté dentro de la ventana y el radar conteste, y se
 * apaga si la persona se aleja (o si falla el sensor). Sirve para calibrar a
 * mano la distancia sin mirar el monitor serie.
 *
 * Modos:
 *   - "auto" (default): decide la regla local.
 *   - "manual": decide solo el comando de AURA. El comando "rele" está
 *     comentado hasta que llegue el relé (ETAPA 2); con el relé, acciona y
 *     pasa a manual, y set_config {"modo":"auto"} devuelve la regla.
 *
 * set_config: intervalo_s (5..86400), umbral_ventana_lx y umbral_aula_lx
 * (1..20000), delay_off_s (0..3600), modo ("auto"|"manual"). Todo validado
 * antes de aplicar nada y guardado en flash (contrato §3.3).
 *
 * Medición (hoy, solo con el LD2420): {"presencia": 0|1}, cada intervalo_s y
 * en cada cambio de presencia. Con los componentes descomentados se agregan
 * {"luz_ventana_lx", "luz_aula_lx", "rele_canal1", "rele_canal2"}. Una sonda
 * que falla no manda valor centinela: su campo desaparece y va la alerta
 * "sensor".
 *
 * No conoce AURA: es una hoja y se identifica por la MAC de su placa
 * (hw_id "mac-..."). Los datos de la mesh van en config_local.h; sin
 * config_local.h compila, mide y guarda en la cola, pero no envía.
 *
 * IDE: placa "ESP32S3 Dev Module" (ESP32-S3-WROOM-1), USB CDC On Boot:
 * ENABLED. Bibliotecas: las de bibliotecas.txt.
 */

#if __has_include("config_local.h")
#include "config_local.h"
#endif

#include <Preferences.h>
#include <LD2420HumanMotion.h>
#include "../../comun/nodo_mesh.h"
#include "regla_luz.h"

// ETAPA 2 (BH1750): descomentar cuando estén cableados los dos sensores.
// #ifdef HAY_BH1750
// #include <Wire.h>
// #include <BH1750.h>
// #endif

// ===== Pines (config_local.h; estos son los por defecto) =====
//
// Datasheet oficial HLK-LD2420, J2 (5 pines): 3V3 | GND | OT1=UART_TX |
// RX=UART_RX | OT2=IO de presencia. Baud 115200. Ojo: NO hay pin "TX" en la
// plaquita; el TX es OT1 (en firmware <= 1.5.2 están invertidos: TX en OT2).
//
// En el ESP32-S3 los pines elegidos son los de la UART1 nativa (IO17=U1TXD,
// IO18=U1RXD) y no son strapping (esos son 0/3/45/46): no molestan en arranque.
//
//   módulo OT1 (TX)  -> LD2420_RX (GPIO18)
//   módulo RX        <- LD2420_TX (GPIO17)
//   módulo 3V3       <- 3V3 del dev kit (3.0..3.6 V, ~50 mA: NUNCA 5 V)
//   módulo GND       -> GND
#ifndef LD2420_RX
#define LD2420_RX 18
#endif
#ifndef LD2420_TX
#define LD2420_TX 17
#endif
// Baudios del radar: 115200 (datasheet oficial, firmware >= 1.5.3);
// 256000 solo en firmware <= 1.5.2.
#ifndef LD2420_BAUD
#define LD2420_BAUD 115200
#endif

// ===== LED de presencia (prende con la distancia dentro de la ventana) =====
// Pines a NO usar: strapping (0/3/45/46), USB (19/20), radar (17/18),
// I2C ETAPA 2 (8/9), relé (15/16). GPIO14 está expuesto en la placa.
#ifndef LED_PRESENCIA_PIN
#define LED_PRESENCIA_PIN 14
#endif
// true si el LED de tu placa prende con LOW (hay LEDs integrados activos a
// nivel bajo). Cablear: pin -> resistencia (220..330 ohm) -> LED -> GND.
#ifndef LED_PRESENCIA_INVERTIDO
#define LED_PRESENCIA_INVERTIDO false
#endif

// ETAPA 2 (BH1750): I2C, 8/9 son los defaults de Wire en ESP32-S3.
// #ifndef I2C_SDA
// #define I2C_SDA 8
// #endif
// #ifndef I2C_SCL
// #define I2C_SCL 9
// #endif

// ETAPA 2 (relé): doble canal, 1 = luz del aula, 2 = reservado. Descomentar
// con el relé cableado (o definirlos directo en config_local.h).
// #ifndef RELE_PIN_1
// #define RELE_PIN_1 (-1)
// #endif
// #ifndef RELE_PIN_2
// #define RELE_PIN_2 (-1)
// #endif
// #ifndef RELE_ACTIVO_BAJO
// #define RELE_ACTIVO_BAJO true
// #endif

// ===== Campos de "values" =====
const char* CAMPO_PRESENCIA = "presencia";

// ===== Configuración remota =====
const uint32_t INTERVALO_MIN_S = 5;
const uint32_t INTERVALO_MAX_S = 86400;
const uint32_t UMBRAL_MIN_LX = 1;
const uint32_t UMBRAL_MAX_LX = 20000;
const uint32_t DELAY_OFF_MAX_S = 3600;

uint32_t intervalo_s = 60;
uint32_t umbral_ventana_lx = 300;   // TODO: calibrar con el BH1750 de la ventana
uint32_t umbral_aula_lx = 300;      // TODO: calibrar con el BH1750 del aula
uint32_t delay_off_s = 0;           // retardo de apagado al perder la presencia
bool modo_auto = true;

// ===== Ventana de distancia del radar (presencia a ~1 m) =====
// El LD2420 mide la distancia de cada reporte; la presencia solo cuenta si
// cae en esta ventana. Datasheet: puertas de 70 cm (gate 2 ≈ 1.05 m) y
// precisión de distancia ±0.35 m, así que la ventana entra en 60..140 y se
// MANTIENE hasta 40..160 (histéresis): un target en el borde no la hace
// entrar y salir. El radar solo da distancia de cuerpos en MOVIMIENTO dentro
// de 8 m; sin movimiento queda el último valor y manda el retardo del sensor.
// Para mirar otra distancia cambiar las constantes y recompilar (son
// constantes, no parámetros de set_config).
const uint16_t DIST_MIN_CM = 60;
const uint16_t DIST_MAX_CM = 140;
const uint16_t DIST_HIST_CM = 20;   // margen de salida: 40..160

// ===== Estado =====
Preferences prefs;
HardwareSerial radar_serial(1);
LD2420HumanMotion radar;

bool luz_encendida = false;         // estado que decide la regla (canal 1)
// ETAPA 2 (relé): estado de los dos canales.
// bool rele_estado[2] = {false, false};
unsigned long ultimo_presencia_ms = 0;   // última vez que hubo presencia real
unsigned long ultima_muestra = 0;
bool primera = true;
bool publicar_ya = true;            // publicar en la próxima vuelta del loop
bool presencia_publicada = false;   // último valor que salió, para publicar cambios

// Luz medida por los BH1750. Sin "HAY_BH1750" (o mientras no conecten los
// sensores) quedan en 0: la regla decide solo con la presencia.
float luz_ventana_lx = 0.0;
float luz_aula_lx = 0.0;

// ETAPA 2 (BH1750): ventana en 0x23 (ADDR a GND), aula en 0x5C (ADDR a VCC).
// Si un sensor falla, la regla lo toma como 0 lx (con presencia, prende) pero
// su campo NO se publica: la sonda avisa.
// #ifdef HAY_BH1750
// BH1750 bh_ventana;
// BH1750 bh_aula;
// bool bh_ventana_ok = false;
// bool bh_aula_ok = false;
// #endif

// ETAPA 2 (relé): escribe un canal. Lógica inversa si RELE_ACTIVO_BAJO.
// void escribir_rele(int canal, bool encendido) {
//   int pin = canal == 0 ? RELE_PIN_1 : RELE_PIN_2;
//   if (pin < 0) return;
//   digitalWrite(pin, (encendido != RELE_ACTIVO_BAJO) ? HIGH : LOW);
// }

// Enciende o apaga la luz del aula (canal 1). Sin relé solo registra el
// estado y lo muestra por el monitor serie: sirve para ver la regla en vivo.
void aplicar_luz(bool encender) {
  if (encender == luz_encendida) return;
  luz_encendida = encender;
  // ETAPA 2 (relé): activar con los componentes.
  // rele_estado[0] = encender;
  // escribir_rele(0, encender);
  Serial.printf("[LUZ] %s\n", encender ? "PRENDIDA" : "APAGADA");
  // if (RELE_PIN_1 >= 0) publicar_ya = true;
}

// Enciende o apaga el LED de presencia según la última distancia reportada.
// Es distinto de `presencia`: este solo mira la ventana (con su histéresis),
// mientras la regla de la luz pide además movimiento confirmado.
void led_presencia(bool prendir) {
  bool nivel = LED_PRESENCIA_INVERTIDO ? !prendir : prendir;
  digitalWrite(LED_PRESENCIA_PIN, nivel ? HIGH : LOW);
}

// ETAPA 2 (BH1750): lee los dos sensores. readLightLevel() devuelve < 0 si el
// sensor no contesta (o nunca arrancó): en ese caso el lux de la regla queda
// en 0 y el campo no se publica, con su alerta "sensor" (contrato §3.5).
// #ifdef HAY_BH1750
// void leer_luz() {
//   float v = bh_ventana.readLightLevel();
//   bh_ventana_ok = v >= 0.0f;
//   luz_ventana_lx = bh_ventana_ok ? v : 0.0f;
//   float a = bh_aula.readLightLevel();
//   bh_aula_ok = a >= 0.0f;
//   luz_aula_lx = bh_aula_ok ? a : 0.0f;
// }
// #endif

// Valida TODOS los parámetros antes de aplicar alguno: si algo no valida, no
// se toca nada y motivo explica por qué (contrato §3.3).
bool aplicar_config(JsonObjectConst params, String& motivo) {
  if (params.isNull() || params.size() == 0) { motivo = "sin_parametros"; return false; }

  uint32_t nuevo_intervalo = intervalo_s;
  uint32_t nuevo_umbral_v = umbral_ventana_lx;
  uint32_t nuevo_umbral_a = umbral_aula_lx;
  uint32_t nuevo_delay = delay_off_s;
  bool nuevo_auto = modo_auto;

  for (JsonPairConst p : params) {
    const char* k = p.key().c_str();
    JsonVariantConst v = p.value();
    if (strcmp(k, "intervalo_s") == 0) {
      if (!v.is<uint32_t>()) { motivo = "intervalo_s_no_es_entero"; return false; }
      nuevo_intervalo = v.as<uint32_t>();
      if (nuevo_intervalo < INTERVALO_MIN_S || nuevo_intervalo > INTERVALO_MAX_S) {
        motivo = "intervalo_s_fuera_de_rango"; return false;
      }
    } else if (strcmp(k, "umbral_ventana_lx") == 0) {
      if (!v.is<uint32_t>()) { motivo = "umbral_ventana_lx_no_es_entero"; return false; }
      nuevo_umbral_v = v.as<uint32_t>();
      if (nuevo_umbral_v < UMBRAL_MIN_LX || nuevo_umbral_v > UMBRAL_MAX_LX) {
        motivo = "umbral_ventana_lx_fuera_de_rango"; return false;
      }
    } else if (strcmp(k, "umbral_aula_lx") == 0) {
      if (!v.is<uint32_t>()) { motivo = "umbral_aula_lx_no_es_entero"; return false; }
      nuevo_umbral_a = v.as<uint32_t>();
      if (nuevo_umbral_a < UMBRAL_MIN_LX || nuevo_umbral_a > UMBRAL_MAX_LX) {
        motivo = "umbral_aula_lx_fuera_de_rango"; return false;
      }
    } else if (strcmp(k, "delay_off_s") == 0) {
      if (!v.is<uint32_t>()) { motivo = "delay_off_s_no_es_entero"; return false; }
      nuevo_delay = v.as<uint32_t>();
      if (nuevo_delay > DELAY_OFF_MAX_S) { motivo = "delay_off_s_fuera_de_rango"; return false; }
    } else if (strcmp(k, "modo") == 0) {
      if (!v.is<const char*>()) { motivo = "modo_no_es_texto"; return false; }
      const char* m = v.as<const char*>();
      if (strcmp(m, "auto") == 0) nuevo_auto = true;
      else if (strcmp(m, "manual") == 0) nuevo_auto = false;
      else { motivo = "modo_invalido"; return false; }
    } else {
      motivo = String("parametro_desconocido:") + k;
      return false;
    }
  }

  intervalo_s = nuevo_intervalo;
  umbral_ventana_lx = nuevo_umbral_v;
  umbral_aula_lx = nuevo_umbral_a;
  delay_off_s = nuevo_delay;
  modo_auto = nuevo_auto;
  prefs.putUInt("intervalo_s", intervalo_s);
  prefs.putUInt("umbral_vent_v", umbral_ventana_lx);
  prefs.putUInt("umbral_aula", umbral_aula_lx);
  prefs.putUInt("delay_off", delay_off_s);
  prefs.putUChar("modo_auto", modo_auto ? 1 : 0);
  Serial.printf("config aplicada: intervalo_s=%lu umbral_ventana_lx=%lu umbral_aula_lx=%lu "
                "delay_off_s=%lu modo=%s\n",
                (unsigned long)intervalo_s, (unsigned long)umbral_ventana_lx,
                (unsigned long)umbral_aula_lx, (unsigned long)delay_off_s,
                modo_auto ? "auto" : "manual");
  return true;
}

void describir_config(JsonObject out) {
  out["intervalo_s"] = intervalo_s;
  out["umbral_ventana_lx"] = umbral_ventana_lx;
  out["umbral_aula_lx"] = umbral_aula_lx;
  out["delay_off_s"] = delay_off_s;
  out["modo"] = modo_auto ? "auto" : "manual";
}

// ETAPA 2 (relé): comando propio de AURA, además de set_config. La regla local
// no se discute con AURA: un "rele" acciona y deja el dispositivo en modo
// manual, y solo set_config {"modo":"auto"} devuelve el control a la regla.
// Sin relé conectado está comentado: el nodo responde "comando_desconocido".
// bool ejecutar_comando(const char* comando, JsonObjectConst params, String& motivo) {
//   if (strcmp(comando, "rele") != 0) { motivo = "comando_desconocido"; return false; }
//
//   JsonVariantConst v_canal = params["canal"];
//   if (!v_canal.is<uint32_t>()) { motivo = "canal_no_es_entero"; return false; }
//   uint32_t canal = v_canal.as<uint32_t>();
//   if (canal < 1 || canal > 2) { motivo = "canal_fuera_de_rango"; return false; }
//   JsonVariantConst v_on = params["encendido"];
//   if (!v_on.is<bool>()) { motivo = "encendido_no_es_booleano"; return false; }
//   bool encendido = v_on.as<bool>();
//
//   int pin = canal == 1 ? RELE_PIN_1 : RELE_PIN_2;
//   if (pin < 0) { motivo = "rele_no_configurado"; return false; }
//
//   rele_estado[canal - 1] = encendido;
//   escribir_rele(canal - 1, encendido);
//   if (canal == 1) luz_encendida = encendido;
//   modo_auto = false;   // el mando manual pausa la regla
//   prefs.putUChar("modo_auto", 0);
//   publicar_ya = true;
//   Serial.printf("[RELE] canal %lu %s, modo manual\n", (unsigned long)canal,
//                 encendido ? "encendido" : "apagado");
//   return true;
// }

// Distancia aproximada de un gate, con la misma fórmula que usa la librería
// (cm = 35 + (gate - 1) * 70; gate 0 = 0). Sirve para leer el rango que el
// sensor tiene grabado en su flash.
uint16_t gate_a_cm(uint8_t gate) {
  return gate == 0 ? 0 : (uint16_t)(35 + (uint16_t)(gate - 1) * 70);
}

// ===== Programa =====

// Diagnóstico del radar: el "error 4" (timeout) no dice la causa, esto la
// separa. Corre los primeros 20 s (o hasta que llegue un reporte válido):
//   validos > 0                 → el sensor habla: cableado y baudios bien
//   validos 0 y rechazados > 0  → llegan datos rotos: revisar cableado
//   validos 0 y rechazados 0    → no llega nada: revisar que el TX del
//                                 módulo (OT1, según el datasheet oficial) esté
//                                 en GPIO18 y el RX en GPIO17, la alimentación
//                                 3V3 (3.0..3.6 V), y en último caso probar
//                                 LD2420_BAUD 256000 (firmware <= 1.5.2)
bool diag_pendiente = true;
unsigned long diag_ultimo_ms = 0;

void diagnosticar_radar() {
  if (!diag_pendiente || millis() - diag_ultimo_ms < 2000) return;
  diag_ultimo_ms = millis();

  const LD2420MotionState& s = radar.state();
  if (s.validFrames > 0) {
    Serial.printf("[RADAR] diag OK: %lu reportes validos, distancia %u cm "
                  "(el sensor responde)\n",
                  (unsigned long)s.validFrames, (unsigned)s.sensorDistanceCm);
    diag_pendiente = false;
    return;
  }
  if (millis() >= 20000) {
    if (s.rejectedFrames > 0) {
      Serial.printf("[RADAR] diag: llegaron %lu bytes que no se entienden "
                    "-> revisar cableado (OT1->GPIO18, RX->GPIO17, GND comun)\n",
                    (unsigned long)s.rejectedFrames);
    } else {
      Serial.println("[RADAR] diag: no llego nada del sensor -> 1) el TX del "
                     "modulo es OT1 (no OT2), 2) alimentar con 3V3 (3.0..3.6 V), "
                     "3) si esta todo bien, probar LD2420_BAUD 256000");
    }
    diag_pendiente = false;
    return;
  }
  Serial.printf("[RADAR] diag: esperando datos del sensor (%lu ms)...\n",
                (unsigned long)millis());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  prefs.begin("luz01", false);
  intervalo_s = prefs.getUInt("intervalo_s", intervalo_s);
  umbral_ventana_lx = prefs.getUInt("umbral_vent_v", umbral_ventana_lx);
  umbral_aula_lx = prefs.getUInt("umbral_aula", umbral_aula_lx);
  delay_off_s = prefs.getUInt("delay_off", delay_off_s);
  modo_auto = prefs.getUChar("modo_auto", modo_auto ? 1 : 0) != 0;
  if (intervalo_s < INTERVALO_MIN_S || intervalo_s > INTERVALO_MAX_S) intervalo_s = 60;
  if (umbral_ventana_lx < UMBRAL_MIN_LX || umbral_ventana_lx > UMBRAL_MAX_LX) umbral_ventana_lx = 300;
  if (umbral_aula_lx < UMBRAL_MIN_LX || umbral_aula_lx > UMBRAL_MAX_LX) umbral_aula_lx = 300;
  if (delay_off_s > DELAY_OFF_MAX_S) delay_off_s = 0;

  // ETAPA 2 (relé): inicializar los canales al arrancar.
  // if (RELE_PIN_1 >= 0) { pinMode(RELE_PIN_1, OUTPUT); escribir_rele(0, false); }
  // if (RELE_PIN_2 >= 0) { pinMode(RELE_PIN_2, OUTPUT); escribir_rele(1, false); }

  // LED de presencia: arranca apagado (y si el radar no contesta, se queda
  // apagado porque en el loop manda `!stale`).
  pinMode(LED_PRESENCIA_PIN, OUTPUT);
  led_presencia(false);

  // LD2420: UART1 con LD2420_BAUD (3.3 V lógicos). Sin sensor conectado, la
  // sonda queda en falla y no se publica presencia: nunca manda un valor
  // inventado.
  radar_serial.begin(LD2420_BAUD, SERIAL_8N1, LD2420_RX, LD2420_TX);
  if (!radar.begin(radar_serial)) {
    Serial.printf("LD2420: no arranco (error %u): revisar cableado TX/RX (el TX "
                  "del modulo esta en OT1 u OT2) o baudios (%u)\n",
                  (unsigned)radar.lastError(), (unsigned)LD2420_BAUD);
  }

  // Config grabada en la flash del sensor (solo lectura, no cambia nada):
  // gates min..max = rango en el que el radar reporta, y retardo de salida.
  LD2420DeviceConfig cfg = {};
  if (radar.readDeviceConfig(cfg)) {
    Serial.printf("LD2420 config: gates %u..%u (~%u..%u cm), retardo %lu s\n",
                  cfg.minGate, cfg.maxGate, gate_a_cm(cfg.minGate),
                  gate_a_cm(cfg.maxGate),
                  (unsigned long)cfg.disappearanceDelaySeconds);
  } else {
    Serial.printf("LD2420: no se pudo leer la config del sensor (error %u)\n",
                  (unsigned)radar.lastError());
  }

  // ETAPA 2 (BH1750): arrancar I2C y los dos sensores.
  // #ifdef HAY_BH1750
  //   Wire.begin(I2C_SDA, I2C_SCL);
  //   bh_ventana_ok = bh_ventana.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
  //   bh_aula_ok = bh_aula.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &Wire);
  //   Serial.printf("BH1750 ventana(0x23) %s, aula(0x5C) %s\n",
  //                 bh_ventana_ok ? "ok" : "SIN RESPUESTA",
  //                 bh_aula_ok ? "ok" : "SIN RESPUESTA");
  // #endif

  // ETAPA 2 (relé): poner "ejecutar_comando" en vez de NULL al descomentar.
  NodoMeshCallbacks cb = {aplicar_config, describir_config, NULL, "red"};
  nodo_mesh_iniciar(cb);
  Serial.printf("E1-PB-LECA-LUZ01 listo: intervalo %lu s, modo %s, ventana %u..%u cm\n",
                (unsigned long)intervalo_s, modo_auto ? "auto" : "manual",
                (unsigned)DIST_MIN_CM, (unsigned)DIST_MAX_CM);
}

void loop() {
  radar.update();
  diagnosticar_radar();
  // ETAPA 2 (BH1750): leer los lux.
  // leer_luz();

  bool stale = radar.isStale();   // sin reportes del radar: la sonda está en falla
  uint16_t distancia_cm = radar.sensorDistanceCm();
  bool movimiento = radar.motionDetected();

  // Ventana con histéresis: entro en DIST_MIN..DIST_MAX, salgo recién cuando
  // la distancia se pasa de DIST_MIN-HIST o DIST_MAX+HIST. Evita que la
  // presencia parpadee con la precisión ±0.35 m del radar en el borde.
  static bool en_ventana = false;
  int d = (int)distancia_cm;
  if (d >= (int)DIST_MIN_CM && d <= (int)DIST_MAX_CM) {
    en_ventana = true;
  } else if (d < (int)DIST_MIN_CM - (int)DIST_HIST_CM ||
             d > (int)DIST_MAX_CM + (int)DIST_HIST_CM) {
    en_ventana = false;
  }

  bool presencia = !stale && movimiento && en_ventana;
  if (presencia) ultimo_presencia_ms = millis();

  // LED: prende mientras la última distancia reportada esté dentro de la
  // ventana (entra en 60..140, sale en 40..160) y el radar conteste.
  led_presencia(!stale && en_ventana);

  // Log del radar: solo cuando cambia el movimiento o el cruce de la ventana.
  // Sirve para ver en el monitor serie a qué distancia mide y por qué la
  // presencia cuenta (o no) en cada caso.
  static bool movimiento_anterior = false;
  static bool ventana_anterior = false;
  if (!stale && (movimiento != movimiento_anterior || en_ventana != ventana_anterior)) {
    movimiento_anterior = movimiento;
    ventana_anterior = en_ventana;
    Serial.printf("[RADAR] %s, distancia %u cm (entra %u..%u, sale %u..%u) -> %s\n",
                  movimiento ? "movimiento" : "sin movimiento",
                  (unsigned)distancia_cm, (unsigned)DIST_MIN_CM,
                  (unsigned)DIST_MAX_CM,
                  (unsigned)(DIST_MIN_CM - DIST_HIST_CM),
                  (unsigned)(DIST_MAX_CM + DIST_HIST_CM),
                  presencia ? "PRESENCIA" : "no cuenta");
  }

  if (!stale) {
    // Presencia efectiva: el retardo de apagado sostiene la luz un rato
    // después de que el radar deja de ver a nadie.
    bool efectiva = presencia;
    if (!efectiva && luz_encendida && delay_off_s > 0 &&
        millis() - ultimo_presencia_ms < delay_off_s * 1000UL) {
      efectiva = true;
    }

    if (modo_auto) {
      ReglaLuz::Parametros p = {(float)umbral_ventana_lx, (float)umbral_aula_lx};
      ReglaLuz::Entrada e = {efectiva, luz_ventana_lx, luz_aula_lx};
      aplicar_luz(ReglaLuz::decidir(e, p, luz_encendida));
    }
  }

  // Estado de las sondas en cada vuelta: la alerta "sensor" va solo cuando
  // cambia (falla o recuperación), nunca más de una vez por cruce.
  nodo_mesh_sonda(CAMPO_PRESENCIA, !stale, "sin_respuesta");
  // ETAPA 2 (BH1750): una sonda por sensor.
  // nodo_mesh_sonda("luz_ventana_lx", bh_ventana_ok, "sin_respuesta");
  // nodo_mesh_sonda("luz_aula_lx", bh_aula_ok, "sin_respuesta");

  // Publicación: cada intervalo_s y en cada cambio de presencia (para que la
  // alerta de cruce del conector llegue sin esperar al próximo intervalo).
  if (!stale && presencia != presencia_publicada) publicar_ya = true;
  bool vencio = primera || millis() - ultima_muestra >= intervalo_s * 1000UL;
  if (vencio || publicar_ya) {
    if (!stale) {   // radar en falla: la sonda ya avisó, no se manda valor inventado
      JsonDocument values;
      values[CAMPO_PRESENCIA] = presencia ? 1 : 0;
      // ETAPA 2 (BH1750): sin sensor, sin campo (nada de centinelas).
      // if (bh_ventana_ok) values["luz_ventana_lx"] = luz_ventana_lx;
      // if (bh_aula_ok) values["luz_aula_lx"] = luz_aula_lx;
      // ETAPA 2 (relé): informar el estado recién con el relé cableado.
      // if (RELE_PIN_1 >= 0) {
      //   values["rele_canal1"] = rele_estado[0] ? 1 : 0;
      //   values["rele_canal2"] = rele_estado[1] ? 1 : 0;
      // }
      nodo_mesh_medicion(values.as<JsonObjectConst>());
      presencia_publicada = presencia;
    }
    publicar_ya = false;
    ultima_muestra = millis();
    primera = false;
  }

  nodo_mesh_loop();
  delay(20);
}
