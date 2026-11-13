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


## Revisión de polifonía y nivel de samples

- **Reemplazo de voces: corregido.** La búsqueda anterior podía elegir repetidamente `lane[0]` al empatar las envolventes one-shot. `Synth::noteOn` ahora usa un orden de disparo monotónico (también distingue notas que llegan antes del mismo frame), prioriza voces en liberación y después las más antiguas. Continúa la onda desplazada durante un fade de 5 ms en un banco fijo de 12 colas. Con las 12 voces y las 12 colas ocupadas, un disparo que necesita retirar otra voz se omite internamente; no se corta una cola ni se asigna memoria en el hilo de audio.
- **Cuatro copias en Original: el hallazgo no corresponde al código revisado.** El bucle de disparo del secuenciador ya contiene `if (n == 0 || !sampleViews[index].enabled || sampleViews[index].pitched)` antes de llamar a `synth.noteOn`. En Original dispara solo `n == 0`, aunque sigue emitiendo las cuatro notas MIDI. Una nueva prueba compara muestra por muestra el acorde Original con un único disparo a igual velocidad y cuenta las cuatro notas del MIDI. Los acordes tocados manualmente por MIDI son eventos independientes, no generados por ese bucle.
- **Atenuación al importar: corregida para nuevas importaciones.** El buffer conserva la amplitud decodificada, y el archivo incrustado sigue intacto. Se mantiene el margen de mezcla por capa para la polifonía. El esquema 3 guarda `legacyGain` por sample: los esquemas 1/2 conservan el comportamiento anterior, también después de volver a guardar. Los valores de audio no finitos o extremos (>16 en valor absoluto) se rechazan explícitamente en lugar de normalizarlos silenciosamente.
- **Vista visual: se mantiene la forma de onda** como editor temporal para elegir slices, con zoom y límites en segundos. No se presenta como FFT o espectrograma. Una vista de frecuencias sigue fuera de esta implementación.
- **Presets antiguos: comportamiento conservado deliberadamente.** Gate/Afinado y la ganancia anterior no se cambian al restaurar un proyecto existente. Para adoptar el comportamiento nuevo puede seleccionarse Completo/Original; reimportar el archivo también elimina la atenuación heredada.

La regresión del motor falló antes del arreglo con `Voice stealing introduces a hard audio discontinuity`. Las pruebas nuevas ejercitan 18 disparos completos, 4 disparos simultáneos, agotamiento de colas, parada y continuidad a 44.1/48/96 kHz. La prueba de nivel usa PCM16 con pico superior a 0.8 y comprueba importación, guardado, restauración y migración del esquema 2. Son pruebas sintéticas; no prueban que el recorte oído por el usuario proceda necesariamente de este defecto ni sustituyen la escucha en su DAW.


## Revisión de samples largos y referencias externas

- El importador ya admite duraciones mayores de 8 segundos; el presupuesto es de memoria y no de tiempo. El editor selecciona y escucha regiones posteriores al segundo 8. La prueba de 100 segundos ahora verifica tanto el inicio en 90 como la parada al llegar a 95. Se mantiene 128 BPM al crear una instancia.
- **Rutas de red en presets: corregido.** Antes se abría cualquier ruta absoluta antes de comparar su hash. Ahora se descartan sintácticamente rutas UNC, separadores mixtos y espacios de nombres de dispositivo antes de construir un `File` o hacer E/S. En Windows se consulta exclusivamente la raíz de la unidad con `GetDriveTypeW` y solo se permite carga automática desde unidades locales reconocidas; las unidades mapeadas de red requieren REUBICAR. Tipos de unidad según [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdrivetypew).
- Las referencias bloqueadas se conservan junto a su SHA-256 y selección; se muestran como FALTA y se pueden recuperar seleccionando explícitamente el archivo. La prueba usa una ruta local con doble barra en POSIX para comprobar el bloqueo sin contactar servidores y verifica la recuperación con REUBICAR. Las pruebas léxicas cubren sintaxis Windows y POSIX. No se ha ejecutado Windows ni probado su clasificación nativa de unidades.
- La carga y restauración siguen siendo síncronas y decodifican el archivo completo. El 25% de RAM es un límite por sample: tres capas pueden sumar el 75%, más buffers temporales y memoria del DAW. No se implementó streaming ni un presupuesto global. Se documenta esta limitación; no se presenta como duración infinita o carga instantánea.
- El editor incluye selección no destructiva, inicio/fin, zoom, preescucha, raíz y modos Completo/Gate y Original/Afinado. No incluye reverse, loops de reproducción ni ADSR específico de sampler.
