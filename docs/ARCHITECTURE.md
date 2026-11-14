# Arquitectura de Quarry

Quarry es un gestor de bases de datos de escritorio, nativo y multiplataforma. Sus prioridades, por orden: **1) ligereza y rendimiento**, **2) interfaz moderna y accesible (WCAG AA, teclado, lectores de pantalla)**, **3) facilidad para añadir motores mediante plugins**.

## 1. Decisiones cerradas

| Tema | Decisión |
|---|---|
| Nombre | **Quarry**: namespace `quarry`, binario `quarry`, ID `dev.quarry.Quarry` |
| Licencia | Apache-2.0 (código propio). Qt LGPLv3 enlazado dinámicamente |
| Plataformas mínimas | Windows 10 1809+ / 11 (x64), macOS 14.4+ (arm64 + x86_64), Linux x64 con glibc ≥ 2.39 (Ubuntu 24.04+, Debian 13+, Fedora 40+). Es el suelo de los binarios de Qt 6.12; Qt 6.12 es la última versión que soporta Windows 10 |
| Qt | 6.12 LTS (`.qt-version`) instalado con aqt. Módulos: Core, Gui, Qml, Quick, QuickControls2, ShaderTools; en Linux también DBus (lo pide QtKeychain) |
| Motores | Hito 1: SQLite (compilado dentro) y PostgreSQL (libpq). Después: MySQL/MariaDB (MariaDB Connector/C) y SQL Server/ODBC genérico. Oracle (ODPI-C) sin fecha |
| Tests | Qt Test (C++) + Qt Quick Test (QML) + `QAbstractItemModelTester` |
| Plugins | Plugins Qt (`QPluginLoader` + `Q_PLUGIN_METADATA`), cargados solo al usarse |
| Persistencia propia | SQLite de la app (`quarry.db`) para conexiones, historial y estado; preferencias simples en `QSettings` |
| Idiomas | Textos fuente en inglés (`qsTr`/`tr`) + traducción al español (`.ts`) |
| Estilo visual | Estilo propio sobre Qt Quick Controls **Basic**, con tokens de tema |
| Iconos | Lucide (ISC) como fuente recortada; la copia original fijada está en `third_party/lucide/` (lucide-static 1.72.0) |
| Telemetría | Ninguna. Sin conexiones salientes salvo las que el usuario abre hacia sus bases de datos |
| Dependencias | vcpkg (manifiesto). Las librerías que dependen de Qt (QtKeychain, KSyntaxHighlighting) se compilan contra la Qt de aqt con overlay ports *shim* (§9) |

## 2. Capas y módulos

```
┌──────────────────────────── app (ejecutable quarry) ────────────────────────────┐
│ QML (vistas, componentes del estilo propio)                                     │
│ ViewModels / modelos C++: ConnectionListModel, SchemaTreeModel, ResultTableModel│
│ ThemeManager, CommandRegistry, AccessibleGrid (QAccessibleTableInterface)        │
└───────────────▲────────────────────────────────────────────────────────────────┘
                │ señales/slots (Qt::QueuedConnection) — solo tipos de core
┌───────────────┴──────────── core (quarry_core, solo Qt6::Core) ────────────────┐
│ Interfaces de provider · ProviderRegistry · Session/QueryRunner (hilos)        │
│ ResultBatch (columnar) · Result<T>/Error · SqlSafety · ConnectionStore (SQLite)│
│ CredentialStore (QtKeychain) · AppDatabase (migraciones)                        │
└───────────────▲────────────────────────────────────────────────────────────────┘
                │ IProviderPlugin (QPluginLoader, carga diferida)
┌───────────────┴──────────── providers/* (un plugin MODULE por motor) ──────────┐
│ sqlite (sqlite3 dentro)   postgres (libpq)   mariadb   odbc   …                 │
└────────────────────────────────────────────────────────────────────────────────┘
```

Reglas de dependencia (las verifica CMake: cada target enlaza solo lo permitido):
- `quarry_core` enlaza solo `Qt6::Core`, `sqlite3` (base de datos de la app) y `qt6keychain`. Nunca Gui/Qml/Quick.
- `providers/*` enlazan `quarry_core` (solo los headers de la API de provider, `quarry_provider_api`) y su driver. Nunca `app`.
- `app` enlaza `quarry_core` y Qt Quick. **Nunca enlaza un provider**: solo los conoce por `ProviderRegistry`.

### Estructura de carpetas

```
src/
  core/            quarry_core: include/quarry/… (API pública), src/…
  core/api/        quarry_provider_api: headers de la interfaz de provider (INTERFACE lib)
  providers/
    sqlite/        quarry_provider_sqlite (MODULE)
    postgres/      quarry_provider_postgres (MODULE)
  app/             ejecutable quarry: main.cpp, viewmodels/, qml/, i18n/
    qml/Quarry/Controls/   componentes del estilo propio (QButton, QTextField, …)
    qml/Quarry/Views/      pantallas
tests/
  core/  providers/  app/ (Qt Quick Test)  fixtures/
tools/
  env-check/  bench/
cmake/  scripts/  docs/  vcpkg-overlays/{ports,triplets}/
```

## 3. Tipos base de `core`

C++20 no tiene `std::expected` (llega en C++23), así que `core` define un `quarry::Result<T>` mínimo con la misma forma (`has_value()`, `value()`, `error()`, `operator bool`), para poder sustituirlo por `std::expected` sin tocar los usos. **No se lanzan excepciones a través de la frontera del plugin.**

```cpp
namespace quarry {

enum class ErrorKind : uint8_t {
    Connection, Authentication, Timeout, Cancelled, Syntax, Permission,
    Constraint, NotFound, Unsupported, Driver, Internal
};

struct Error {
    ErrorKind kind = ErrorKind::Internal;
    QString message;            // lenguaje claro, traducible: "Wrong user name or password"
    QString detail;             // técnico, desplegable: texto literal del driver
    QString sqlState;           // "28P01", si existe
    std::optional<int> position;// posición en el SQL (para subrayar en el editor)
};

template <typename T> class Result;   // ≈ std::expected<T, Error>

// Cancelación cooperativa: la consultan los bucles largos dentro del provider.
class CancellationToken {
public:
    bool isCancelled() const noexcept;   // std::atomic<bool>, sin bloqueo
};
}
```

### Resultados: datos crudos en columnas

`ResultBatch` guarda N filas en formato **columnar**. Nada se convierte a texto hasta pintar la celda.

```cpp
namespace quarry {

enum class ValueType : uint8_t { Null, Bool, Int64, Double, Decimal, Text, Bytes,
                                 Date, Time, DateTime, DateTimeTz, Uuid, Json, Other };

struct ColumnInfo {
    QString name;
    ValueType type;          // tipo lógico normalizado
    QString nativeType;      // "int4", "varchar(255)", …
    bool nullable;
    std::optional<ObjectPath> sourceTable; // necesario para la edición (Hito 2)
};

class ColumnBuffer {          // un buffer por columna
    // fijos: std::vector<int64_t>/<double>/…; variables: arena de bytes UTF-8 + offsets (uint32)
    // nulls: bitmap
public:
    bool isNull(qsizetype row) const;
    int64_t int64At(qsizetype row) const;
    double doubleAt(qsizetype row) const;
    QByteArrayView bytesAt(qsizetype row) const; // Text/Decimal/Json/Bytes/Other: vista sin copia
};

struct ResultBatch {
    qsizetype rowCount = 0;
    std::vector<ColumnBuffer> columns;    // columns.size() == número de columnas
};
}
```

**Columnas heterogéneas** (decisión del 2026-11-12): un `ColumnBuffer` empieza tipado (solo el almacén de su tipo + bitmap de NULLs). Si llega un valor de otro tipo (en SQLite el tipo es por valor; también pasa con `UNION`, expresiones o columnas sin tipo declarado), el buffer pasa a **modo mixto**: reserva un vector de 1 byte por fila con el `ValueType` de cada celda y guarda cada valor en el almacén de su tipo (int64, double o bytes). No se pierde fidelidad (un BLOB sigue siendo BLOB) y solo cuesta +1 B/celda en las columnas mezcladas. `ColumnInfo::type` de una columna mixta vale `ValueType::Other`; el tipo real de cada celda se consulta con `ColumnBuffer::typeAt(row)`.

Motivo: con 1 M de filas, un `QVariant` por celda costaría unos 16 B más una reserva de memoria por string. En columnas, un `int64` ocupa 8 B, y los textos van a una arena contigua con un offset de 4 B por celda. `ResultTableModel::data()` convierte solo las celdas visibles, y para eso consulta el `ValueFormatter` del dialecto.

## 4. Interfaz de provider

Los providers implementan una **API síncrona y bloqueante**, siempre llamada desde un hilo de trabajo que gestiona `core`. Así escribir un plugin es sencillo: el autor no gestiona hilos ni event loops. La asincronía, el envío por lotes y la cancelación los resuelve `core` (§5). La única llamada que se hace desde otro hilo es `requestCancel()`, que debe ser thread-safe.

```cpp
namespace quarry {

struct ProviderInfo {
    QString id;                 // "postgres"
    QString displayName;        // "PostgreSQL"
    QVersionNumber version;
    Capabilities caps;          // flags: Schemas, MultipleDatabases, Transactions, Cancel,
                                //        Explain, ServerSideCursor, MultipleResultSets, Ssh, …
};

// Describe el formulario de conexión; app lo genera sin conocer el motor.
struct ConnectionField {
    QString key;                // "host", "port", "database", "file", "sslmode"…
    QString label;              // traducible
    enum class Kind { Text, Number, Path, Enum, Bool, Secret } kind;
    QVariant defaultValue;
    QStringList enumValues;
    bool required = false;
    bool advanced = false;      // se oculta en "Opciones avanzadas"
};

struct ConnectionConfig {
    QUuid id;
    QVariantMap values;         // sin secretos
};

struct Secrets {                // viene de CredentialStore, no se persiste
    QString password;
};

// Identifica un objeto del árbol: [database, schema, table, …] + tipo.
struct ObjectPath { QStringList parts; ObjectKind kind; };

enum class ObjectKind : uint8_t { Database, Schema, Table, View, MaterializedView,
                                  Column, Index, ForeignKey, Function, Procedure,
                                  Sequence, Trigger, Folder };

struct SchemaNode {
    ObjectPath path;
    QString name;
    bool hasChildren;           // pinta el triángulo sin cargar los hijos
    QString detail;             // "int4 NOT NULL", "≈ 12 345 rows"…
};

struct TableDetails {
    std::vector<ColumnDef> columns;
    std::vector<IndexDef> indexes;
    std::vector<ForeignKeyDef> foreignKeys;
    std::optional<QStringList> primaryKey;
};

struct Statement {
    QString sql;
    QVariantList parameters;    // posicionales; cada dialecto traduce los placeholders
    int fetchSize = 500;
};

class IResultStream {
public:
    virtual ~IResultStream() = default;
    virtual const std::vector<ColumnInfo>& columns() const = 0;   // vacío si no devuelve filas
    // Bloquea hasta tener maxRows filas o llegar al final. Un batch vacío con atEnd() == true marca el fin.
    virtual Result<ResultBatch> fetch(qsizetype maxRows, const CancellationToken&) = 0;
    virtual bool atEnd() const = 0;
    virtual std::optional<qint64> affectedRows() const = 0;       // UPDATE/DELETE/INSERT
    virtual QStringList notices() const = 0;                      // RAISE NOTICE, warnings
    // Varios result sets (SQL Server, MySQL CALL…). Devuelve false si no hay más.
    virtual Result<bool> nextResult(const CancellationToken&) = 0;
};

class IConnection {
public:
    virtual ~IConnection() = default;
    virtual Result<ServerInfo> open(const CancellationToken&) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Introspección bajo demanda: solo los hijos directos de `parent`.
    virtual Result<std::vector<SchemaNode>> children(const ObjectPath& parent,
                                                     const CancellationToken&) = 0;
    virtual Result<TableDetails> describe(const ObjectPath& table, const CancellationToken&) = 0;

    // Ejecución en streaming. El stream tiene la conexión ocupada hasta que se destruye.
    virtual Result<std::unique_ptr<IResultStream>> execute(const Statement&,
                                                           const CancellationToken&) = 0;

    // Thread-safe: se llama desde el hilo de UI mientras otro hilo está en execute()/fetch().
    // Debe hacer que la llamada bloqueada vuelva pronto con ErrorKind::Cancelled.
    virtual void requestCancel() = 0;

    // Transacciones explícitas (Hito 2, edición de datos)
    virtual Result<void> begin() = 0;
    virtual Result<void> commit() = 0;
    virtual Result<void> rollback() = 0;
};

class ISqlDialect {
public:
    virtual ~ISqlDialect() = default;
    virtual QString quoteIdentifier(QStringView) const = 0;      // "a""b" | `a``b` | [a]]b]
    virtual QString quoteLiteral(QStringView) const = 0;
    virtual QString highlightingDefinition() const = 0;          // "SQL (PostgreSQL)" de KSyntaxHighlighting
    virtual QStringList keywords() const = 0;                    // para autocompletado
    // Divide un script en sentencias: tiene en cuenta $$…$$, DELIMITER, comentarios y strings.
    virtual std::vector<StatementRange> splitStatements(QStringView script) const = 0;
    virtual QString limitClause(qint64 limit, qint64 offset) const = 0;
    virtual QString explainPrefix(ExplainMode) const = 0;        // Hito EXPLAIN
};

class IDatabaseProvider {
public:
    virtual ~IDatabaseProvider() = default;
    virtual ProviderInfo info() const = 0;
    virtual std::vector<ConnectionField> connectionFields() const = 0;
    virtual const ISqlDialect& dialect() const = 0;
    // Crea una conexión sin abrirla. No toca la red.
    virtual std::unique_ptr<IConnection> createConnection(const ConnectionConfig&,
                                                          const Secrets&) const = 0;
};

// Punto de entrada del plugin (QObject + Q_PLUGIN_METADATA)
class IProviderPlugin {
public:
    virtual ~IProviderPlugin() = default;
    virtual std::unique_ptr<IDatabaseProvider> createProvider() = 0;
};
}
#define QUARRY_PROVIDER_IID "dev.quarry.IProviderPlugin/1"
Q_DECLARE_INTERFACE(quarry::IProviderPlugin, QUARRY_PROVIDER_IID)
```

Prueba de conexión = `createConnection()` + `open()` + `close()`, con un timeout que aplica `core`.

**Ajustes de implementación (H1.1):** el código manda; esto resume en qué se aparta de las firmas de arriba.
- `ObjectKind`/`ObjectPath` están en `object_path.h` (los usan tanto `ColumnInfo` como la API de provider). `Capabilities` es `QFlags<Capability>`.
- `ColumnBuffer` guarda int64, bool y double en el mismo slot de 8 bytes (el double como patrón de bits), así que el modo mixto solo añade la etiqueta de 1 B por fila. `appendBytes()` devuelve `false` y guarda NULL si la arena del batch superase 4 GiB.
- Los errores se devuelven con `quarry::fail(Error)`. Un acceso indebido a `Result` (p. ej. `value()` sin valor) termina con `qFatal`, también en release.
- **`splitStatements`:** devuelve rangos semiabiertos `[begin, end)` en unidades UTF-16 sobre el texto original, sin espacios alrededor y **sin el `;` final**. Las sentencias vacías o que solo tienen comentarios se descartan. Todos los providers siguen esta convención (`tests/providers/conformance`).
- Una columna sin tipo declarado (expresiones, `UNION`) tiene `ColumnInfo::type == ValueType::Other`. En SQLite, `DECIMAL`/`NUMERIC` llegan siempre como texto exacto.
- **Contrato de cancelación:** `requestCancel()` es thread-safe y afecta solo a la llamada en curso. Cada `open/children/describe/execute/fetch` descarta al empezar cualquier cancelación pendiente, para que una cancelación tardía nunca aborte la sentencia siguiente.
- **`Session`:** `open()` y `execute()` se pueden encadenar. El `execute` espera en cola a que acabe la apertura y, si la apertura falla, termina con `failed()` ("The connection is not open."). Un `execute` nuevo cancela el `execute`/`fetchMore` en curso, pero nunca la apertura. `fetchMore` se ignora si la sesión está ocupada, y `cancel()` con la sesión inactiva no toca la conexión. `ExecutionStats` acumula tiempo y filas entre `execute` y los `fetchMore` siguientes.

### Cómo cumple cada motor la API

| Motor | Streaming | Cancelación |
|---|---|---|
| SQLite | `sqlite3_step` por fila hasta llenar el batch | `sqlite3_interrupt()` (thread-safe) |
| PostgreSQL | `PQsendQueryParams` + `PQsetChunkedRowsMode(fetchSize)` (libpq ≥ 17) | `PQcancelCreate` + `PQcancelBlocking` sobre una conexión de cancelación aparte |
| MariaDB | `mysql_use_result` + `mysql_fetch_row` | `KILL QUERY <id>` desde una conexión auxiliar |
| ODBC | `SQLFetch` con *row-wise binding* de bloques | `SQLCancel` (thread-safe por especificación) |

La contrapresión es natural: si el usuario no pide más filas, `core` no llama a `fetch()` y el socket deja de vaciarse (control de flujo TCP).

## 5. Modelo de hilos

```
Hilo UI (QGuiApplication)          Hilo de sesión (1 QThread por sesión)
──────────────────────────         ─────────────────────────────────────
QueryController.run(sql) ──Queued──▶ SessionWorker::execute()
                                       IConnection::execute()
                                       stream->fetch(500) ─┐
ResultTableModel.append() ◀─Queued── batchReady(batch)  ◀─┘
  (beginInsertRows/endInsertRows)
fetchMore() ──────────────Queued──▶ SessionWorker::fetchNext(500)
Cancel button ── directo ──────────▶ IConnection::requestCancel() (thread-safe)
                                     + CancellationToken.cancel()
```

- **Sesión** = una `IConnection` abierta + su `QThread` + `SessionWorker` (QObject movido al hilo). El `IConnection` solo se usa desde ese hilo, salvo `requestCancel()`.
- Cada conexión abierta en la interfaz tiene **una sesión de metadatos** (árbol de esquemas) y **una sesión por pestaña de editor**, que se crea al ejecutar por primera vez. Así una consulta larga no congela el árbol. En SQLite las sesiones abren el mismo archivo y cada pestaña tiene su propio `sqlite3*`.
- Los batches viajan por `Qt::QueuedConnection` como `std::shared_ptr<const ResultBatch>` (Q_DECLARE_METATYPE). El hilo de UI no copia datos.
- **Carga bajo demanda:** el primer batch es de 500 filas (configurable). `ResultTableModel::canFetchMore()/fetchMore()` pide el siguiente cuando la vista se acerca al final. *Fetch all* pide batches seguidos y la UI los agrupa en `beginInsertRows` cada ≤ 50 ms, para no saturar el event loop.
- **Límite de memoria:** si el resultado supera un umbral configurable (por defecto 2 M filas o 1 GB), se para el fetch y se avisa al usuario con la opción de seguir o de exportar en streaming.
- **Timeouts:** conexión 15 s (configurable). Las consultas no tienen timeout por defecto; se cancelan manualmente.
- Ninguna llamada de red ni de disco pesada en el hilo de UI. `ProviderRegistry` lee los metadatos de los plugins en el arranque sin cargarlos (§8), y `QPluginLoader::load()` se ejecuta en el hilo de sesión la primera vez que se usa el motor.

### 5.1 Contrato del árbol de esquemas (H1.4)

Ampliación de `core` (cabeceras en `src/core/include/quarry/`) que usa `SchemaTreeModel` (app):

```cpp
namespace quarry {
class ProviderRegistry {
    // Lo que hoy hace ConnectionTester, extraído para reutilizarlo. Carga el plugin si hace falta.
    // Errores: NotFound "This database engine is not available." (detail = motivo del loader);
    //          Internal "Could not prepare the connection." si el provider devuelve nullptr.
    Result<std::unique_ptr<IConnection>> createConnection(const ConnectionProfile &profile,
                                                          const Secrets &secrets);
};

class Session {
public slots:
    // Hijos directos de `parent` (IConnection::children). Se encola en orden con las demás
    // operaciones; no cancela un execute en curso. Antes de que termine open() espera a él;
    // si open() falló -> childrenFailed("The connection is not open."). Devuelve un id > 0.
    quint64 children(const quarry::ObjectPath &parent);
signals:
    void childrenReady(quint64 requestId, quarry::ObjectPath parent,
                       std::vector<quarry::SchemaNode> nodes);
    // También por cancel()/close(): cada petición pendiente o en curso termina con
    // childrenFailed(ErrorKind::Cancelled), para que la UI quite su "Loading…".
    void childrenFailed(quint64 requestId, quarry::ObjectPath parent, quarry::Error error);
};
}
Q_DECLARE_METATYPE(quarry::ObjectPath)
Q_DECLARE_METATYPE(std::vector<quarry::SchemaNode>)
```

- Cada conexión desplegada en el árbol tiene su `Session` de metadatos, propiedad del modelo. Desplegar conecta; plegar **no** desconecta; se desconecta con el comando "Disconnect" o al cerrar la app.
- Contraseña: `Saved` -> `CredentialStore::readPassword`; `AskEveryTime` -> diálogo que guarda la contraseña con `rememberForSession` (solo en memoria hasta cerrar la app); `None` -> sin secretos.
- `ConnectionTester` pasa a usar `ProviderRegistry::createConnection` (mismo comportamiento y mismos mensajes).
- `SchemaTreeModel::fetchMore` solo marca el nodo y encola la petición (cambiar el modelo dentro de `fetchMore` es ilegal cuando lo llama otra señal); la fila "Loading…" aparece en el siguiente ciclo del event loop. Las respuestas se identifican por (sesión, requestId) y las de una sesión cerrada se descartan.
- shortcut: `ProviderRegistry::provider()` carga el plugin en el hilo de UI, no en el de sesión como dice §5; se moverá cuando haya providers con carga lenta (H1.8).

## 6. Flujo de una consulta de principio a fin

1. El usuario pulsa **Ctrl/Cmd+Enter** en el editor. `QueryController` obtiene la sentencia bajo el cursor con `dialect().splitStatements()`.
2. **`SqlSafety`** analiza la sentencia con el tokenizador del dialecto. Si es `UPDATE`/`DELETE` sin `WHERE`, o `DROP`/`TRUNCATE` en una conexión marcada como producción, muestra un diálogo de confirmación con el texto de la sentencia. Escape cancela; para confirmar hay que pulsar un botón explícito, que no es el de por defecto.
3. `QueryController` pide la sesión de la pestaña a `SessionManager`, que la crea si no existe. Si la conexión necesita contraseña, la saca de `CredentialStore` en el hilo de sesión, porque QtKeychain hace E/S.
4. `SessionWorker::execute(Statement)` → `IConnection::execute()`, y después `fetch(500)`.
5. Señales hacia la UI: `started` (el estado pasa a "Running…" y se activa Cancel), `columnsReady(columns)`, `batchReady(batch)`, `finished(stats {elapsed, rows, affected, notices})` o `failed(Error)`.
6. `ResultTableModel` crea las columnas con `columnsReady` y añade filas con `batchReady`. La vista virtualizada solo crea delegates para las celdas visibles.
7. Si la vista se acerca al final, `fetchMore()` → `SessionWorker::fetchNext(500)`.
8. **Cancelar** (Esc en el editor o Ctrl/Cmd+.): `requestCancel()` se llama directamente desde el hilo de UI y además se marca el token. El provider devuelve `ErrorKind::Cancelled` y la UI muestra "Query cancelled", sin tratarlo como error.
9. Al cerrar la pestaña, se destruye el stream y después la sesión (`QThread::quit()` + `wait()` con timeout; si se pasa, `requestCancel()`).
10. La sentencia, la duración y el resultado (éxito o error) se guardan en el historial (`quarry.db`, Hito de historial).

### 6.1 Contrato de ejecución (H1.6)

Decisiones del 2026-11-14:
- **`SqlSafety` usa un lexer genérico de `core`**, no un tokenizador del dialecto: `ISqlDialect` no tiene `tokenize()` y añadirlo rompería la ABI de los plugins. El lexer reconoce comentarios (`--`, `/* */` anidados), cadenas `'…'` (con `''`), identificadores `"…"`, `` `…` `` y `[…]`, y `$tag$…$tag$`. Basta para SQLite, PostgreSQL y MySQL. `shortcut:` un dialecto con otra sintaxis de comentarios o cadenas puede dar falsos positivos o negativos; si llega uno, añadir un `tokenize()` opcional al dialecto.
- **Ejecutar todo** (Mod+Shift+Enter) ejecuta las sentencias en orden y **se para en el primer error**. Mensajes muestra el resultado de cada una ("Statement 2 of 5: 3 rows affected") y el error, subrayado en el editor.
- **Confirmación única:** antes de ejecutar nada, `SqlSafety` revisa todas las sentencias del plan. Si alguna es peligrosa, un solo diálogo las lista todas, con la conexión y su marca PRODUCTION. Confirmar ejecuta el plan completo; Esc lo cancela antes de empezar.
- **Resultados sin grid:** hasta H1.7, la pestaña Results muestra un resumen ("120 rows · 15 ms") y los nombres de las columnas. Se sigue leyendo el primer lote de 500 filas para probar el streaming y la cancelación.

```cpp
// src/core/include/quarry/sql_safety.h
namespace quarry {
enum class SqlRisk {
    None,
    UpdateWithoutWhere,   // en cualquier conexión
    DeleteWithoutWhere,   // en cualquier conexión
    Drop,                 // solo si production
    Truncate,             // solo si production
};

class SqlSafety {
public:
    // Una sentencia tal como la da splitStatements(). Solo cuenta el comando principal: tras un
    // WITH (CTE) se salta hasta el comando; DELETE/UPDATE necesitan un WHERE a profundidad de
    // paréntesis 0 detrás del comando (un WHERE dentro de un CTE o una subconsulta no vale).
    // Lo que va en comentarios, cadenas e identificadores entrecomillados se ignora.
    // DROP/TRUNCATE cuentan solo como comando principal (no `ALTER TABLE … DROP COLUMN`).
    static SqlRisk check(QStringView statement, bool production);
};
}
```

- `QueryController` (app) tiene una `Session` por pestaña del editor, aparte de la sesión de metadatos del árbol. Se crea en la primera ejecución (con la contraseña del llavero o la recordada en esta ejecución de la app, como el árbol) y se cierra al cerrar la pestaña, al cambiar su conexión o al borrar la conexión.
- Se engancha a `EditorTabsModel::runRequested(plan)`: aplica `SqlSafety` al plan, ejecuta cada rango con `Session::execute` (un `fetchSize` de 500, sin `fetchMore` hasta H1.7) y pasa al siguiente con `finished`. `failed` para el plan; `cancelled` también, y muestra "Query cancelled" sin tratarlo como error.
- Un plan nuevo en la misma pestaña mientras otro se ejecuta cancela el anterior (es lo que ya hace `Session::execute`). Cada pestaña ejecuta lo suyo a la vez que las demás.
- Implementación (H1.6): lo que ve la UI vive en un `QueryState` por pestaña (estado, resumen, `QueryMessagesModel`, error a subrayar), y `QueryController.current` es el de la pestaña actual. Para saber de quién es cada señal final, la pestaña guarda una entrada por `execute()` enviado con el id de su plan; las de un plan reemplazado se descartan, así el `cancelled` del plan viejo no para el nuevo. Si una sentencia acaba antes de que llegue la cancelación, el plan termina igual como "Query cancelled".
- La contraseña se comparte con `ConnectionController` (`readPassword` → `passwordAvailable`/`passwordUnavailable`, `sessionPassword`, `rememberPassword`, `forgetPassword`, que solo borra la recordada en memoria, nunca la del llavero). Un `open()` que falla cierra la sesión; si es de autenticación, se olvida la contraseña recordada para que la siguiente ejecución la pida.
- `QueryController` se declara antes que `ConnectionController` en `MainWindow.qml`: los hijos se destruyen en orden de creación y las sesiones deben morir antes de descargar los plugins.

## 7. Gestión de errores

- Los providers traducen los errores del driver a `Error`: `kind` según SQLSTATE o código nativo; `message` en lenguaje claro y traducible; `detail` con el texto literal del driver; `position` si el driver la da (PostgreSQL la da).
- En la UI, un **panel de mensajes** muestra el `message` destacado, con un icono y un texto que indica el tipo: nunca solo color. El `detail` va en una sección desplegable "Technical details" que se puede copiar. Si hay `position`, se subraya en el editor y la posición se anuncia por accesibilidad.
- Los errores de conexión sugieren qué hacer según el `kind`: Authentication → "Check the user name and password"; Connection → "Check host, port and that the server is running"; Timeout → "The server did not answer in 15 s".
- Los errores internos (bugs) se registran en el log local (`QStandardPaths::AppLocalDataLocation/logs`, rotado, sin datos de consultas por defecto) y se muestran con un genérico "Something went wrong" y un enlace para abrir el log.
- Si un plugin falla al cargar (ABI distinto, falta una DLL del driver), el motor sale como "unavailable" con el motivo, y el resto de la app sigue funcionando.

## 8. Sistema de plugins

- Cada motor es una librería `MODULE` en `<app>/plugins/quarry/` (en macOS, `Quarry.app/Contents/PlugIns/quarry/`), con un JSON embebido:
  ```json
  { "id": "postgres", "name": "PostgreSQL", "version": "0.1.0",
    "apiVersion": 1, "fileExtensions": [], "urlSchemes": ["postgres", "postgresql"],
    "capabilities": ["Schemas", "MultipleDatabases", "Transactions", "Cancel", "Explain"] }
  ```
- **Descubrimiento sin carga:** en el arranque, `ProviderRegistry` recorre el directorio y lee `QPluginLoader::metaData()`, que no carga la librería. Con eso llena el selector de motores de "Nueva conexión". `load()` + `createProvider()` solo se ejecutan cuando se abre una conexión de ese motor.
- **Versionado:** el IID incluye la versión mayor de la API (`/1`). Si cambia de forma incompatible, se sube a `/2`; los plugins viejos se ignoran y se registra el motivo. `apiVersion` en el JSON permite filtrar sin cargar.
- **Restricción:** un plugin Qt tiene que compilarse con la misma versión mayor/menor de Qt y un compilador compatible con el ABI. Los plugins oficiales se compilan en el mismo build. En v1 no se soportan plugins de terceros compilados aparte; se documenta el SDK cuando la API se estabilice.
- **Drivers nativos:** libpq, MariaDB Connector/C y el driver ODBC se despliegan junto al plugin (Windows: misma carpeta, con `SetDllDirectory`/`AddDllDirectory` para el directorio de plugins; macOS: `@loader_path`; Linux: `$ORIGIN`). SQLite va compilado dentro de su plugin; la base de datos de la app usa su propia copia enlazada en `core`.
- **Añadir un motor** = nueva carpeta en `src/providers/<id>/`:
  1. `CMakeLists.txt` con `quarry_add_provider(<id> SOURCES … DEPENDS <driver>)`.
  2. Implementar `IDatabaseProvider`, `IConnection`, `IResultStream` y `ISqlDialect`.
  3. Pasar la **suite de conformidad** común, `tests/providers/conformance/`: un conjunto de tests parametrizados (conectar, introspección, tipos, streaming, cancelación, errores) que cada provider instancia con su fixture.

## 9. Dependencias y vcpkg

| Librería | Origen | Uso |
|---|---|---|
| Qt 6.12 | aqt (`C:\Qt`, `~/Qt`) | UI, core |
| sqlite3 | vcpkg | provider SQLite + `quarry.db` |
| libpq 18 | vcpkg | provider PostgreSQL |
| qtkeychain-qt6 | vcpkg + shim | credenciales |
| syntax-highlighting (KF6) | vcpkg + shim | resaltado SQL |
| libmariadb | vcpkg | provider MariaDB (hito posterior) |
| libssh2 | vcpkg | túneles SSH (hito posterior) |

**Overlay *shim* de Qt.** `vcpkg-overlays/ports/qtbase` y `qttools` son ports vacíos (`VCPKG_POLICY_EMPTY_PACKAGE`) con la versión de `.qt-version`. Los triplets overlay (`x64-windows-quarry`, `x64-linux-quarry`, `arm64-osx-quarry`…) añaden `VCPKG_ENV_PASSTHROUGH QT_ROOT_DIR` y pasan `-DCMAKE_PREFIX_PATH=$ENV{QT_ROOT_DIR}` a los ports. Así QtKeychain y KSyntaxHighlighting se compilan contra la Qt de aqt y no aparece una segunda Qt. Ventajas: un solo gestor de dependencias, caché binaria de vcpkg en la CI y Perl (lo necesita KSyntaxHighlighting) gestionado por vcpkg. `vcpkg.json` fija un `builtin-baseline`.

## 10. Credenciales y seguridad

- **Nunca hay contraseñas en texto plano** en disco, logs ni historial. `ConnectionStore` (`quarry.db`) guarda host, puerto, usuario, opciones, color y la marca de producción, pero no el secreto.
- `CredentialStore` usa QtKeychain: Windows Credential Manager, macOS Keychain, Linux Secret Service (libsecret/KWallet). Clave: servicio `dev.quarry.Quarry`, cuenta `connection/<uuid>/password` (y `…/ssh-passphrase` cuando haya SSH).
- Si el llavero no está disponible (Linux sin Secret Service), se ofrece **"Ask every time"**: la contraseña se pide al conectar y solo vive en memoria durante la sesión. Nunca se guarda en otro sitio como alternativa.
- El usuario elige si guarda la contraseña o la pide siempre. Borrar una conexión borra también su entrada del llavero.
- Las cadenas de conexión que se muestran o copian enmascaran la contraseña. La exportación de conexiones (hito posterior) nunca incluye secretos.
- Seguridad del usuario (se detalla en UX.md): color por conexión; marca de producción en rojo, con icono y texto; confirmación para `UPDATE`/`DELETE` sin `WHERE`; vista previa del SQL antes de aplicar ediciones; deshacer en el editor de datos; modo de solo lectura por conexión (`SET default_transaction_read_only`, `PRAGMA query_only`).

## 11. Base de datos propia (`quarry.db`)

- Ruta: `QStandardPaths::AppDataLocation/quarry.db`. Modo WAL. Migraciones con `PRAGMA user_version`, aplicadas en el arranque dentro de una transacción.
- Tablas iniciales: `connections`, `connection_folders`, `query_history` (con FTS5 en su hito), `saved_queries`, `ui_state` (pestañas abiertas, tamaño de paneles).
- Se accede desde un hilo de `core` propio. La UI nunca espera a disco en el arranque: carga la lista de conexiones de forma asíncrona y muestra el estado vacío o la lista cuando llega, normalmente en < 10 ms.
- Cada hito crea solo las tablas que usa. H1.3 crea `connections` (migración 1). El resto llega con su hito.

### 11.1 Contrato de conexiones y credenciales (H1.3)

Contrato compartido entre `core` (backend) y `app` (frontend). Las cabeceras viven en `src/core/include/quarry/`. Todas las clases son QObject del hilo de UI con API **asíncrona**: el trabajo de disco y llavero va a un hilo de `core` y los resultados llegan por señales con `Qt::QueuedConnection`. Los errores son `quarry::Error` con `message` en lenguaje claro y `detail` técnico.

```cpp
namespace quarry {
enum class PasswordMode : uint8_t { None, Saved, AskEveryTime };

struct ConnectionProfile {
    QUuid id;                       // generado al crear
    QString name;                   // obligatorio y no vacío (lo valida ConnectionStore)
    QString providerId;             // "sqlite", "postgres"…
    QVariantMap values;             // campos de connectionFields() SIN secretos
    int colorIndex = -1;            // -1 = sin color; 0..7 = paleta de UX §6
    bool production = false;
    PasswordMode passwordMode = PasswordMode::None;
    QDateTime createdAt, updatedAt; // UTC, los fija ConnectionStore
    std::optional<QDateTime> lastUsedAt;
};

class ConnectionStore : public QObject {          // tabla `connections`, migraciones con user_version
    explicit ConnectionStore(QString databasePath, QObject *parent = nullptr);
    void load();                                   // -> loaded(QList<ConnectionProfile>) | failed(Error)
    void save(ConnectionProfile profile, const QStringList &secretFieldKeys = {}); // inserta o actualiza -> saved(ConnectionProfile) | failed(Error)
                                                   // secretFieldKeys = claves de los campos Kind::Secret del provider; se eliminan (y siempre "password")
    void remove(QUuid id);                         // -> removed(QUuid) | failed(Error). La app borra también la credencial.
    void touch(QUuid id);                          // actualiza lastUsedAt -> saved(...)
    static QString defaultDatabasePath();          // QStandardPaths::AppDataLocation/quarry.db
};

class CredentialStore : public QObject {           // QtKeychain; servicio "dev.quarry.Quarry", cuenta "connection/<uuid>/password"
    void readPassword(QUuid id);                    // -> passwordRead(QUuid, QString) | failed(QUuid, Error)
    void writePassword(QUuid id, QString password); // -> passwordWritten(QUuid) | failed(QUuid, Error)
    void deletePassword(QUuid id);                  // -> passwordDeleted(QUuid) | failed(...). "Not found" cuenta como éxito.
    // "Ask every time": la contraseña solo vive en memoria durante la ejecución de la app.
    void rememberForSession(QUuid id, QString password);
    std::optional<QString> sessionPassword(QUuid id) const;
    void forgetForSession(QUuid id);
};

class ConnectionTester : public QObject {          // usa ProviderRegistry + Session; timeout 15 s
    explicit ConnectionTester(ProviderRegistry &registry, QObject *parent = nullptr);
    void test(const ConnectionProfile &profile, const Secrets &secrets); // -> succeeded(ServerInfo) | failed(Error); un test a la vez
    void setTimeoutMs(int ms);                                // 15000 por defecto (seam de tests)
    void cancel();                                            // -> cancelled()
};
}
```

Reglas:
- `quarry.db` **nunca** contiene contraseñas, y `values` no puede llevar un campo de tipo `ConnectionField::Kind::Secret` (`ConnectionStore` los elimina antes de guardar).
- Si el llavero no está disponible, `CredentialStore` falla con un `Error` claro ("The system keychain is not available…") y la UI ofrece "Ask every time". **Nunca** hay un respaldo en disco.
- Borrar una conexión: `ConnectionStore::remove` y después `CredentialStore::deletePassword`, que orquesta la app. `deletePassword` solo se llama si el modo era `Saved` (así no se toca el llavero sin necesidad); `forgetForSession` se llama siempre.
- Guardar con `Saved`: primero se escribe en el llavero y después en `quarry.db`. Si el llavero falla, no se guarda nada y el diálogo ofrece cambiar a "Ask every time".
- Duplicar conserva el modo de contraseña pero no copia la contraseña. Abrir un archivo SQLite que ya está guardado selecciona esa conexión en vez de crear otra.
- Implementación (H1.3): `failed(Error)` de `ConnectionStore` no indica la operación; `load()` ordena por nombre; `lastUsedAt` solo lo cambia `touch()`. `ConnectionTester` emite siempre desde el event loop, nunca dentro de `test()`. Tras recargar, los números de `values` pueden ser `double`/`qint64`: usa `toInt()`.
- Fuera de H1.3: carpetas de conexiones, modo de solo lectura e importar/exportar conexiones.

### 11.2 Contrato de pestañas del editor (H1.5)

Las pestañas del editor y su texto se guardan en `quarry.db` para que nunca se pierda lo escrito. Mismo patrón que `ConnectionStore`: QObject del hilo de UI, trabajo en un hilo de `core`, resultados por señales en cola.

```cpp
namespace quarry {
struct EditorTab {
    QUuid id;                           // nulo = nueva (el store genera uno)
    std::optional<QUuid> connectionId;  // nullopt = pestaña sin conexión
    QString title;                      // "Query 1"…; no vacío
    QString text;
    int cursorPosition = 0;
    int position = 0;                   // orden en la barra de pestañas
    QDateTime updatedAt;                // UTC, lo fija el store
};

class EditorTabStore : public QObject {           // tabla `editor_tabs` (migración 2)
    explicit EditorTabStore(QString databasePath, QObject *parent = nullptr);
    ~EditorTabStore() override;                   // termina las escrituras en cola y une el hilo (≤ 5 s)
    void load();                                  // -> loaded(QList<EditorTab>) ordenadas por position | failed(Error)
    void save(quarry::EditorTab tab);             // inserta o actualiza -> saved(EditorTab) | failed(Error)
    void remove(QUuid id);                        // -> removed(QUuid) (también si no existía) | failed(Error)
};
}
```

- `editor_tabs(id, connection_id REFERENCES connections(id) ON DELETE SET NULL, title, text, cursor_position, position, updated_at)`: borrar una conexión deja sus pestañas sin conexión, no las borra.
- Las migraciones viven en un solo sitio de `core` (`AppDatabase`), compartido por `ConnectionStore` y `EditorTabStore`, y son seguras si los dos abren `quarry.db` a la vez (transacción `BEGIN IMMEDIATE`).
- La app guarda con *debounce* (~1 s tras la última tecla) y al cerrar la pestaña o la app; el destructor del store garantiza que esas escrituras terminan.
- En la app, `EditorTabsModel` (`src/app/ui`) guarda la lista, el texto en memoria y la lógica de sentencias: parte el texto con `splitStatements()` del dialecto 250 ms después de la última edición (la sentencia bajo el cursor usa el último corte) y `run()` siempre parte el texto actual. `run()` devuelve el plan `{ok, kind, message, tabId, connectionId, index, count, ranges[{begin, end, startLine, endLine}]}` y emite `runRequested(plan)`: es el punto de enganche de la ejecución en H1.6. Las pestañas se cargan después del primer frame; una pestaña abierta antes se conserva y las guardadas van delante. Al borrar una conexión, sus pestañas pierden la conexión también en memoria.
- `SqlDocument` conecta un `TextEdit` con KSyntaxHighlighting. No se usan archivos `.theme`: un `SyntaxHighlighter` propio colorea cada estilo con los tokens `syn.*` de `Theme` (UX §6), así que el cambio de tema repinta en vivo y el contraste se comprueba en `tst_theme_contrast`. Definiciones: "SQL" sin conexión, "SQL (PostgreSQL)" para PostgreSQL.
- `shortcut:` los números de línea suponen que todas las líneas tienen la misma altura; con fuentes de reserva (CJK, emoji) pueden desalinearse. Mejorarlo si se nota, leyendo la geometría de cada bloque visible.

## 12. Arranque rápido

- No se carga ningún provider ni se abre ninguna conexión en el arranque. El escaneo de providers (solo metadatos) y el hilo de `ConnectionStore` arrancan en `ConnectionController::start()`, después del primer frame.
- Los diálogos del sistema (`QtQuick.Dialogs`, para elegir el archivo SQLite) se cargan con un `Loader` por URL al usarse por primera vez, no en el arranque. Es una dependencia de despliegue: H10 debe incluir los DLL de Qt6QuickDialogs2 y su plugin QML. Las vistas QML se crean con `Loader { asynchronous: true }` salvo el esqueleto de la ventana principal.
- QML precompilado con **qmlcachegen** (y `qmlsc` cuando esté disponible), con `qt_add_qml_module`, para evitar compilar QML en tiempo de ejecución.
- KSyntaxHighlighting y su `Repository` (definiciones) se inicializan al abrir el primer editor, no en el arranque. Solo se paga la carga de su DLL al iniciar el proceso (~6 ms): no se puede diferir porque la app enlaza su metaobjeto exportado. Abrir una pestaña de 10 000 líneas cuesta ~170 ms una vez, en el primer resaltado.
- Las medidas y los presupuestos están en [PERFORMANCE.md](PERFORMANCE.md).

## 13. Riesgos técnicos identificados

| Riesgo | Mitigación |
|---|---|
| **Qt Quick no expone `QAccessibleTableInterface`**: un `TableView` no se anuncia como tabla a NVDA, VoiceOver ni Orca | Spike en el Hito 1: `QuarryGrid` (QQuickItem en C++) con un `QAccessibleInterface` propio (`QAccessible::installFactory`) que implementa la tabla, las celdas y las cabeceras sobre el modelo. Plan B: el elemento enfocado anuncia "fila X de N, columna Y: valor" con `Accessible.announce` (Qt 6.8+) |
| Rendimiento de `TableView` con 1 M de filas y anchos variables | Altura de fila fija, anchos de columna calculados con una muestra de las primeras 500 filas, `reuseItems: true`, delegates sin `Loader` |
| QtKeychain en Linux sin Secret Service | Modo "Ask every time" (§10) |
| aqt fijado a un commit de master | Volver a una versión de PyPI cuando salga una > 3.3.0 (`scripts/requirements-aqt.txt`) |
| macOS universal con vcpkg | Compilar dos triplets (arm64/x64) y unir con `lipo` en el hito de distribución |
