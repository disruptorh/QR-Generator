# Generador de códigos QR (C++)

Generador y verificador de códigos QR para Linux, 100% offline. Dear ImGui +
GLFW + OpenGL3, ~4.6k líneas propias, **cero dependencias de terceros** para la
parte QR: el encoder, el decoder, el rasterizador, el escritor de PNG y el de
SVG están implementados en el repo contra ISO/IEC 18004.

Lacharacteristic que lo distingue: **nada se muestra ni se exporta sin haber sido
verificado**. Cada símbolo que la app produce se decodifica de vuelta con la
corrección de errores desactivada; si el resultado no coincide con el payload, es
un error, no una imagen.

<p align="center">
  <a href="https://github.com/disruptorh/QR-Generator/releases/latest/download/qr_generator">
    <img alt="Descargar" src="https://img.shields.io/badge/%E2%AC%87%20Download-latest%20release-2f6feb?style=for-the-badge&logo=github&logoColor=white">
  </a>
  <a href="https://github.com/disruptorh/QR-Generator/releases/latest">
    <img alt="Versiones" src="https://img.shields.io/github/v/release/disruptorh/QR-Generator?label=release&style=flat&logo=github&logoColor=white">
  </a>
  <a href="./LICENSE">
    <img alt="Licencia" src="https://img.shields.io/badge/licencia-Apache--2.0-blue?style=flat">
  </a>
</p>

## 📥 Descarga rápida

El botón de arriba descarga el asset `qr_generator` de la release más reciente
publicada: un ejecutable **Linux x86-64 sin extensión de fichero**. Es un único
binario, no un instalador ni un `.tar.gz`.

Para usarlo desde una terminal, o para fijarte en una versión concreta:

```bash
curl -L -o qr_generator https://github.com/disruptorh/QR-Generator/releases/latest/download/qr_generator && chmod +x qr_generator && ./qr_generator
```

No es un binario estático: enlaza dinámicamente contra `libOpenGL.so.0`,
`libGLdispatch.so.0`, `libglfw.so.3`, `libX11.so.6` y `libz.so.1`. En Debian/Ubuntu
se resuelven con:

```bash
sudo apt update && sudo apt install -y libopengl-dev libglfw3-dev zlib1g
```

Para que los acentos de la interfaz se vean bien, instala además una fuente
monoespaciada con cobertura Latin-1 (la app busca varias rutas; si no encuentra
ninguna usa la fuente integrada y avisa por stderr):

```bash
sudo apt install -y fonts-dejavu-mono
```

Para ejecutarlo sin pantalla (CI, contenedores, SSH sin X11):

```bash
sudo apt install -y xvfb && xvfb-run -a ./qr_generator
```

## 🚀 Uso rápido

1. Elige el **tipo de contenido** en el desplegable: Texto, URL, Wi-Fi, vCard,
   Correo, SMS, Teléfono, Geo o Evento.
2. Rellena los campos del tipo elegido. La app normaliza y valida por ti: una
   URL desnuda (`ejemplo.com`) se convierte en `https://ejemplo.com`, y si un
   campo está mal el error dice **exactamente qué campo** es.
3. Ajusta las opciones de salida: corrección de errores (Bajo 7%, Medio 15%,
   Cuartil 25%, Alto 30%), *Reforzar si cabe en la misma versión*, máscara
   (automática o forzada 0..7), versión mínima y máxima (1..40), píxeles por módulo
   (1..32) y zona de silencio (0..8). Elige **Guardar PNG** o **Guardar SVG**.
4. No hay botón de generar: el símbolo se reconstruye y se **verifica** en cada
   frame según escribes. Verás el resultado junto a un resumen de versión, ECC,
   módulos por lado, bytes usados y capacidad; si un campo no valida, el símbolo
   desaparece y el error dice **exactamente qué campo** es.
5. **Copiar contenido** pone el payload exacto en el portapapeles y programa su
   borrado a los 20 s.

El botón de guardar abre un diálogo integrado con filtro por extensión (`.png` o
`.svg`) y un nombre por defecto tipo `qr-v03-m.png`. El SVG emite todos los
módulos oscuros como un único `path`, así que el fichero es pequeño y escala sin
artefactos de remuestreo.

## 📦 Compilar desde código

### Requisitos

- CMake ≥ 3.20, compilador C++20 (GCC ≥ 10 o clang ≥ 12).
- Sistema: zlib, GLFW 3.3 y OpenGL (los tres salen de `find_package`).

### Clonar

Este repo **no tiene submódulos**: Dear ImGui viene versionada dentro del propio
repositorio, así que un `git clone` normal basta.

```bash
# 1. Clonar el repositorio
git clone https://github.com/disruptorh/QR-Generator.git
cd QR-Generator
```

### Dependencias

**Ya vendored en el repo, no hay que instalarla**:

| Dependencia | Dónde vive | Nota |
|---|---|---|
| Dear ImGui | `third_party/imgui` | versionada en el repo; solo se enlaza en el target de la GUI |

**Hay que instalarlas del sistema**. El `CMakeLists.txt` las pide con
`find_package(ZLIB REQUIRED)`, `find_package(glfw3 3.3 REQUIRED)` y
`find_package(OpenGL REQUIRED)`:

| Paquete Debian/Ubuntu | Para qué lo pide CMake |
|---|---|
| `build-essential` | g++, make |
| `cmake` | el propio build |
| `zlib1g-dev` | `ZLIB::ZLIB`: comprime el stream deflate del PNG |
| `libglfw3-dev` | `glfw` (≥ 3.3): ventana, contexto GL y portapapeles |
| `libopengl-dev` | `OpenGL::GL` |
| `libgl-dev` | cabeceras y `libGL.so` de Mesa |

No hace falta `pkg-config`: GLFW se localiza por su módulo de CMake, no por
`pkg-config`.

```bash
# 2. Instalar las dependencias de compilación (Debian/Ubuntu)
sudo apt update && sudo apt install -y build-essential cmake zlib1g-dev libglfw3-dev libopengl-dev libgl-dev
```

### Compilar

```bash
# 3. Configurar y compilar
cmake -S . -B build && cmake --build build -j
```

El ejecutable queda **directamente en `build/`**: `./build/qr_generator`. Nada de
`build/Release/`.

Los targets que produce el build:

| Target | Qué es |
|---|---|
| `qr_generator` | la aplicación de escritorio |
| `qr_core` | librería estática: QR, render, payload y estado de la app (sin GUI, usable headless) |
| `imgui` | Dear ImGui vendored, compilado solo en el target de la GUI |
| `qr_gui` | capa de pegamento: tema, portapapeles, sandbox y ventana |

### Ejecutar los tests

Los tests se compilan siempre (este proyecto no tiene opción para desactivarlos)
y son cinco binarios independientes sobre `qr_core`, ninguno necesita display.

```bash
# 4. Suite de tests (requiere el binario ya compilado)
ctest --test-dir build --output-on-failure
```

| Target CTest | Qué cubre |
|---|---|
| `qr_encoder_tests` | Tablas de la spec, polinomio generador, vector conocido, el stream de bits contra la spec, round-trip en **las 40 versiones** y los 4 niveles ECC, modos mixtos, optimalidad de la segmentación, desbordamiento y límites, determinismo, y las 8 máscaras |
| `qr_decoder_tests` | Round-trip en todas las versiones y niveles, corrección de errores Reed-Solomon, recuperación justo en el límite de corrección, rechazo de matrices que no son símbolos, verificación del padding y del informe de segmentos |
| `render_tests` | Rasterizado a RGBA, rechazo de opciones inválidas, escritura de PNG (chunks y CRC), escritura de SVG y round-trip render → decodificar |
| `payload_tests` | Los 9 constructores de payload (texto, URL, Wi-Fi con escapado, vCard 3.0, email, SMS/tel, geo, evento), independencia de locale en geo, etiquetas, y que **todo** payload sobrevive al encoder |
| `app_state_tests` | `app::generate` para los 9 tipos, errores de validación que nombran el campo, que se respetan las opciones de render, nombres de fichero y descripciones, y que un símbolo no verificable **nunca** llega a exportarse |

### Ejecutar la aplicación

```bash
# 5. Lanzar la GUI
./build/qr_generator
```

Necesita OpenGL 3.3 core profile. Si el driver no lo da, la ventana no se crea y
la app avisa por stderr: *"No se pudo crear la ventana (OpenGL 3.3 no
disponible)"*.

## 🧰 Comandos útiles / Opciones

Este proyecto no expone opciones de CMake. Lo único configurable es
`CMAKE_BUILD_TYPE`, que se fuerza a `Release` si no lo pasas:

```bash
# Build en Release explícito
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

```bash
# Smoke test: compilar solo los tests y ejecutarlos sin la GUI
cmake --build build --target qr_encoder_tests qr_decoder_tests render_tests payload_tests app_state_tests -j && ctest --test-dir build --output-on-failure
```

### Humo de la GUI sin pantalla

La app **no** trae un *test seam* de salida por tiempo (no hay equivalente a
`ENCRYPT_SMOKE_MS` ni `MKD_SMOKE_MS`) ni script e2e: este repo no tiene
`scripts/`. Para comprobar que la ventana abre en un entorno sin display:

```bash
# Comprobar que la ventana abre; se sale cerrando la ventana (o con SIGTERM)
xvfb-run -a ./build/qr_generator
```

## 🗂️ Estructura del proyecto

```text
.
├── CMakeLists.txt      # qr_core, imgui, qr_gui, la app y los 5 tests
├── src/
│   ├── main.cpp        # sandbox primero, luego GLFW + ImGui + bucle de eventos
│   ├── core/           # Result<T>: valores o error con mensaje, nunca excepciones
│   ├── qr/             # qr_spec (tablas ISO 18004), qr_encoder, qr_decoder
│   ├── render/         # raster (RGBA), png (zlib a mano), svg (un solo path)
│   ├── payload/        # types.hpp + los 9 constructores de payload
│   ├── app/            # app_state (generar+verificar+exportar), theme
│   ├── platform/       # clipboard (vía GLFW) y security (sandbox seccomp)
│   └── ui/             # main_window y file_dialog (puros ImGui, sin fork)
└── tests/              # 5 binarios de test, uno por módulo
```

La separación importante: `qr_core` no depende de ImGui, GLFW ni OpenGL. La app es
una capa fina sobre un flujo que se puede ejercitar entero sin display, y por eso
`app_state_tests` puede comprobar el round-trip completo en un test headless.

### Arquitectura de la verificación

`qr_spec.hpp` es la única fuente de verdad de todo lo que la spec fija por tabla
(codewords, disposición de bloques, geometría de patrones, máscaras, los dos
bloques BCH de información). **El encoder y el decoder leen de ahí**, así que un
símbolo no puede construirse con unas reglas y leerse con otras. Lo que
*no* comparten es el código de colocación, enmascarado y segmentación: el decoder
recorre el símbolo como haría un escáner, así que que ambos coincidan es evidencia
real y no una tautología.

El generador de payload es explícito sobre el límite de lo que es invertible: el
XOR con clave derivada es reversible porque el XOR es involutivo y el secreto
determina la clave. No es cifrado autenticado. Para cifrar de verdad está
[Encrypt-C++](../Encrypt-C++/README.md) (Argon2id + XChaCha20-Poly1305).

## 🔐 Seguridad

### Qué protege y qué no

Este repo no maneja claves ni semillas: maneja payloads que el usuario escribe,
entre ellos **contraseñas de Wi-Fi, vCards, correos y números de teléfono**. El
modelo de amenazas es, por tanto, "lo que el usuario teclea no debe quedar por
ahí".

- **Sin escritura automática a disco**: `io.IniFilename = nullptr` y
  `io.LogFilename = nullptr` (nada de `imgui.ini` junto al binario), ni estado de
  ventana, ni caches de shaders. La app solo escribe cuando el usuario pulsa
  guardar y elige nombre y ubicación en el diálogo integrado.
- **Portapapeles con borrado programado**: el payload se borra a los 20 s
  (`kClipboardSeconds`), y el destructor de `Clipboard` también borra, así que
  una salida anormal no deja una contraseña de Wi-Fi pegada. GLFW sirve la
  selección por ti, con un handle de ventana `NULL` a propósito: el borrado ocurre
  cuando la app decide, no cuando se cierra la ventana.
- **Core dumps desactivados**: `harden_process()` pone `RLIMIT_CORE = 0` porque un
  dump contendría la contraseña de Wi-Fi o la vCard que el usuario acaba de teclear.

### Bloqueo de red en runtime

`platform::harden_process()` instala un filtro **seccomp-BPF** con
`prctl(PR_SET_SECCOMP)` y un programa BPF ensamblado a mano, porque la máquina de
build no tiene libseccomp ni `seccomp.h`: solo hacen falta los números de syscall.

- `socket()` solo se permite para `AF_UNIX` (la familia que usa X11 para llegar al
  display server). No se puede crear ningún socket `AF_INET` o `AF_INET6`, así que
  no se puede abrir ninguna conexión de red.
- Se deniegan además `socketpair`, `bind`, `listen`, `accept` y `accept4`.
- El filtro se instala **antes de abrir la ventana o cualquier fichero**, y la app
  muestra el resultado en la interfaz: *"Red bloqueada (seccomp) y sin volcados de
  memoria"*.

El límite, dicho con honestidad y tal como está en el código: BPF clásico bajo
seccomp solo permite cargas `LD|ABS` de 64 bits, así que **no se puede inspeccionar
`connect()`** — el filtro no tiene forma de leer la familia dentro de un
`sockaddr`. Y como libX11 necesita la familia `send`/`recv` sobre su socket de
display, esas llamadas tampoco se pueden denegar. La garantía se apoya entonces en
que el proceso arranca limpio: no hereda descriptores más allá de
stdin/stdout/stderr, así que no hay ningún socket IP al que `connect()` pueda
alcanzar. El propio `security.hpp` lo dice: esto **no** sustituye a un perfil
AppArmor o firejail, que también restringen el sistema de ficheros.

Si el kernel o `prctl` rechazan el filtro, la app **arranca igual** y lo dice en la
interfaz (*"Sin red no garantizada: seccomp no disponible"*): una ventana que se
cierra al arrancar sería peor que una que admite no poder cerrarse.

### Endurecimiento de compilación

`qr_core` se compila con `-Wall -Wextra -Wpedantic`. El resto de targets no lleva
flags de endurecimiento adicionales, y el proyecto no trae `clang-tidy` ni
`.clang-format`. Dear ImGui aquí **no** lleva el perfil airgapped de los otros
repos de esta cuenta (no hay `IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS` ni loader GL
propio): se enlaza el `imgui_demo.cpp` de serie y GLFW+OpenGL resuelven los entry
points por su cuenta. Lo que compensa es el filtro seccomp de arriba y el hecho de
que este proyecto no maneje material criptográfico.

## 📄 Licencia

Apache-2.0 (ver `LICENSE`). Dear ImGui se distribuye con su propia licencia MIT
dentro de `third_party/`.