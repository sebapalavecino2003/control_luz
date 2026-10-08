# E1-PB-LECA-LUZ01 — control de iluminación del aula del LabECA

Mide la presencia con un radar y enciende o apaga la luz del aula **localmente**: la regla
vive en el firmware, AURA no decide en cada ciclo. Si la luz del aula (y la que entra por la
ventana) ya alcanzan, no enciende.

| | |
|---|---|
| **Ubicación** | Edificio 1, planta baja, LabECA |
| **Responsable** | Grupo de IC IV 2026 |
| **Estado** | **pruebas solo con el LD2420**; el código de BH1750 y relé está comentado (`ETAPA 2`), falta cablear los componentes |
| **Instalado** | todavía no |
| **Transporte** | mesh ESP-WIFI-MESH, según [ADR-006](../../docs/adr/ADR-006-esp-wifi-mesh-reemplaza-a-esp-now-en-interior.md) (nodo hoja) |
| **Contrato que implementa** | [`CONTRATO_MQTT.md`](../../docs/CONTRATO_MQTT.md) v4.0 |
| **Basado en** | [`../../ejemplos/plantilla_dispositivo/`](../../ejemplos/plantilla_dispositivo/) |
| **Códigos anteriores** | ninguno |

**Identidad**: por MAC de la placa (`hw_id` `mac-…`), como todo dispositivo del repo
([ADR-005](../../docs/adr/ADR-005-identidad-por-placa-y-mapeo-en-aura.md)).

## Hardware

| Parte | Qué hace | Estado |
|---|---|---|
| ESP32-S3 dev kit (WAVESHARE, ESP32-S3-WROOM-1) | la placa | puesto. IDE: placa **ESP32S3 Dev Module**, USB CDC On Boot: Enabled |
| LD2420 | radar mmWave de presencia, UART1 | **puesto**: cables y pines en `config_local.h`. J2 (5 pines): `3V3 | GND | OT1=TX | RX | OT2=IO presencia` — el **TX es OT1** (en firmware ≤ 1.5.2 está invertido: TX en OT2), 115200 baud. Alimentación **3V3** (3.0..3.6 V, ~50 mA), nunca 5 V |
| BH1750 ×2 | lux en la ventana (0x23) y en el centro del aula (0x5C), por I2C (8/9) | **falta cablear**: código en el `.ino` comentado con `ETAPA 2`, se descomenta y se define `HAY_BH1750` |
| Relé doble canal | canal 1 = luz del aula, canal 2 = reservado | **falta cablear**: código comentado con `ETAPA 2`, se descomenta y se definen `RELE_PIN_1`/`RELE_PIN_2` |
| LED de presencia | **GPIO 14** + resistencia 220–330 Ω + LED + GND: prende mientras el radar mide a alguien dentro de la ventana | **para cablear**: pin configurable con `LED_PRESENCIA_PIN` (no usar 0/3/45/46, 19/20, 17/18, 8/9, 15/16) |

**Datos del datasheet** (`datasheet/hlk-ld2420.pdf`): alcance 8 m en pared / 5 m en techo,
precisión de distancia **±0.35 m**, puertas de **70 cm** (0..15), refresh 10 Hz, ángulo ±60°,
mínimo 0.2 m sin zona ciega, y **solo informa distancia de cuerpos en movimiento** (sin
movimiento queda el último valor). Montaje: techo (recomendado, 2.7–3 m, cono de 5 m de radio)
o pared (1.5–2 m, ±45°). En el ESP32-S3 los pines 17/18 son la UART1 nativa
(`IO17=U1TXD`, `IO18=U1RXD`) y no son strapping.

**Ventana de distancia**: la presencia solo cuenta si el radar la mide entre **`DIST_MIN_CM`
y `DIST_MAX_CM`** (60..140 cm, o sea ~1 m), y se mantiene hasta **40..160 cm** (histéresis
`DIST_HIST_CM`, contra la precisión ±0.35 m: un target en el borde no la hace parpadear).
Constantes en el `.ino`, no son `set_config`. El LD2420 sigue detectando más lejos, nosotros lo
ignoramos. Al arrancar, el monitor serie imprime la config grabada del sensor (`gates … ~… cm`)
y cada cambio imprime
`[RADAR] movimiento, distancia 103 cm (entra 60..140, sale 40..160) -> PRESENCIA`.

## La regla local (`regla_luz.h`)

```
sin presencia                        → luz OFF
con presencia y aporte < 0.95        → luz ON
con presencia y aporte >= 1.0        → luz OFF
entre 0.95 y 1.0                     → no se cambia (histéresis, anti-parpadeo)

aporte = luz_ventana_lx / umbral_ventana_lx + luz_aula_lx / umbral_aula_lx
```

Por ahora (solo LD2420) ambos lux valen 0, el aporte es 0 y la presencia manda sola: la
regla decide encender/apagar y lo muestra por el monitor serie (`[LUZ] PRENDIDA/APAGADA`),
que es justamente lo que se quiere ver en las pruebas. El `delay_off_s` sostiene la luz ese
rato después de que el radar deja de ver a nadie. Si el radar no responde, **no se publica
`presencia`** (nada de valores inventados) y va la alerta `sensor`. Cuando se descomente la
etapa 2, si un sensor de luz falla la regla lo toma como 0 lx (con presencia, prende) pero
**no se publica ese campo**: su sonda avisa.

## Lo que envía a AURA

| Campo | Unidad | Rango | Cada cuánto |
|---|---|---|---|
| `presencia` | 0/1 | 0..1 | `intervalo_s` y en cada cambio (para que la alerta de cruce llegue al instante) |
| `luz_ventana_lx` | lx | 0..65535 | **ETAPA 2**: solo con el BH1750 de la ventana respondiendo |
| `luz_aula_lx` | lx | 0..65535 | **ETAPA 2**: solo con el BH1750 del aula respondiendo |
| `rele_canal1` | 0/1 | 0..1 | **ETAPA 2**: solo con el relé cableado |
| `rele_canal2` | 0/1 | 0..1 | **ETAPA 2**: solo con el relé cableado |

Hoy solo sale `presencia`. **Alertas**: `sensor` cuando falla o se recupera un campo con
sonda (motivo `sin_respuesta`). Sonda en falla ⇒ el campo desaparece de `values`, nunca manda
un centinela.

## Cómo se configura (`set_config`)

Todo o nada: si un parámetro no valida, no se aplica ninguno y el resultado trae el motivo.
Vigente tras reinicios (flash).

| Parámetro | Default | Rango | Qué hace |
|---|---|---|---|
| `intervalo_s` | 60 | 5..86400 | cada cuánto manda medición y chequea la sonda |
| `umbral_ventana_lx` | 300 | 1..20000 | cuánta luz en la ventana alcanza para no encender (TODO: calibrar) |
| `umbral_aula_lx` | 300 | 1..20000 | ídem para el centro del aula (TODO: calibrar) |
| `delay_off_s` | 0 | 0..3600 | retardo de apagado al perder la presencia |
| `modo` | `auto` | `auto`/`manual` | `auto` = manda la regla; `manual` = manda AURA |

## Comandos

```json
{"command":"rele","params":{"canal":1,"encendido":true}}
```

Acciona el relé (canal 1 o 2) y **deja el dispositivo en modo `manual`**: la regla local queda
pausada hasta que AURA mande `set_config {"modo":"auto"}`.

**ETAPA 2**: la función `ejecutar_comando` está comentada en el `.ino` hasta que llegue el
relé, así que hoy el nodo responde `comando_desconocido`. Al descomentarla hay que poner
`ejecutar_comando` en el `NodoMeshCallbacks` de `setup()` (ahí hay un `NULL` con la nota).

## Compilar y probar

```bash
# tests de la regla (lógica pura, se corren en la PC)
make -C tests test
```

En Arduino IDE: placa **ESP32S3 Dev Module**, *USB CDC On Boot: Enabled*, bibliotecas de
`bibliotecas.txt`, y copiar `config_local.h.example` → `config_local.h` con los datos de la
mesh que da la cátedra. Sin `config_local.h` compila igual, pero no envía (guarda en la cola).

Para darlo de alta en AURA hay que agregar la fila a la tabla *Dispositivos* de
[`../../dispositivos/README.md`](../../dispositivos/README.md) y cargar la placa con
`herramientas/leer_mac`; los dos pasos van juntos en el mismo PR.
