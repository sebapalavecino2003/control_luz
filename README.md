# mi_proyecto_control_luz — E1-PB-LECA-LUZ01

Proyecto de control de iluminación del aula del **LabECA** (Edificio 1, planta baja):
presencia por radar y decisión local de encender/apagar la luz.

Es un trabajo de desarrollo que **después se copia a un fork de [`../aura-firmware`](../aura-firmware)**
y se manda como PR (ver checklist abajo). El repo `aura-firmware` es el autoritativo: si algo
difere, manda ese.

| Qué | Dónde |
|---|---|
| Conector de AURA (alerta de presencia) | [`conectores/E1-PB-LECA-LUZ01/`](conectores/E1-PB-LECA-LUZ01/) |
| Firmware del dispositivo (regla local de la luz) | [`dispositivos/E1-PB-LECA-LUZ01/`](dispositivos/E1-PB-LECA-LUZ01/) |
| SDK y headers de la mesh | [`comun/`](comun/) (espejo de `aura-firmware/comun`; si cambia el original, se vuelve a copiar) |

Contrato: [`aura-firmware/docs/CONTRATO_MQTT.md`](../aura-firmware/docs/CONTRATO_MQTT.md) v4.0.
Transporte: mesh ESP-WIFI-MESH, el nodo es **hoja** ([ADR-006](../aura-firmware/docs/adr/ADR-006-esp-wifi-mesh-reemplaza-a-esp-now-en-interior.md)),
identidad por MAC de la placa ([ADR-005](../aura-firmware/docs/adr/ADR-005-identidad-por-placa-y-mapeo-en-aura.md)).

## Cómo correr todo

```bash
# 1. tests del conector (Python, sin dependencias)
python3 -m unittest discover -s conectores/E1-PB-LECA-LUZ01/tests

# 2. tests del SDK y de los headers de la mesh (C++, en la PC)
make -C comun/tests test

# 3. tests de la regla local de la luz (lógica pura, en la PC)
make -C dispositivos/E1-PB-LECA-LUZ01/tests test

# 4. validadores del repo autoritativo, corriendo contra este árbol
python3 herramientas/validar_conectores.py
python3 herramientas/validar_dispositivos.py
```

`herramientas/`, `dispositivos/README.md` (el mapa de códigos) y `dispositivos/E1-PB-LECA-HFR01/`
son copias del repo autoritativo: están acá solo para poder validar todo localmente, y **no se
copian al fork** (esos archivos ya viven ahí).

## Compilar el firmware

Arduino IDE (o el `arduino-cli` que trae la IDE):

- Placa: **ESP32S3 Dev Module** (`esp32:esp32:esp32s3`), *USB CDC On Boot: Enabled*
- Bibliotecas: las de `dispositivos/E1-PB-LECA-LUZ01/bibliotecas.txt`
- Copiar `config_local.h.example` → `config_local.h` con los datos de la mesh (los da la
  cátedra). **`config_local.h` nunca se commitea.**
- Abrir `dispositivos/E1-PB-LECA-LUZ01/E1-PB-LECA-LUZ01.ino` y subir

Con línea de comandos:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 dispositivos/E1-PB-LECA-LUZ01
```

## Checklist del PR a aura-firmware

- [ ] Copiar **solo** `conectores/E1-PB-LECA-LUZ01/` y `dispositivos/E1-PB-LECA-LUZ01/` al fork
      (sin `config_local.h`, sin binarios de tests, y sin `dispositivos/README.md` ni
      `E1-PB-LECA-HFR01/`: ya están en el fork)
- [ ] Agregar la fila del dispositivo a la tabla *Dispositivos* de
      `dispositivos/README.md` del fork (ya está redactada en nuestro espejo; el código
      `E1-PB-LECA-LUZ01` usa abreviaturas ya existentes: `E1`, `PB`, `LECA`, `LUZ`)
- [ ] Si el grupo tiene reviewers: agregar su usuario a `.github/CODEOWNERS`
- [ ] Correr `herramientas/generar_autocontenidos.sh` del fork
- [ ] Alta de la placa con `herramientas/leer_mac` → la cátedra carga el dispositivo en AURA
      (`hw_id` = `mac-` + MAC en minúsculas)
- [ ] CI verde: validadores (`validar_conectores.py`, `validar_dispositivos.py`) y
      compilación de todos los sketches
- [ ] Confirmar con la cátedra el uso del devkit ESP32-S3 WAVESHARE (ADR-006 prevé XIAO ESP32S3)

## Pendientes

- [ ] **Pruebas con el LD2420** (lo único conectado): subir el firmware, ver las alertas de
      presencia en AURA y la regla por el monitor serie (`[LUZ] PRENDIDA/APAGADA`)
- [x] Código de BH1750 y relé escrito — está **comentado** en el `.ino` (bloques `ETAPA 2`):
      nada se borró, `grep -n "ETAPA 2" dispositivos/E1-PB-LECA-LUZ01/E1-PB-LECA-LUZ01.ino`
      los lista todos
- [ ] Cuando lleguen los componentes: descomentar los bloques `ETAPA 2`, cablear y definir en
      `config_local.h` (`HAY_BH1750`, `I2C_SDA`/`I2C_SCL`, `RELE_PIN_1`/`RELE_PIN_2`), y poner
      `ejecutar_comando` en el `NodoMeshCallbacks` de `setup()` (ahí hay un `NULL` marcado)
- [ ] Calibrar `umbral_ventana_lx` y `umbral_aula_lx` con mediciones reales
- [ ] El conector ya declara `luz_ventana_lx`, `luz_aula_lx`, `rele_canal1`, `rele_canal2`:
      no hace falta tocarlo, simplemente esos campos no llegan hasta que el firmware los mande
