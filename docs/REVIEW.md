# Resolución de la revisión

La revisión se contrastó con el código, la API oficial de GitHub y pruebas locales. No se ejecutaron sus instrucciones de commit/push ni se borraron paquetes anteriores.

| Hallazgo | Resolución |
| --- | --- |
| CI sin pluginval | Corregido: `scripts/build.py --pluginval` valida antes de empaquetar. El workflow lo exige en las tres plataformas. Se mantienen las pruebas de GUI y el proceso aislado predeterminado del validador. |
| Dependencia del alias `python` | Corregido: `actions/setup-python` instala Python 3.12 en ambos jobs. No era un fallo demostrado del runner; ahora el workflow no depende de ese supuesto. |
| Windows y Actions no ejecutados | Pendiente. Los tests de comandos/rutas Windows son simulados. No hay repositorio remoto configurado ni autorización para hacer push. No se presentan las pruebas locales como evidencia de una ejecución remota. |
| VST3 externos | Aclarado posteriormente por el usuario: quiere patrones y sonidos coordinados, incluidos samples propios. La versión 1.1 importa WAV/AIFF por capa; no aloja instrumentos VST3 externos. |
| ZIP antiguo en dist | Conservado por la instrucción de no eliminar datos. `dist/README.md` identifica los paquetes de la revisión vigente; `dist/` está ignorado por Git. |
| GIF estático | Limitación documentada. El requisito original pide usarlo como wallpaper, no exige animarlo. No se amplió el alcance a un reproductor de GIF. |
| Falta LICENSE propio | El estado actual del proyecto ya contiene `LICENSE` con AGPLv3. Se conserva sin modificaciones y se incluye en los paquetes nativos y de fuentes. No se presume autorización sobre el wallpaper. |
| Conteo exacto de parámetros | Corregido: contrato fijo de IDs del esquema 1 y valores predeterminados para nuevas adiciones. Una prueba simula un parámetro futuro y verifica conservación de valores y rechazo de estados truncados. |

Los tags de checkout 7.0.1, upload-artifact 7.0.2 y setup-python 7.1.0 se resolvieron mediante la API oficial de GitHub y coinciden con los SHA del workflow. Los assets de pluginval 1.0.4 se descargaron de la release oficial y se fijaron sus hashes en `scripts/validate_plugin.py`. Esa release no publica un campo `digest` en su API; los SHA fijados se calcularon a partir de las descargas.

Fuentes: [setup-python](https://github.com/actions/setup-python/releases/tag/v7.1.0), [pluginval 1.0.4](https://github.com/Tracktion/pluginval/releases/tag/v1.0.4), [opciones CLI de pluginval](https://github.com/Tracktion/pluginval/blob/v1.0.4/Source/CommandLine.cpp).

## Revisión de samples y concurrencia (1.1.0)

El hallazgo del bloqueo es válido. En la revisión fijada de JUCE, `juce_audio_plugin_client_VST3.cpp`, función `processAudio`, toma `getCallbackLock()` de forma bloqueante antes de llamar al procesador. El `ScopedTryLock` dentro de `processBlock` no evita esa espera. APVTS actualiza parámetros mediante `setValueNotifyingHost`; por ello no deben notificarse bajo el bloqueo que requiere el audio.

`loadSampleData` y `clearSample` ahora notifican después del intercambio de muestras. `restoreState` intercambia las muestras y reinicia las voces dentro del bloqueo, y ejecuta `parameters.replaceState` fuera. Se vuelve a marcar el cambio tras reemplazar los parámetros para que el siguiente bloque de audio lo procese aunque haya ocurrido otro bloque durante las notificaciones. `getStateInformation` también copia los parámetros fuera del bloqueo: dentro solo conserva las referencias a las muestras. La decodificación, serialización y destrucción de muestras retiradas siguen fuera.

`Tests/ProcessorTests.cpp` incorpora un listener que comprueba el bloqueo desde otro hilo durante las notificaciones de los tres recorridos. Usar el mismo hilo daría un falso positivo por ser un bloqueo recursivo. El caso falla en el código anterior y pasa con el cambio. Esto verifica la disciplina del bloqueo, no demuestra ausencia de interrupciones o interbloqueos en todos los DAW. Reemplazar muestras reinicia las voces, y los parámetros de un preset se actualizan individualmente: no se promete una transición audible atómica.

Los demás hallazgos se mantienen como límites explícitos:

- Windows y Actions remotos siguen sin ejecutarse. No se ha hecho commit, push ni publicación para intentar validarlos.
- No hay prueba con los samples del usuario ni valoración subjetiva en su DAW. Las señales de prueba son sintéticas.
- Se conserva el audio incrustado para que los presets y sesiones no dependan de rutas externas. Tres muestras de 8 MiB producen aproximadamente 32 MiB de base64, más parámetros/XML, con límite total de 40 MiB. Esto puede encarecer el guardado automático; no se ha medido su latencia en DAW reales.
