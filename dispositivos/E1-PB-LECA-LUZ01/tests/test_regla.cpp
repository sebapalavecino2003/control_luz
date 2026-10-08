// Pruebas de la regla local de iluminación (regla_luz.h): lógica pura, sin
// placa. La misma que corre el firmware, corriendo en la PC.

#include "../../../comun/tests/aserciones.h"
#include "../regla_luz.h"

using ReglaLuz::aporte;
using ReglaLuz::decidir;
using ReglaLuz::Entrada;
using ReglaLuz::Parametros;

static const Parametros UMBRALES = {300.0f, 300.0f};  // defaults del firmware

int main() {
  // === Sin presencia: nunca enciende, aunque esté oscuro ===
  VERIFICAR(!decidir({false, 0.0f, 0.0f}, UMBRALES, false));
  VERIFICAR(!decidir({false, 0.0f, 0.0f}, UMBRALES, true));   // se apaga igual
  VERIFICAR(!decidir({false, 10000.0f, 10000.0f}, UMBRALES, true));

  // === Etapa 1: sin BH1750, ambos lux en 0 ⇒ solo manda la presencia ===
  VERIFICAR(decidir({true, 0.0f, 0.0f}, UMBRALES, false));
  VERIFICAR(decidir({true, 0.0f, 0.0f}, UMBRALES, true));

  // === Con luz que alcanza (aporte >= 1) no enciende ===
  VERIFICAR(!decidir({true, 300.0f, 0.0f}, UMBRALES, false));     // ventana justa
  VERIFICAR(!decidir({true, 0.0f, 300.0f}, UMBRALES, false));     // aula justa
  VERIFICAR(!decidir({true, 400.0f, 400.0f}, UMBRALES, false));   // sobra luz
  VERIFICAR(!decidir({true, 300.0f, 0.0f}, UMBRALES, true));      // la apaga si estaba

  // === Con luz insuficiente enciende (aporte < 0.95) ===
  VERIFICAR(decidir({true, 150.0f, 100.0f}, UMBRALES, false));    // 0.5 + 0.33
  VERIFICAR(decidir({true, 0.0f, 280.0f}, UMBRALES, false));      // 0.93

  // === Los dos sensores suman (cada uno con su umbral) ===
  VERIFICAR(!decidir({true, 200.0f, 100.0f}, UMBRALES, false));   // 0.67 + 0.33 = 1.0
  VERIFICAR(!decidir({true, 200.0f, 90.0f}, UMBRALES, false));    // 0.67 + 0.30 = 0.97

  // === Histéresis: entre 0.95 y 1.0 no se cambia de estado ===
  VERIFICAR(decidir({true, 200.0f, 90.0f}, UMBRALES, true));      // encendida y 0.97 ⇒ sigue ON
  VERIFICAR(!decidir({true, 285.0f, 0.0f}, UMBRALES, false));     // exactamente 0.95 ⇒ OFF
  VERIFICAR(decidir({true, 285.0f, 0.0f}, UMBRALES, true));       // 0.95 ⇒ sigue ON

  // === Cada umbral normaliza su propio sensor ===
  Parametros umbrales_dobles = {600.0f, 600.0f};
  VERIFICAR(!decidir({true, 300.0f, 300.0f}, umbrales_dobles, false));  // 0.5 + 0.5 = 1.0
  Parametros umbrales_chicos = {1000.0f, 1000.0f};
  VERIFICAR(decidir({true, 300.0f, 300.0f}, umbrales_chicos, false));   // 0.3 + 0.3

  // === Umbral mínimo 1 lx: no divide por cero ===
  Parametros umbral_min = {1.0f, 1.0f};
  VERIFICAR(decidir({true, 0.0f, 0.0f}, umbral_min, false));
  VERIFICAR(!decidir({true, 1.0f, 0.0f}, umbral_min, false));           // aporte 1.0
  VERIFICAR(aporte({true, 0.5f, 0.0f}, umbral_min) > 0.4f);

  // === aporte() es la suma normalizada ===
  VERIFICAR(aporte({true, 150.0f, 150.0f}, UMBRALES) > 0.99f &&
            aporte({true, 150.0f, 150.0f}, UMBRALES) < 1.01f);
  VERIFICAR(aporte({true, 300.0f, 300.0f}, UMBRALES) > 1.99f);

  // === La presencia efectiva (con delay_off) es un asunto del .ino: la regla
  //     solo recibe el booleano, y sin presencia nunca enciende ===
  VERIFICAR(!decidir({false, 0.0f, 0.0f}, UMBRALES, false));

  RESUMEN();
}
