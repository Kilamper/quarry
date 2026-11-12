# Quarry: convenciones del proyecto

Gestor de bases de datos de escritorio: C++20, Qt 6.12 LTS (Qt Quick), CMake + Ninja + vcpkg. Antes de cambiar nada, lee los docs: [ARCHITECTURE](docs/ARCHITECTURE.md), [ROADMAP](docs/ROADMAP.md), [UX](docs/UX.md), [PERFORMANCE](docs/PERFORMANCE.md) y [SETUP](docs/SETUP.md).

## Reglas de trabajo
- Se trabaja **hito a hito** según ROADMAP.md. Antes de empezar un hito, resume el plan; al terminar, compila, pasa los tests, verifica los criterios y los presupuestos, actualiza ROADMAP.md y **espera el visto bueno**.
- Si una decisión de diseño no está en los docs, **pregunta** antes de tomarla y actualiza el doc correspondiente.
- **Ninguna dependencia** fuera del stack de ARCHITECTURE §1/§9 sin aprobación.
- Ni migraciones, ni builds de release para publicar, ni push, ni nada destructivo sin confirmación explícita.
- Reparto entre subagentes: `backend-dev` se encarga de `src/core`, `src/providers/*` y sus tests; `frontend-dev` de `src/app` (QML, ViewModels, modelos C++ de la UI) y sus tests. El contrato entre ambos son los headers públicos de `src/core/include/quarry/`: si cambian, se acuerdan antes de lanzar los subagentes.

## Compilar y testear
```powershell
# Windows: primero el entorno de MSVC
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```
Linux/macOS: `linux-debug` / `macos-debug` (y `*-release`). Linux sin pantalla: `QT_QPA_PLATFORM=offscreen`. Instalar o reparar el entorno: `scripts/setup.ps1` o `scripts/setup.sh`. La versión de Qt solo se cambia en `.qt-version`.

## Estructura
```
src/core/include/quarry/   API pública de core (contrato con app y providers)
src/core/api/              quarry_provider_api (headers de la interfaz de plugin)
src/core/src/              implementación de core (solo Qt6::Core)
src/providers/<id>/        un plugin MODULE por motor
src/app/                   ejecutable: main.cpp, viewmodels/, qml/Quarry/{Controls,Views}/, i18n/
tests/{core,providers/conformance,providers/<id>,app}/
tools/{env-check,bench}/   vcpkg-overlays/   cmake/   scripts/   docs/
```
Dependencias permitidas: `app` → `core` (nunca un provider); `providers` → `quarry_provider_api` + su driver; `core` → Qt6::Core, sqlite3, qtkeychain. **core no incluye nada de QtGui, QtQml ni QtQuick.**

## Estilo C++
- C++20, sin extensiones del compilador. `clang-format` (`.clang-format` del repo) obligatorio; `clang-tidy` en la CI.
- Archivos: `snake_case.h` / `snake_case.cpp`, con `#pragma once`. Un tipo principal por archivo.
- Nombres: tipos `PascalCase`; funciones y variables `camelCase`; miembros `m_camelCase`; constantes `kPascalCase`; enums `enum class` con valores `PascalCase`; macros `QUARRY_UPPER_CASE`. Namespace `quarry` (y `quarry::sqlite`, `quarry::postgres` … en los providers).
- Errores: `quarry::Result<T>` / `quarry::Error`. **Ninguna excepción cruza la frontera del plugin ni de un hilo.** Los mensajes de `Error::message` se escriben en lenguaje claro y traducibles; el texto del driver va en `detail`.
- Propiedad: `std::unique_ptr` por defecto; jerarquía de padres de Qt para los QObject; nada de `new` sin dueño. Usa `const&`/`QStringView` en los parámetros.
- Hilos: un `IConnection` solo se usa desde el hilo de su sesión, salvo `requestCancel()`. Hacia la UI, solo señales con `Qt::QueuedConnection`. **Nada bloqueante en el hilo de UI** (disco, red, llavero, carga de plugins).
- Rendimiento: nada de `QVariant` por celda en almacenamiento; la conversión a texto solo ocurre en `data()`. Antes de añadir trabajo al arranque, comprueba el presupuesto en PERFORMANCE.md.
- Comentarios: solo el *porqué* que el código no muestra. Los atajos con límite conocido llevan `// shortcut: <límite>, <cuándo mejorarlo>`.

## QML y UI
- Solo componentes de `Quarry.Controls` (estilo propio sobre Basic); nada de colores literales, todo con tokens de `Theme`.
- Todo control interactivo tiene `Accessible.role`/`Accessible.name` (traducible), foco visible, orden de tabulación lógico y funciona solo con teclado (UX.md §2). Los cambios asíncronos se anuncian con `Accessible.announce`.
- Textos: inglés en el código con `qsTr()`/`tr()`; traducción en `src/app/i18n/quarry_es.ts`.
- Respeta `Theme.animationDuration` (0 con movimiento reducido). Ninguna información se transmite solo por color.
- Tipos C++ expuestos con `QML_ELEMENT` / `QML_SINGLETON` en `qt_add_qml_module`, no con `setContextProperty`.

## Tests
- Qt Test para C++ (`tests/core`, `tests/providers`), con `QSignalSpy` para lo asíncrono y `QAbstractItemModelTester` en todos los modelos. Qt Quick Test para QML (`tests/app`).
- Un provider nuevo pasa la suite de conformidad (`tests/providers/conformance`).
- Nombres: `tst_<unidad>.cpp`, clase `Tst<Unidad>`. Los tests no necesitan red salvo los de conformidad de servidores, que se saltan (`QSKIP`) si no está definida `QUARRY_TEST_<MOTOR>_URL`.

## Commits
- [Conventional Commits](https://www.conventionalcommits.org/) con ámbito: `feat(core): …`, `fix(app): …`, `test(providers/sqlite): …`, `ci: …`, `docs: …`, `perf: …`, `chore: …`.
- Commits pequeños, con un solo propósito, que compilan y pasan los tests. Asunto en inglés, en imperativo, de ≤ 72 caracteres; el cuerpo explica el porqué.
- No se commitean `build/`, `CMakeUserPresets.json` ni secretos.
