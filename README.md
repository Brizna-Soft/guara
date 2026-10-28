# Guara

**Guara: revisa tu PC y te dice qué hacer.**

Guara es un programa gratis para Windows 10 y 11 (64 bits). Revisa tu PC **sin cambiar nada** y te explica en
español sencillo qué tiene y qué anda mal. Si necesitas ayuda, te da un informe para pasárselo a quien te ayude,
sin tus datos personales.

La guara roja es el ave nacional de Honduras: lista y habladora, como este programa.

Es de [Brizna Soft](https://brizna-soft.github.io): gratis, de código abierto, sin publicidad y sin recoger datos.

## Qué revisa

Cada parte se muestra en una tarjeta con un semáforo: verde **Bien**, amarillo **Revisa esto** y rojo
**Problema**. Cada aviso dice en una línea qué significa y qué hacer, y tiene un botón **Ver solución**.

- **Resumen:** lo más importante, arriba.
- **Sistema:** edición, versión y compilación de Windows, cuánto tiempo lleva encendida y la fecha de la última
  actualización instalada.
- **Procesador:** modelo, núcleos, hilos y qué tan ocupado está.
- **Memoria:** total, en uso y los módulos con su velocidad.
- **Discos:** modelo, tipo (SSD, HDD o NVMe), salud y espacio libre de cada unidad. Avisa si queda menos del 10 %.
- **Tarjeta gráfica:** nombre, versión y fecha del controlador.
- **Batería** (en laptops): la salud, es decir, cuánto le queda de su capacidad original.
- **Dispositivos con problemas:** los que tienen un error en el Administrador de dispositivos, con lo que significa.
- **Seguridad:** el antivirus y el estado de Microsoft Defender.
- **Apagados inesperados y pantallas azules** de los últimos 30 días, con el código y una pista de la causa.
- **Programas que arrancan con Windows:** la lista, y un botón que abre el Administrador de tareas para
  desactivarlos ahí.

## Buscador de soluciones

En la pestaña **Buscar soluciones** escribes lo que le pasa a tu PC (por ejemplo «lenta», «sonido», «wifi» o
«pantalla azul») y ves pasos cortos y seguros, un botón que abre la herramienta de Windows que corresponde y un
enlace a la página de ayuda oficial de Microsoft. Hay 24 problemas comunes. Los pasos nunca piden editar el
registro, borrar archivos de Windows ni apagar el antivirus.

## Guara no cambia nada

- Solo lee información: no cambia la configuración de Windows, no borra nada y no instala nada.
- No pide ser administrador.
- No hace falta instalarlo: es un solo archivo, `Guara.exe`.
- Los botones solo abren herramientas de Windows (por ejemplo, Configuración o el Administrador de tareas). Lo
  que hagas ahí lo decides tú.

## Qué datos no salen de tu PC

- Guara **no envía nada**: no tiene publicidad, ni estadísticas, ni nada parecido. Solo se conecta a internet si
  tú pulsas **Buscar actualización** (consulta la página de versiones de este repositorio en GitHub) o un enlace.
- El informe (**Copiar informe** y **Guardar informe...**) se queda en tu PC hasta que tú lo pegues o lo mandes.
  **No incluye** tu nombre de usuario, el nombre de la PC, números de serie, direcciones IP ni MAC. Las rutas de
  tu carpeta personal aparecen como `%USERPROFILE%`, y los aparatos Bluetooth, los teléfonos y las impresoras
  salen sin su nombre, porque podría ser personal.
- Lo único que guarda es el tamaño de la ventana, en `%LOCALAPPDATA%\Guara\ajustes.json`.

## Cómo usarlo

1. Baja `Guara-0.1.0-x64.zip` desde [Versiones (Releases)](https://github.com/Brizna-Soft/guara/releases).
2. Descomprímelo y abre `Guara.exe`.
3. Espera unos segundos mientras dice «Revisando...».

Como el programa es nuevo y todavía no tiene firma digital, Windows puede mostrar un aviso de SmartScreen.
Puedes comprobar que el archivo es el original con su huella SHA256 (está en el archivo `.sha256` de cada versión).

## Cómo reportar problemas

Entra en [Reportar un problema](https://github.com/Brizna-Soft/guara/issues/new/choose) (también está en el menú
**Ayuda** de Guara). Si quieres, pega el informe de **Copiar informe**: no lleva tus datos personales. Por
favor, no pongas tu correo, teléfono ni contraseñas.

## Límites conocidos

- La salud de la batería depende de lo que informe el fabricante; en algunas laptops sale «No disponible».
- Guara no mide temperaturas ni la salud detallada de los discos (eso pide permisos de administrador).
- Si Windows no deja leer algo, la tarjeta dice «No disponible» y el resto sigue funcionando.

## Para programadores

- C++20 con wxWidgets 3.3 (enlazado de forma estática) y nlohmann/json, con vcpkg.
- Lo de Windows se lee con WMI, el registro de eventos y la API de Windows, siempre en un hilo aparte.
- Compilar: `powershell -ExecutionPolicy Bypass -File compilar.ps1` (necesita Visual Studio 2022 con C++).
  Deja el programa en `programa\`.
- Empaquetar: `compilar.ps1 -Publicacion` y después `empaquetar.ps1`. Deja el .zip y su .sha256 en `publicar\`.
- Las soluciones están en `res\soluciones.json` y van dentro del .exe. Para agregar una, súmala a ese archivo
  con un enlace oficial de Microsoft y vuelve a compilar.
- El ícono se dibuja con `res\crear-icono.ps1`.

## Licencia

Guara usa la licencia MIT (archivo [LICENSE](LICENSE)). Las bibliotecas que van dentro del programa tienen sus
propias licencias, en [AVISOS-DE-TERCEROS.txt](AVISOS-DE-TERCEROS.txt).

Si Guara te sirvió y quieres apoyar a Brizna Soft, puedes [donar por PayPal](https://paypal.me/BriznaSoft).

Hecho con ayuda de IA.
