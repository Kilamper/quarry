# Roadmap de Quarry

Hitos pequeños y verificables. Al terminar cada uno: compilar en los tres sistemas (CI), pasar los tests, comprobar los criterios de aceptación y los presupuestos de [PERFORMANCE.md](PERFORMANCE.md), actualizar este archivo y **esperar el visto bueno** antes del siguiente.

Estados: `pendiente` · `en curso` · `hecho` · `bloqueado`

| Hito | Contenido | Estado |
|---|---|---|
| F0 | Entorno de desarrollo (Windows verificado) | hecho |
| H0 | Esqueleto + CI en los tres sistemas | hecho (2026-11-12) |
| H1 | Versión mínima (subhitos 1.1–1.8) | en curso (1.1–1.3 hechos) |
| H2 | Edición de datos | pendiente |
| H3 | Autocompletado | pendiente |
| H4 | Historial y consultas guardadas | pendiente |
| H5 | Exportación | pendiente |
| H6 | MySQL / MariaDB | pendiente |
| H7 | SQL Server + ODBC genérico | pendiente |
| H8 | Túneles SSH | pendiente |
| H9 | EXPLAIN visual | pendiente |
| H10 | Distribución | pendiente |
| — | Oracle (ODPI-C) | sin fecha |

---

## H0 — Esqueleto del proyecto

**Objetivo:** estructura `core` / `providers` / `app` que compila y pasa los tests en la CI de Windows, macOS y Linux, reutilizando los scripts de la FASE 0.

- Renombrar el proyecto a `quarry`: carpeta, `project()`, venv de aqt, docs. Añadir `LICENSE` (Apache-2.0) y `NOTICE`.
- `vcpkg.json` con `builtin-baseline` y las dependencias de H1 (`sqlite3`, `libpq`, `qtkeychain-qt6`, `syntax-highlighting`), más `vcpkg-overlays/` (shim de `qtbase`/`qttools` + triplets `*-quarry`) según ARCHITECTURE §9.
- Targets: `quarry_provider_api` (INTERFACE), `quarry_core` (Qt6::Core), `quarry_provider_dummy` (MODULE de prueba), `quarry` (exe con una ventana vacía y el estilo Basic propio).
- `cmake/QuarryHelpers.cmake`: `quarry_add_provider()`, `quarry_add_test()`, warnings estrictos (`/W4 /permissive-` · `-Wall -Wextra -Wpedantic`), warnings como error en la CI.
- `.clang-format`, `.clang-tidy` (subconjunto razonable) y `.editorconfig`.
- GitHub Actions `ci.yml`: matriz `windows-2022`, `macos-15` (arm64; `macos-14` ya retirada), `ubuntu-24.04`. Pasos: `scripts/setup.* --non-interactive` → `cmake --preset <os>-release` → build → `ctest`. Caché de aqt y de vcpkg (binary caching con `actions/cache`).
- Repo privado en GitHub (`gh repo create`, **con confirmación antes del push**).

**Criterios de aceptación** (CI en verde: run 43319872951, commit `1545348`)
- [x] `ctest --preset <os>-debug` pasa en local (Windows, 3/3 con warnings como errores) y la CI está en verde en Windows (`windows-2022`), macOS (`macos-15`, arm64) y Linux (`ubuntu-24.04`, con clang-tidy), más el job de clang-format.
- [x] Test: `ProviderRegistry` lista el plugin dummy por metadatos sin cargarlo y lo carga al pedir `provider("dummy")` (`tst_provider_registry`, que además rechaza un plugin con API `/2`).
- [x] Test de arquitectura: `quarry_assert_no_ui_deps()` hace fallar la configuración si `quarry_core` enlaza Gui/Qml/Quick/Widgets.
- [x] `quarry --smoke` sale con 0 y sin warnings de QML en los tres sistemas (offscreen en la CI; nativo en Windows). La detección de warnings se comprobó inyectando uno: sale con 2.
- [x] QtKeychain y KSyntaxHighlighting compilan con vcpkg contra Qt 6.12 de aqt en los tres sistemas. En `vcpkg_installed` solo está el shim vacío de `qtbase`.

**Desviaciones**
- La carpeta local sigue llamándose `dbmanager`: no se pudo renombrar porque la sesión de trabajo la tenía abierta. Todo lo demás ya usa `quarry`.
- El repo `Kilamper/quarry` es **público** desde el 2026-11-12 (estaba previsto como privado). La historia se reescribió con el email noreply antes de publicarlo.
- Ejecutables en la raíz del build en todas las plataformas: en macOS `src/app/quarry` chocaba con el directorio `src/app/Quarry` del módulo QML (`errno=21`).
- CI: `macos-14` ya está retirada (se usa `macos-15`); `setup.sh` instala autotools (libgpg-error ← libsecret ← QtKeychain).
- Triplet de Windows: se antepone `$QT_ROOT_DIR/bin` al PATH en los ports que usan Qt, porque `katehighlightingindexer` fallaba con `0xC0000135` (falta de DLL).

---

## H1 — Versión mínima

Se divide en subhitos y cada uno se puede revisar por separado. Al final de H1, el usuario puede conectarse a SQLite y PostgreSQL, explorar el esquema, escribir SQL con resaltado, ejecutarlo de forma asíncrona y cancelable, y ver millones de filas sin que la UI se bloquee.

### H1.1 — Núcleo de ejecución + provider SQLite · **hecho (2026-11-13)**
- `Result<T>`, `Error`, `CancellationToken`, `ResultBatch`/`ColumnBuffer`, interfaces de provider (ARCHITECTURE §3–4).
- `SessionWorker` / `SessionManager` (§5), señales `columnsReady`/`batchReady`/`finished`/`failed`.
- Provider SQLite: abrir archivo y `:memory:`, `children()` (tablas, vistas, columnas, índices, FKs), streaming, `sqlite3_interrupt`, dialecto con `splitStatements`.
- Suite de conformidad de providers (`tests/providers/conformance/`).

Criterios:
- [x] Conformidad SQLite en verde (`tst_sqlite_provider`: 18 casos de conformidad + 17 específicos): tipos, NULLs, 0 filas, columnas mixtas, parámetros, errores con `position` (también tras caracteres no ASCII), `splitStatements` (comentarios, comillas, triggers). `execute` rechaza varias sentencias con `Unsupported`.
- [x] Cancelar un `WITH RECURSIVE` infinito devuelve `Cancelled` en < 200 ms y la conexión sigue usable. Una cancelación tardía con la sesión inactiva no aborta la siguiente sentencia.
- [x] `SELECT` de 1 M de filas en batches de 500 a través de `Session` y el plugin real: el hilo principal sigue procesando eventos (hueco máximo de 5 ms). Tarda 289 ms en Release.
- [x] Bench (`bench_core`, Release, máquina de referencia): **442 ms** (2,26 M filas/s) y **17 MB** de pico de RSS, frente a ≤ 1,5 s y ≤ 200 MB. Los batches se descartan al leerlos; la RAM con 1 M filas cargadas se mide en H1.7.
- Sin probar: SQLite con la base bloqueada por otra conexión (la traducción a `Timeout`) ni conexiones concurrentes. KSyntaxHighlighting no tiene definición específica de SQLite; se usa "SQL".

### H1.2 — Shell de la aplicación, tema y componentes base · **hecho (2026-11-13)**
- Ventana principal: barra lateral, área de pestañas, panel de resultados y barra de estado (UX.md §3).
- `ThemeManager`: claro, oscuro y alto contraste, siguiendo `QStyleHints::colorScheme` y `accessibility()->contrastPreference/motionPreference`, con posibilidad de forzarlos desde los ajustes.
- Componentes del estilo propio (`Quarry.Controls`): `QButton`, `IconButton`, `QTextField`, `QComboBox`, `QCheckBox`, `QDialog`, `QMenu`, `QToolTip`, `QSplitView`, `QTabBar`, `FocusRing`, `Heading`, con foco visible y `Accessible.*`.
- Paleta de comandos (Ctrl/Cmd+K) con `CommandRegistry`.
- i18n: `qt_add_translations`, `quarry_es.ts`.

Criterios:
- [x] Test QML (`tests/app/A11y.qml`, usado en `tst_controls` y `tst_mainwindow`): todo `Item` enfocable tiene `Accessible.name` y un rol distinto de `NoRole`, en la ventana completa y también con la paleta y los ajustes abiertos.
- [x] Test de contraste (`tst_theme_contrast`): texto ≥ 4.5:1 (el mínimo medido es 5.22) e indicadores ≥ 3:1 (mínimo 4.19), en los cuatro temas fijos.
- [x] El tema cambia en caliente con el sistema (esquema de color, alto contraste, movimiento reducido) y se puede forzar en Ajustes. El alto contraste real de Windows solo se ha probado con la paleta simulada en tests.
- [x] Toda la ventana se recorre con Tab, Shift+Tab y F6/Shift+F6, más Mod+1/2/3, sin trampas de foco. Los diálogos atrapan el foco y lo devuelven al cerrarse.
- [x] Arranque en caliente: mediana de **366 ms** (`--bench-startup`, Release, 7 ejecuciones) frente a ≤ 400 ms. Una ventana vacía de Qt cuesta ~320–335 ms en esta máquina, así que el margen es pequeño (ver PERFORMANCE.md).
- Además: paleta de comandos (Mod+K / Mod+Shift+P), Ajustes (Mod+,), comandos para mostrar y ocultar paneles y restablecer el diseño, interfaz en español (79 textos) con cambio de idioma en caliente.
- Sin probar: la pasada manual con lectores de pantalla (`docs/a11y-checklist.md`) y si se anuncia bien el elemento activo de la paleta.

### H1.3 — Conexiones y credenciales · **hecho (2026-11-13)**
- `AppDatabase` (`quarry.db`, migraciones) y `ConnectionStore`.
- Diálogo de conexión generado a partir de `connectionFields()`, con la sección avanzada plegada y el botón "Test connection".
- `CredentialStore` (QtKeychain) con "Save password" o "Ask every time".
- Color por conexión y marca de producción (rojo + icono + texto "PRODUCTION").
- Estado vacío: "No connections yet: create one or open a SQLite file", con botones.

Criterios:
- [x] Tras guardar una conexión, `quarry.db` no contiene la contraseña (`tst_connection_store::secretsAreNeverWritten` busca la cadena en el archivo; `tst_connection_controller` comprueba que los campos secretos se eliminan antes de guardar).
- [x] La contraseña se escribe y se lee del llavero real en Windows (Credential Manager, `QUARRY_TEST_KEYCHAIN=1`) y en Linux (CI, `gnome-keyring` headless). macOS: pendiente de la prueba manual.
- [x] Un fallo de conexión muestra un mensaje claro, el detalle técnico plegado y lo anuncia al lector de pantalla (`tst_connections.qml`).
- Además: lista de conexiones con estado vacío, diálogo generado desde `connectionFields()` (sección avanzada plegada, "Test connection" cancelable), color por conexión, marca PRODUCTION (texto + icono) que exige escribir el nombre para borrar, duplicar, abrir archivo SQLite (Mod+O), todo con teclado y recorrido por el walker de accesibilidad. 160 textos en español.
- Arranque: el escaneo de providers y el hilo del store salen del camino de arranque (después del primer frame). La mediana medida fue 786 ms con la máquina cargada por otro proceso; el árbol anterior medía lo mismo con la misma carga (768 vs 770 ms). **Pendiente** repetir `--bench-startup` en una máquina tranquila (objetivo ≤ 400 ms).
- Sin probar: la pasada manual con lectores de pantalla del diálogo de conexión; compilación local en Linux/macOS (solo CI). El área desplazable del diálogo no tiene barra de desplazamiento visible.

### H1.4 — Árbol de esquemas bajo demanda
- `SchemaTreeModel` (QAbstractItemModel) con `canFetchMore`/`fetchMore` por nodo y una sesión de metadatos propia.
- Nodos: conexión → base de datos → esquema → carpetas (Tables, Views, …) → objeto → columnas/índices/FKs.
- Teclado: flechas, ←/→ para plegar y desplegar, Enter para abrir, búsqueda escribiendo, menú contextual (Shift+F10 / tecla Menú).

Criterios:
- [ ] `QAbstractItemModelTester` sin errores.
- [ ] Desplegar un esquema con 10 000 tablas no bloquea la UI (ningún frame > 50 ms) y muestra el indicador "Loading…" accesible.
- [ ] El lector de pantalla anuncia el nivel, el estado plegado/desplegado y la posición ("Tables, expanded, 3 of 5").

### H1.5 — Editor SQL
- Editor sobre `TextEdit` con KSyntaxHighlighting (`QQuickTextDocument`) y la definición del dialecto.
- Números de línea, sentencia actual resaltada, Ctrl/Cmd+Enter (sentencia actual) y Ctrl/Cmd+Shift+Enter (todo).
- Pestañas de editor (Ctrl/Cmd+T, Ctrl/Cmd+W) asociadas a una conexión.

Criterios:
- [ ] Resaltado correcto para SQLite y PostgreSQL (`$$…$$`, comentarios anidados).
- [ ] Teclear en un archivo de 10 000 líneas no supera 16 ms por tecla (p95).
- [ ] El lector de pantalla lee el texto del editor y el número de línea.

### H1.6 — Ejecución asíncrona y cancelable en la UI
- Barra de estado: `Running… 00:03`, botón Cancel (Esc / Ctrl/Cmd+.), filas y tiempo al acabar.
- Panel de mensajes: errores con `position` subrayada en el editor, notices y filas afectadas.
- `SqlSafety`: confirmación para `UPDATE`/`DELETE` sin `WHERE`, y para `DROP`/`TRUNCATE` en conexiones de producción.

Criterios:
- [ ] La UI responde durante la consulta: se puede cambiar de pestaña, desplegar el árbol y cancelar.
- [ ] El inicio y el fin de la ejecución se anuncian (`Accessible.announce`) sin robar el foco.
- [ ] Tests de `SqlSafety`: detecta `DELETE FROM t`, `delete from t -- where`, y no da falso positivo con `DELETE … WHERE` ni con CTE + `WHERE`.

### H1.7 — Grid de resultados virtualizado
- `ResultTableModel` sobre `ResultBatch` (texto solo en `data()`), `fetchMore` de 500 filas y "Fetch all".
- `QuarryGrid`: `TableView` con cabeceras fijas, selección por celda, rango o fila, copiar (TSV), ajuste de ancho de columna y navegación completa con teclado.
- **Spike de accesibilidad** (ARCHITECTURE §13): `QAccessibleTableInterface` propio. Si no es viable en el plazo del subhito, se pasa al plan B con `announce`, y la decisión se documenta.
- NULL se muestra como texto "NULL" en cursiva y atenuado (no solo con color); los BLOB como `<binary 2.3 KB>`.

Criterios:
- [ ] Bench: 1 M filas × 10 columnas (SQLite local). Primer batch visible ≤ 150 ms; "Fetch all" ≤ 3 s; RAM ≤ 250 MB; scroll continuo sin frames > 33 ms (p95 ≤ 16 ms).
- [ ] NVDA, VoiceOver y Orca anuncian la cabecera de columna y el valor al moverse por las celdas (lista de comprobación manual de UX.md §7).
- [ ] `QAbstractItemModelTester` sin errores durante el streaming.

### H1.8 — Provider PostgreSQL
- libpq 18: `PQsendQueryParams` + `PQsetChunkedRowsMode`, cancelación con `PQcancelCreate`/`PQcancelBlocking`, SSL (`sslmode`), introspección por `pg_catalog`.
- Tipos: int2/4/8, numeric (texto exacto), float, bool, text/varchar, bytea, date/time/timestamp(tz), uuid, json/jsonb, arrays (como texto en v1).
- CI: contenedor de servicio `postgres:17` en el job de Linux; la conformidad corre allí. En Windows y macOS se compila y solo corren los tests unitarios.

Criterios:
- [ ] Conformidad PostgreSQL en verde en la CI.
- [ ] Cancelar `SELECT pg_sleep(60)` vuelve en < 500 ms y la conexión queda usable.
- [ ] El error de sintaxis marca la posición correcta en el editor.

---

## H2 — Edición de datos
- Edición en el grid de tablas con PK (o con ROWID/ctid y aviso), insertar y borrar filas.
- Los cambios se acumulan en local con indicador visual (icono + texto, no solo color). **Vista previa del SQL** generado antes de aplicar. Se aplica en una transacción.
- Deshacer/rehacer de cambios pendientes (Ctrl/Cmd+Z / Shift+Z).
- Conexiones de producción: confirmación reforzada (hay que escribir el nombre de la conexión).

Criterios: la vista previa coincide exactamente con lo que se ejecuta (test); fallo a mitad → rollback y los cambios siguen pendientes; edición completa por teclado (F2/Enter/Esc/Tab).

## H3 — Autocompletado
- Palabras clave del dialecto, esquemas, tablas, columnas (con alias), funciones; caché de metadatos por sesión, invalidable.
- Popup accesible (rol lista; se anuncia el número de sugerencias); Ctrl+Space.

Criterios: sugerencias en ≤ 50 ms con 10 000 tablas en caché; nunca bloquea la escritura.

## H4 — Historial y consultas guardadas
- `query_history` con FTS5, búsqueda, filtro por conexión, re-ejecutar o copiar al editor. Consultas guardadas con carpetas.
- Opción de no guardar el historial por conexión (datos sensibles).

## H5 — Exportación
- CSV, TSV, JSON, JSON Lines, SQL `INSERT`, Markdown. Del resultado actual o en streaming directo desde la consulta (sin cargarla en el grid).

Criterios: exportar 10 M de filas en streaming con RAM constante (≤ +50 MB); se puede cancelar.

## H6 — MySQL / MariaDB
- MariaDB Connector/C: streaming con `mysql_use_result`, `KILL QUERY` para cancelar, `DELIMITER` en `splitStatements`. Conformidad en la CI con los servicios `mariadb:11` y `mysql:8.4`.

## H7 — SQL Server + ODBC genérico
- Provider ODBC (unixODBC / iODBC / Windows ODBC); SQL Server como perfil con su dialecto (T-SQL, `GO`); ODBC genérico con capacidades reducidas. Conformidad con `mcr.microsoft.com/mssql/server` en Linux.

## H8 — Túneles SSH
- libssh2: contraseña, clave y agente; `known_hosts` con confirmación de la huella. Credenciales en el llavero. Port forwarding local en un hilo propio.

## H9 — EXPLAIN visual
- PostgreSQL (`EXPLAIN (FORMAT JSON, ANALYZE opcional)`), MySQL (`EXPLAIN FORMAT=JSON`), SQLite (`EXPLAIN QUERY PLAN`). Árbol accesible con coste y filas; vista de texto alternativa.

## H10 — Distribución
- Windows: MSIX + instalador Inno Setup, firmado. macOS: `.app` universal (arm64 + x86_64 con `lipo`), `.dmg` firmado y **notarizado**. Linux: AppImage (construido en Ubuntu 24.04) + Flatpak.
- `windeployqt` / `macdeployqt` / `linuxdeploy` con la lista mínima de módulos; avisos de licencia LGPL de Qt y licencias de terceros en "About".

Criterios: tamaño de los paquetes dentro del presupuesto; instalación y arranque limpios en máquinas virtuales de las versiones mínimas (Win10 1809, macOS 14.4, Ubuntu 24.04).

## Sin fecha
- Oracle (ODPI-C; requiere Instant Client instalado por el usuario).
- Plugins de terceros con SDK estable.
- Windows arm64 y Linux arm64.
