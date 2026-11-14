# Rendimiento de Quarry

La ligereza es la primera prioridad. Cada presupuesto tiene una medida automatizada; una regresión que se salga del presupuesto bloquea el hito.

## 1. Máquinas de referencia

Los runners compartidos de GitHub tienen mucho ruido, así que hay dos niveles:
- **Absoluto**: se mide en la máquina de referencia de desarrollo (Windows 11 x64, 16 GB de RAM, SSD; la máquina actual) al cerrar cada hito, y en un Mac con Apple Silicon y una VM con Ubuntu 24.04 cuando estén disponibles. Los valores se apuntan en `perf/results/<hito>-<os>.json`.
- **Regresión relativa**: en la CI, en cada PR, comparando con `perf/baseline-<os>.json` de `main`. Falla si un indicador empeora más que el margen (§3).

## 2. Presupuestos

| Indicador | Presupuesto | Condiciones |
|---|---|---|
| Arranque en caliente → primer frame interactivo | **≤ 400 ms** | Sin conexiones abiertas, caché de disco caliente. Medido en H1.2: 366 ms (mediana), con un suelo de ~320–335 ms para una ventana vacía de Qt Quick en la máquina de referencia |
| Arranque en frío | **≤ 1 s** | Primer arranque tras reiniciar |
| RAM en reposo (ventana principal, sin conexiones) | **≤ 80 MB** | *Private working set* (Windows) / RSS (macOS, Linux) |
| RAM con 1 conexión, árbol desplegado y 1 pestaña con 500 filas | **≤ 120 MB** | |
| Primer batch visible (500 filas) | **≤ 150 ms** | Desde que la consulta empieza a devolver filas, SQLite local |
| "Fetch all" de 1 M filas × 10 columnas (mixtas: int, double, text 20–50 B, timestamp, NULLs) | **≤ 3 s** | SQLite local; PostgreSQL en localhost ≤ 5 s |
| RAM con 1 M × 10 cargadas | **≤ 250 MB** sobre el reposo | Almacenamiento columnar |
| Tiempo de frame durante el streaming y el scroll | p95 **≤ 16 ms**, máximo **≤ 50 ms** | Ningún bloqueo de la UI perceptible |
| Cancelación | **≤ 200 ms** (SQLite), **≤ 500 ms** (PostgreSQL) | Desde que se pulsa hasta el estado "cancelled" |
| Expandir un nodo del árbol con 10 000 hijos | ≤ 300 ms hasta verlos, sin frames > 50 ms | |
| Teclear en el editor (10 000 líneas) | p95 ≤ 16 ms por tecla | Con resaltado |
| Carga de un plugin de provider | ≤ 50 ms | `QPluginLoader::load()` + `createProvider()` |
| Tamaño del paquete | Windows ≤ **45 MB** (instalador), macOS ≤ **60 MB** (dmg universal), Linux ≤ **70 MB** (AppImage) | Qt mínima (§4) |

## 3. Cómo se mide

**`tools/bench/`** contiene ejecutables y scripts que escriben JSON (`{"metric": "...", "value": ..., "unit": "ms"}`):

| Bench | Qué hace |
|---|---|
| `quarry --bench-startup` | Mide desde el inicio del proceso (marca del sistema) hasta el primer `QQuickWindow::frameSwapped` con la ventana interactiva, imprime el valor y sale. Un script lo lanza 10 veces y toma la mediana; en frío, una vez tras vaciar la caché (solo en la máquina de referencia). |
| `bench_memory` (script) | Arranca `quarry --bench-idle`, espera 3 s, lee el *private working set* / RSS (Windows: `GetProcessMemoryInfo`; macOS: `task_info`; Linux: `/proc/self/status`) y sale. |
| `bench_core` (Qt Test, `QBENCHMARK`) | Streaming de 1 M filas desde una fixture SQLite generada por el propio bench (determinista, con semilla). Mide el tiempo y el pico de RSS del núcleo, sin UI. |
| `bench_grid` (Qt Quick Test) | Carga 1 M filas en `ResultTableModel`, hace scroll programático por todo el rango y registra los tiempos de frame (`QQuickWindow::afterFrameEnd`). Devuelve el p95 y el máximo. |
| `bench_tree` / `bench_editor` | Árbol con 10 000 hijos (provider fake) y editor con 10 000 líneas y escritura simulada. `bench_editor` teclea 300 caracteres al principio, en medio y al final de 10 010 líneas con resaltado, y da p50, p95 y máximo por tecla. Debe ejecutarse en la plataforma nativa, no en `offscreen` (el renderizado por software multiplica por ~3 las cifras). `BENCH_VERBOSE=1` muestra el coste por posición y por tecla. |
| `bench_package` | Tamaño de los artefactos del job de empaquetado (H10). |

**CI:**
- Job `perf` (Linux, Windows y macOS) en cada PR a `main`. Compila en Release, ejecuta `bench_core`, `bench_grid` (offscreen en Linux), `--bench-startup` y `bench_memory`, y sube los JSON como artefacto.
- `scripts/perf_compare.py` compara con `perf/baseline-<os>.json`. Márgenes en la CI: tiempo **+25 %**, memoria **+10 %**, tamaño del paquete **+5 %**. Al cerrar un hito se actualiza la línea base con un commit explícito (`perf: update baseline`).
- Los presupuestos absolutos (§2) se comprueban en la máquina de referencia al cerrar cada hito, y se anotan en ROADMAP.md.

## 4. Reglas que mantienen los presupuestos

- **Módulos de Qt al mínimo**: Core, Gui, Qml, Quick, QuickControls2 (estilo Basic), ShaderTools, y DBus en Linux. Nada de WebEngine, Multimedia, 3D, Widgets (salvo que lo exija un diálogo nativo concreto, y entonces habría que justificarlo) ni Network en `app`.
- El despliegue (`windeployqt` …) se hace con `--no-translations` de los módulos que no se usan, sin el plugin de `qtvirtualkeyboard` y sin plugins de imagen que no se usen.
- Nada se carga en el arranque que no sea necesario para el primer frame: ni providers, ni KSyntaxHighlighting, ni la base de datos de la app de forma síncrona.
- QML compilado de antemano (qmlcachegen); sin `Loader` dentro de los delegates del grid; delegates reutilizados (`reuseItems`).
- Resultados en columnas; `QString` solo para las celdas visibles; sin `QVariant` por celda en almacenamiento.
- Ninguna operación en el hilo de UI tarda más de 8 ms. Si algo puede tardar más, va a un hilo de trabajo.
