# UX y accesibilidad de Quarry

Objetivo: **WCAG 2.2 AA**, uso completo con teclado y compatible con NVDA y Narrator (Windows), VoiceOver (macOS) y Orca (Linux). Cada componente nuevo cumple estas pautas desde el principio. Un componente que no las cumpla no se integra.

## 1. Principios

1. **Lo avanzado, solo cuando hace falta.** Los formularios muestran lo mínimo (host, puerto, usuario, base de datos) y el resto va en "Advanced options", plegado. Las acciones raras van en la paleta de comandos, no en la barra de herramientas.
2. **Los estados vacíos explican qué hacer.** Ninguna vista vacía se queda en blanco: dice qué es, por qué está vacía y qué hacer, con un botón para hacerlo. Ejemplo: "No results yet. Write a query and press Ctrl+Enter to run it."
3. **Errores en lenguaje claro.** Un título que dice qué pasó y qué hacer; el detalle técnico del driver va en una sección desplegable "Technical details" que se puede copiar (ARCHITECTURE §7).
4. **La información nunca se transmite solo con color.** El color va siempre acompañado de un icono, un texto o un patrón: producción, errores, NULL, celdas modificadas, estado de la conexión.
5. **Seguridad del usuario.** Producción en rojo con el texto "PRODUCTION" visible en la pestaña y en la barra de estado. Confirmación para `UPDATE`/`DELETE` sin `WHERE`. Vista previa del SQL antes de aplicar ediciones. Deshacer en el editor de datos. Las acciones destructivas nunca son el botón por defecto.
6. **Rápida y predecible.** Nada bloquea la interfaz. Las operaciones largas muestran su progreso y se pueden cancelar con la misma tecla en todas partes (Esc).
7. **La plataforma manda.** Se usan sus atajos (Cmd en macOS, Ctrl en Windows y Linux), sus tipografías, el escalado de fuente, el tema claro/oscuro, el alto contraste y la preferencia de reducir animaciones.

## 2. Requisitos de accesibilidad por componente

Cada control interactivo:
- Tiene `Accessible.role` correcto y `Accessible.name` traducible. Si el texto visible no basta (botones que solo tienen icono), añade `Accessible.description`.
- Es enfocable con teclado (`activeFocusOnTab`, o navegación interna con flechas en compuestos como el árbol, el grid o las pestañas).
- Muestra un **foco visible**: anillo de 2 px con contraste ≥ 3:1 frente al fondo, que no depende solo del color de fondo y no queda oculto por otros elementos (WCAG 2.4.7 y 2.4.11).
- Tiene un objetivo táctil o de clic ≥ 24×24 px lógicos (WCAG 2.5.8).
- Cumple el contraste de los tokens del tema: texto ≥ 4.5:1 (≥ 3:1 si es grande), bordes de controles y estados ≥ 3:1.
- Expone su estado: `checked`, `expanded`, `selected`, `disabled`, `busy`.
- Anuncia los cambios asíncronos relevantes con `Accessible.announce()` (Qt 6.8+), con prioridad *polite* salvo para los errores (*assertive*), y sin mover el foco.

Orden de tabulación: sigue el orden visual (de izquierda a derecha y de arriba abajo). Los diálogos atrapan el foco mientras están abiertos y lo devuelven al control que los abrió al cerrarse. **F6 / Shift+F6** recorre las regiones (árbol, editor, resultados, barra de estado).

## 3. Mapa de pantallas

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Barra de título/menú (macOS: menú global)                                │
├───────────────┬──────────────────────────────────────────────────────────┤
│ Sidebar       │ [● prod-db  PRODUCTION] [Query 2] [+]        ← pestañas  │
│ ─ Conexiones  ├──────────────────────────────────────────────────────────┤
│   ▸ local.db  │ Editor SQL                                               │
│   ▾ prod-db ● │                                                          │
│     ▾ public  │                                                          │
│       ▸ Tables├──────────────────────────────────────────────────────────┤
│               │ [Results] [Messages] [Plan]                              │
│ [+ New]       │ Grid de resultados virtualizado                          │
├───────────────┴──────────────────────────────────────────────────────────┤
│ Barra de estado: conexión · PRODUCTION · Running… 00:03 [Cancel] · 1 204 rows │
└──────────────────────────────────────────────────────────────────────────┘
```

| Pantalla | Contenido | Estado vacío |
|---|---|---|
| Bienvenida (sin conexiones) | Botones "New connection", "Open SQLite file" y atajos clave | Es en sí misma el estado vacío |
| Diálogo de conexión | Selector de motor → campos de `connectionFields()`, Advanced plegado, color, "Production", "Save password / Ask every time", "Test connection" | — |
| Sidebar / árbol | Conexiones y carpetas; esquema bajo demanda | "No connections yet" + botón |
| Editor | Pestañas, números de línea, sentencia actual resaltada | Placeholder: "Write SQL here. Ctrl+Enter runs the current statement." |
| Resultados | Grid, contador, tiempo, "Fetch all" | "Run a query to see results here." |
| Mensajes | Errores, notices, filas afectadas | "No messages." |
| Editor de datos (H2) | Grid editable + barra de cambios pendientes (n cambios, Preview SQL, Apply, Discard) | — |
| Paleta de comandos | Campo de búsqueda + lista filtrada de comandos con su atajo | "No commands match '…'" |
| Ajustes | Tema (System/Light/Dark/High contrast), tamaño del texto, animaciones (System/On/Off), filas por página, idioma | — |
| Confirmaciones de seguridad | SQL afectado, conexión (con su marca de producción), botón destructivo explícito (no por defecto) | — |

## 4. Atajos de teclado

`Mod` = Ctrl en Windows/Linux y Cmd en macOS. Se usa `QKeySequence::StandardKey` cuando existe uno equivalente.

| Acción | Atajo |
|---|---|
| Paleta de comandos | Mod+K (alternativo: Mod+Shift+P) |
| Ejecutar sentencia actual | Mod+Enter |
| Ejecutar todo / selección | Mod+Shift+Enter |
| Cancelar ejecución | Esc (en el editor/resultados) · Mod+. |
| Nueva pestaña de consulta | Mod+T |
| Cerrar pestaña | Mod+W |
| Pestaña siguiente / anterior | Ctrl+Tab / Ctrl+Shift+Tab |
| Nueva conexión | Mod+Shift+N |
| Recorrer regiones | F6 / Shift+F6 |
| Ir a árbol / editor / resultados | Mod+1 / Mod+2 / Mod+3 |
| Buscar en el editor / en el grid | Mod+F |
| Formatear SQL (hito posterior) | Mod+Shift+F |
| Autocompletar (H3) | Ctrl+Space |
| Editar celda (H2) | F2 o Enter · Esc cancela |
| Deshacer / rehacer | Mod+Z / Mod+Shift+Z (en Windows también Ctrl+Y) |
| Menú contextual | Shift+F10 / tecla Menú |
| Ajustes | Mod+, |
| Aumentar / reducir / restablecer zoom | Mod+= / Mod+- / Mod+0 |

Todos los atajos aparecen en la paleta de comandos junto a su comando; así se pueden descubrir sin documentación. Un atajo no puede ser solo una letra sin modificador fuera de los campos de texto (WCAG 2.1.4).

## 5. Paleta de comandos

- `CommandRegistry` (C++) guarda los comandos: `id`, `title` (traducible), `category`, `shortcut`, `enabled()` según el contexto y `trigger()`. Los menús, los atajos y la paleta usan esa misma fuente.
- Búsqueda difusa por título y categoría; los comandos usados recientemente salen primero.
- También sirve para saltar a objetos: con el prefijo `@` busca tablas y vistas en la caché de metadatos de la conexión activa (H3).
- Accesibilidad: un campo con rol `EditableText`, una lista con rol `List` y el elemento activo con `ListItem`. Al filtrar se anuncia "N commands". Las flechas mueven el elemento activo sin quitar el foco del campo. Enter ejecuta y Esc cierra y devuelve el foco.

## 6. Sistema de temas

- Tokens semánticos en un singleton QML `Theme` respaldado por `ThemeManager` (C++): `bg.canvas`, `bg.surface`, `bg.raised`, `fg.default`, `fg.muted`, `fg.onAccent`, `border.default`, `border.strong`, `focus.ring`, `accent`, `danger`, `warning`, `success`, `selection.bg`, `selection.fg`, `grid.null`, `conn.<color>`, `conn.production`. Los componentes nunca usan colores literales.
- Temas: **Light**, **Dark** y **High contrast** (claro y oscuro). En el modo "System" se siguen:
  - `QStyleHints::colorScheme` (claro/oscuro) con `colorSchemeChanged`.
  - `QStyleHints::accessibility()->contrastPreference()` (Qt 6.10+) para el alto contraste. En Windows, el modo de alto contraste usa los colores del sistema (`QPalette`).
  - `QStyleHints::accessibility()->motionPreference()` (Qt 6.12) para reducir animaciones: `Theme.animationDuration` pasa a 0 y se quitan las transiciones que no son esenciales.
- **Acento** (decisión del 2026-11-13): azul índigo. Lo usan la acción principal, la selección y el foco. Cada tema define su tono para cumplir ≥ 4.5:1 como texto y ≥ 3:1 como indicador. El rojo queda reservado para producción y errores, y el verde para éxito.
- **Densidad:** compacta por defecto (filas y controles de ~24 px). En Ajustes hay una opción "Comfortable" (~32 px). Los objetivos de clic nunca bajan de 24×24 px (WCAG 2.5.8). Se expone como `Theme.density` y las medidas se derivan de ella.
- **Iconos:** [Lucide](https://lucide.dev) (ISC) como fuente, recortada a los iconos usados (`scripts/icons.txt` → `scripts/subset_icons.py` → `src/app/assets/icons/lucide-subset.ttf` + `Icons.qml`). Se pintan con `Text` y toman el color del token del tema, así que funcionan en alto contraste, escalan con el zoom y no necesitan Qt Svg. Un icono nunca va solo: los botones que solo tienen icono llevan `Accessible.name` y un tooltip.
- Tipografía: base = fuente del sistema (`QGuiApplication::font()`). Todas las medidas se derivan de `Theme.fontSize` y del zoom de la app (Mod+=/-), y el layout aguanta hasta un 200 % sin cortar texto (WCAG 1.4.4). Para el código, una fuente monoespaciada del sistema configurable.
- Colores de conexión: una paleta de 8 colores distinguibles también con daltonismo: Blue, Orange, Teal, Purple, Pink, Gold, Green y Slate. Cada color va acompañado de su nombre y de un icono. **No hay rojo** (decisión del 2026-11-13): el rojo solo significa peligro o producción, y "Production" siempre añade además el texto y el icono de aviso.
- Separadores de paneles (decisión del 2026-11-13): el tirador de `QSplitView` es fino (8 px) para aprovechar el espacio. Para cumplir WCAG 2.5.8 por la excepción de *control equivalente*, cada panel se puede mostrar u ocultar y el diseño se puede restablecer con comandos de la paleta y del menú Ver, y con botones de 24 px en la barra. El tirador también se redimensiona con el teclado (flechas).
- Un test unitario recorre todos los pares token-fondo de cada tema y comprueba los ratios WCAG (ROADMAP H1.2).

## 7. Cómo se prueba la accesibilidad

**Automático (CI, cada PR):**
- Test QML que recorre el árbol de items de cada pantalla y falla si un item enfocable no tiene `Accessible.name` o tiene rol `NoRole`.
- Test de orden de foco: simula Tab y Shift+Tab y compara la secuencia con la esperada en cada pantalla.
- Test de contraste de los tokens (§6).
- Test de teclado: cada comando del `CommandRegistry` se puede ejecutar sin ratón (atajo o paleta).
- Linux: un test con `QT_ACCESSIBILITY=1` y AT-SPI (`dbus-run-session` + `at-spi2-registryd`) comprueba que el árbol accesible expone los nombres esperados en las pantallas principales.

**Manual (al cerrar cada hito que toque UI), lista de comprobación en `docs/a11y-checklist.md`:**

| Lector | Plataforma | Comprobaciones |
|---|---|---|
| NVDA (última estable) | Windows 11 | Recorrer la app con Tab y F6; árbol (nivel, plegado/desplegado, posición); grid (cabecera + valor por celda); anuncios de ejecución y errores; diálogos |
| Narrator | Windows 11 | Lo mismo, por cubrir UIA sin NVDA |
| VoiceOver | macOS 14+ | Rotor (encabezados, controles), VO+flechas en el grid, menús globales |
| Orca | Ubuntu 24.04 (GNOME) | Mismas rutas que NVDA |

También en cada hito de UI: modo de alto contraste de Windows, zoom de texto del 200 %, "reducir movimiento" activado y navegación solo con teclado de punta a punta (crear conexión → explorar → ejecutar → cancelar → copiar resultados). Las incidencias encontradas se apuntan en el ROADMAP del hito; el hito no se cierra con bloqueos de accesibilidad.

**Grid de resultados:** es el componente de mayor riesgo, porque Qt Quick no implementa `QAccessibleTableInterface` (ARCHITECTURE §13). Su spike en H1.7 incluye la prueba con los tres lectores antes de dar el subhito por cerrado.
