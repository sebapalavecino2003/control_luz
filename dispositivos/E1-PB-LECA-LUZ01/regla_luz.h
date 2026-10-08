#pragma once
// Regla local de iluminación del aula — E1-PB-LECA-LUZ01.
//
// Lógica pura, sin Arduino ni estado: la decide el firmware y se testea en host
// (tests/test_regla.cpp). La toma cada vuelta del loop, en modo "auto".
//
//   SI presencia = NO  → LUZ = OFF
//   SI presencia = SI:
//       aporte = luz_ventana_lx / umbral_ventana_lx + luz_aula_lx / umbral_aula_lx
//       aporte <  0.95                 → ON   (no alcanza la luz)
//       aporte >= 1.0                  → OFF  (alcanza)
//       0.95 <= aporte < 1.0           → se mantiene el estado (histéresis)
//
// Cada sensor aporta normalizado con su propio umbral: la ventana con sol
// directo no infla la cuenta. Sin BH1750 conectado (o si uno falla) ese lux
// llega en 0, el aporte baja y la luz la decide solo la presencia.

namespace ReglaLuz {

// Anti-parpadeo: para encender hace falta un poco menos de oscuridad que para
// seguir encendida. Con aporte entre 0.95 y 1.0 no se cambia de estado.
constexpr float APORTE_PARA_ENCENDER = 0.95f;
constexpr float APORTE_PARA_APAGAR = 1.0f;

struct Entrada {
  bool presencia;        // presencia efectiva: la real, ya con delay_off aplicado
  float luz_ventana_lx;  // 0 si no hay BH1750 conectado
  float luz_aula_lx;     // 0 si no hay BH1750 conectado
};

struct Parametros {
  float umbral_ventana_lx;  // >= 1 (el firmware valida el rango antes de aplicarlo)
  float umbral_aula_lx;     // >= 1
};

// Cuánta luz disponible hay, en "veces el umbral": 1.0 = alcanza.
inline float aporte(const Entrada& e, const Parametros& p) {
  return e.luz_ventana_lx / p.umbral_ventana_lx + e.luz_aula_lx / p.umbral_aula_lx;
}

// true = la luz artificial debe estar encendida. `encendida_ahora` es el estado
// actual, con el que se aplica la histéresis.
inline bool decidir(const Entrada& e, const Parametros& p, bool encendida_ahora) {
  if (!e.presencia) return false;          // sin presencia no se enciende nunca
  float a = aporte(e, p);
  if (encendida_ahora) return a < APORTE_PARA_APAGAR;
  return a < APORTE_PARA_ENCENDER;
}

}  // namespace ReglaLuz
