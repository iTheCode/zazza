# Validación de Zazza 1.1.0

Validación local del 12 de noviembre de 2026, macOS 26.2, Apple Silicon, Xcode 26.5.

- VST3 y standalone **arm64 y x86_64**: compilaciones Release correctas. Las pruebas Intel se ejecutaron mediante Rosetta en este Mac; no se presentan como pruebas en un Mac Intel físico.
- CTest: **2/2 suites correctas en cada arquitectura**. El motor ejecuta **449.068 comprobaciones**, incluyendo armonía compartida, huecos para la melodía, repetición de motivos, mezcla reproducible, afinación del sampler, conversión 44.1/48 kHz, reproducción estéreo y final de muestra.
- Samples: importación WAV mono y AIFF estéreo, límites de 8 segundos/8 MiB, rechazo de silencio/archivos inválidos, conservación del sample al generar ideas y del sonido al variar patrones. El estado incorpora el audio, reproduce el mismo resultado al restaurar y sigue cargando sesiones originales del esquema 1.
- Concurrencia: reemplazo, captura del estado y retirada de samples durante procesamiento; se comprueba audio finito y liberación de notas MIDI externas cuando se llama directamente al callback y no obtiene el bloqueo de cambio de sample. El wrapper VST3 de JUCE toma ese bloqueo antes de entrar: esta prueba directa no demuestra que el wrapper nunca espere.
- Regresión del bloqueo: un listener de parámetros intenta adquirir el bloqueo de audio desde otro hilo durante las notificaciones de importación, retirada de samples y restauración del estado. Comprueba que hubo notificaciones y que el bloqueo estaba disponible, incluidos los gestos al importar/retirar. La prueba falló antes de la corrección (`Sample import notifies with audio callback lock held`) y pasó después. Las notificaciones y `copyState()` quedan fuera del bloqueo; dentro solo se intercambian referencias a muestras y se reinician voces/marcas.
- `pluginval` 1.0.4, exigencia 5, **arm64 y x86_64: SUCCESS**, incluida la apertura del VST3 y su editor, audio con diferentes bloques/frecuencias, automatización, estado y buses. No se ejecutó el validador independiente del SDK de Steinberg.
- Captura: generada desde el editor JUCE real; no es un mockup.
- GitHub Actions: validado con actionlint. Python 3.12 queda instalado explícitamente y pluginval es obligatorio antes de empaquetar en cada plataforma. La ejecución remota en GitHub y Windows todavía no se ha realizado.
- Scripts Python: **11/11 pruebas correctas**, incluyendo el manifiesto del SDK con comas finales, preservación de cadenas, rechazo de datos inválidos, ZIP de fuentes sin sobrescrituras, rechazo de descargas alteradas, fallos/timeouts del validador y bloqueo del empaquetado. El recorrido Windows se prueba con archivos y procesos simulados, no con un ejecutable Windows real.
- Wallpaper y fuente: hashes idénticos a los recursos originales del usuario.

Los registros locales están en `validation/`, excluidos del repositorio y del ZIP de fuentes. La regresión antes/después se conserva en `lock-regression-before-test.log` y `lock-regression-after-test.log`. La entrega corregida se identifica en `dist/README.md`. El primer intento de pluginval dentro del sandbox abortó al registrar su aplicación en macOS, antes de cargar Zazza. La ejecución posterior fuera del sandbox completó correctamente la validación.

La resolución de la revisión anterior está en [REVIEW.md](REVIEW.md). Los cambios 1.1 están en `Source/MusicEngine.*` (arreglos y sampler), `Source/SampleAsset.*` (lectura de audio), `Source/PluginProcessor.*` (generación, persistencia e intercambio de samples) y `Source/PluginEditor.*` (importación, arrastrar archivos y controles). Los paquetes anteriores se conservan; `dist/README.md` identifica la entrega vigente.

No se ha probado dentro de Ableton, FL Studio, Cubase u otro DAW del usuario ni con samples aportados por él: las pruebas usan señales sintéticas WAV/AIFF. No se evalúa subjetivamente que todos los arreglos suenen bien. Hay tres capas de síntesis o muestras propias; la raíz de cada sample se ajusta manualmente. No hay time-stretch, análisis automático de loops, hosting de plugins externos, animación del GIF, firma comercial, notarización ni publicación. Windows y GitHub Actions remotos siguen pendientes.

## Integración de semantic-release

La configuración se adaptó de FindNote, conservando la firma/notarización que ya existía en el workflow de Zazza. Estas comprobaciones son posteriores a las validaciones 1.1.0 descritas arriba:

- `npm ci --ignore-scripts` instala el lockfile y las **24 pruebas Node pasan**: clasificación real de Conventional Commits, notas, resolución de assets del plugin de GitHub, planes sin publicación y rechazo de paquetes ausentes, corruptos, de otro commit/versión, incompletos o sin pluginval. Las pruebas del plan de push inyectan el resultado del dry-run; no se ejecutó un dry-run remoto autenticado ni una publicación.
- **13 pruebas Python pasan**, incluyendo propagación de versión a CMake, rechazo de discrepancias con el manifiesto y ZIP de fuentes con scripts de firma y release.
- **actionlint y `git diff --check` correctos**. SHA de setup-node 7.1.0 y download-artifact 8.0.2 comprobados con la API oficial de GitHub.
- Motor portátil local: configuración con versión externa, compilación y **CTest 1/1 correcto**. CMake rechaza una versión de prerelease.
- VST3 y standalone macOS arm64: compilación real mediante `build.py --version 1.2.3`, **CTest 2/2 correcto**, manifiesto VST3 y ambos Info.plist con `1.2.3`, arquitectura y firmas ad hoc verificadas. Esa versión se usó únicamente para comprobar la propagación; los paquetes distribuidos anteriormente se conservaron.

No se volvió a ejecutar pluginval para este cambio de tooling; continúa siendo obligatorio en cada build de Actions. Tampoco se ejecutó el nuevo workflow remoto, la notarización o la publicación. Los registros están en `validation/semantic-release-*.log`. La validación de paquetes de Windows y macOS Intel en las pruebas de release usa fixtures; no equivale a compilar esas plataformas con el nuevo flujo. CI instala Node 24; las pruebas locales se ejecutaron con Node 25.1.0.


## Reproducción completa y editor de slices

La regresión de audio se cubre con una muestra sintética de dos segundos: Completo/Original sigue sonando después de un note-off corto, con caída mínima y una nota dos octavas más alta. Las pruebas comprueban también límites del slice, silencio tras el final, Gate, parada forzada, rangos invertidos, conservación del audio importado, persistencia de los controles, migración de presets originales con samples y escucha a velocidad original.

`Source/SampleEditor.*` muestra la forma de onda mono/estéreo, selección no destructiva, límites en segundos, zoom 1–32×, desplazamiento, modos de reproducción y nota raíz. `docs/images/sample-slice.png` es una captura del componente JUCE real. El importador mantiene sus límites de 8 segundos/8 MiB; no se implementan time-stretch, detección de nota o slicing automático por transitorios. Los presets antiguos mantienen Gate/Afinado; las importaciones nuevas seleccionan Completo/Original.

Los registros de esta revisión están en `validation/sample-slice-*.log`; `dist/README.md` identifica los paquetes locales corregidos. Las pruebas usan señales sintéticas: queda pendiente escuchar los samples del usuario en su DAW y ejecutar esta revisión en los runners de Actions/Windows.


### Revisión de voces de samples

Se añadieron regresiones de desbordamiento de las 12 voces, disparos en el mismo frame, fade de retirada de 5 ms y agotamiento de colas a tres frecuencias de muestreo. El caso de continuidad falló en el motor anterior y pasó tras la corrección. La integración comprueba que un acorde Original tiene el mismo audio que un solo disparo, aunque conserva cuatro notas MIDI. También cubre amplitud PCM intacta en importaciones nuevas, rechazo de amplitudes float extremas y compatibilidad del nivel al migrar y volver a guardar estados antiguos. La evidencia antes/después está en `validation/voice-review-*.log`; la resolución por hallazgo está en `REVIEW.md`.
