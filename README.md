# QR Generator — Offline

Generador de códigos QR de escritorio en Python + Tkinter, **100% offline**: no
hace ninguna petición de red, todo se genera localmente. Un solo archivo,
`QR-Generator.py`, sin build ni empaquetado.

Cuatro plantillas de datos:

| Tipo | Payload generado |
|---|---|
| Texto libre | El texto que escribas, tal cual |
| WiFi | `WIFI:T:{seguridad};S:{ssid};P:{password};;` |
| Contacto (vCard) | `BEGIN:VCARD` / `VERSION:3.0` / `FN` / `TEL` / `EMAIL` / `END:VCARD` |
| Coordenadas GPS | `geo:{latitud},{longitud}` |

Los campos de la derecha cambian según el tipo elegido: al seleccionar WiFi
aparecen SSID, contraseña y seguridad; en vCard, nombre, teléfono y email.

## Requisitos

- Python 3.9+ con Tkinter (viene con Python en la mayoría de distribuciones;
  en Debian/Ubuntu puede necesitar `sudo apt install python3-tk`).
- Dos dependencias:

```sh
pip install "qrcode[pil]" pillow
```

## Uso

```sh
python3 QR-Generator.py
```

1. Elige el **tipo de datos** y rellena el contenido. El primer campo nunca
   puede quedar vacío.
2. Ajusta las **opciones**: color (5 paletas), nivel de corrección de error
   (L/M/Q/H), tamaño de píxel (5–20) y borde en módulos (1–10).
3. **GENERAR QR** — previsualiza el código y lo guarda automáticamente como
   `qr_output.png` junto al script.
4. **GUARDAR PNG** — exporta a la ruta que elijas.

## Opciones

- **Corrección de error**: `L` (baja, más datos), `M` (media, por defecto),
  `Q` (alta), `H` (máxima robustez — aguanta un 30 % de daño, ideal para
  imprimir o estampar).
- **Paletas**: clásico negro/blanco, matrix verde neón, ciberpunk cyan/negro,
  ámbar retro y rojo oscuro.
- **Tamaño de píxel** y **borde** (la zona de silencio, que los lectores
  necesitan para localizar el código) ajustables en vivo.

## Estructura

```
QR-Generator.py    # toda la app: paleta, plantillas, UI Tkinter y generación
```

Un único archivo de ~480 líneas organizado en secciones: paleta de colores,
tabla de tipos de QR, construcción de la UI (columna izquierda de
configuración, derecha de resultado), y generación/guardado.

## Notas

- El script **escribe en disco** al generar: `qr_output.png` en el directorio
  del propio script, sin preguntar. El botón **GUARDAR PNG** es el que te deja
  elegir la ruta y el nombre.
- Al cargar un QR con credenciales de WiFi o una vCard, el contenido queda
  legible por cualquiera que lo escanee: trátalo como un dato público, no como
  un secreto. Para material sensible, usa las herramientas de
  [Bip39-Generator-C++](../Bip39-Generator-C++/README.md) o
  [Bip39-Obfuscator-C++](../Bip39-Obfuscator-C++/README.md), que son
  airgapped de verdad y no filtran datos a terceros.

## Licencia

Apache-2.0 (ver `LICENSE`).
