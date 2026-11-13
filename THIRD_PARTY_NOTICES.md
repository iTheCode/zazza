# Recursos y dependencias

- **JUCE 8.0.17-security**, commit `dabd5ad13cd869cba41046947fffc8e5ce5cd662`: AGPLv3 o licencia comercial de JUCE. [Términos de esta revisión](https://github.com/juce-framework/JUCE/blob/dabd5ad13cd869cba41046947fffc8e5ce5cd662/LICENSE.md). El SDK VST3 integrado tiene sus propios avisos en esa dependencia. CMake descarga la revisión fijada; no se suben sus binarios ni el SDK al repositorio.
- **Luckiest Guy**, Astigmatic: fuente reutilizada de FindNote. Licencia Apache 2.0 y atribución en `assets/fonts/LICENSE.txt` y `assets/fonts/NOTICE.txt`, incluidas también en cada paquete.
- **wallpaper.gif**: copia exacta del recurso proporcionado por el usuario (`190588.gif`). Se muestra su primer fotograma. El código del plugin no concede derechos sobre esta ilustración; la autorización de redistribución no fue acreditada en este proyecto.
- La estética y la configuración JUCE toman como referencia el proyecto local FindNote del usuario. El motor musical y el instrumento se implementaron para Zazza.

El proyecto contiene un archivo `LICENSE` con el texto de AGPLv3, conservado sin modificaciones e incluido en los paquetes. Sigue pendiente confirmar los derechos de redistribución del fondo. Los paquetes macOS tienen firma ad hoc, sin Developer ID ni notarización; Windows no incluye Authenticode.
