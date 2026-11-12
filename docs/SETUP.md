# Entorno de desarrollo

La versión de Qt se fija **solo** en [`.qt-version`](../.qt-version). La leen `scripts/setup.*`, `cmake/QtLocate.cmake` (y, a través de él, los presets) y la CI.

## Requisitos

| Herramienta | Windows | macOS | Linux |
|---|---|---|---|
| Compilador C++20 | MSVC 2022 Build Tools (carga VCTools) | Xcode Command Line Tools (clang) | gcc ≥ 11 o clang ≥ 14 |
| CMake | ≥ 3.24 | ≥ 3.24 | ≥ 3.24 |
| Ninja, Git, Python 3 | winget | Homebrew | gestor de paquetes |
| Qt | `.qt-version`, `win64_msvc2022_64` en `C:\Qt` | `clang_64` en `~/Qt` | `linux_gcc_64` / `linux_gcc_arm64` en `~/Qt` |
| vcpkg | `C:\vcpkg` | `~/vcpkg` | `~/vcpkg` |

Qt se instala con [aqtinstall](https://github.com/miurahr/aqtinstall), fijado en `scripts/requirements-aqt.txt`. Módulos: el paquete base (qtbase, qtdeclarative/Qt Quick, Quick Controls) más `qtshadertools`.

> **Por qué aqt está fijado a un commit de master:** aqt 3.3.0 (última versión en PyPI) no instala Qt ≥ 6.11 en Windows. Qt reorganizó el repositorio por arquitectura (`qt6_6120/qt6_6120_msvc2022_64/`) y aqt 3.3.0 da el error `Failed to download checksum for the file 'Updates.xml'`. El arreglo (miurahr/aqtinstall#959, PR #1000) está en master sin publicar. Cuando salga una versión > 3.3.0, cambia la línea por `aqtinstall==<versión>`.

## Instalación

### Windows
```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1          # interactivo
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -NonInteractive -SkipCompiler   # CI
```
Pasos manuales:
- La instalación de Build Tools pide **UAC** (admin).
- El script pregunta antes de definir `VCPKG_ROOT` como variable de usuario.
- Ninja + MSVC necesitan el entorno de desarrollador. Antes de compilar, abre **Developer PowerShell for VS 2022** o ejecuta:
  ```powershell
  & 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  ```

### macOS / Linux
```bash
scripts/setup.sh                    # interactivo; pide confirmación antes de sudo
scripts/setup.sh --non-interactive  # CI
```
- aqt se instala en un venv propio (`~/.cache/dbmanager/aqt-venv`) para evitar el error PEP 668 `externally-managed-environment`.
- Si la versión de CMake de la distribución es < 3.24, el script instala CMake desde PyPI en `~/.local/bin`.
- El script no modifica tu perfil de shell. Añade tú `export VCPKG_ROOT=~/vcpkg`.

## Compilar y testear
```bash
cmake --preset windows-debug        # o linux-debug / macos-debug / *-release
cmake --build --preset windows-debug
ctest --preset windows-debug
```
Los binarios quedan en `build/<preset>/`. El test `env-check-smoke` abre `tools/env-check` en modo offscreen y falla si aparece algún warning de QML.

Para ejecutar a mano en Windows, Qt tiene que estar en el PATH (o hay que desplegar con `windeployqt`):
```powershell
$env:Path = "C:\Qt\6.12.0\msvc2022_64\bin;$env:Path"; .\build\windows-debug\env-check.exe
```

## Variables de entorno

| Variable | Uso |
|---|---|
| `VCPKG_ROOT` | Obligatoria. Los presets la usan para el toolchain de vcpkg. |
| `QT_ROOT_DIR` | Opcional. Si existe, prevalece sobre `QT_BASE_DIR/<versión>/<arch>`. La define `install-qt-action` en la CI. |
| `QT_BASE_DIR` | Variable de caché de CMake (`-DQT_BASE_DIR=...`) para instalar Qt fuera de la ruta por defecto. |
| `QT_QPA_PLATFORM=offscreen` | Para ejecutar la app en Linux sin pantalla. El test smoke ya la usa. |

## Problemas comunes

- **`python3` abre la Microsoft Store (Windows):** es un alias de `WindowsApps`. Usa `py` o `python`, o desactívalo en *Configuración → Aplicaciones → Alias de ejecución de aplicaciones*.
- **`cmake`/`ninja` no se encuentran justo después de instalarlos:** winget actualiza el PATH del registro, no el de la shell abierta. Abre una terminal nueva. `setup.ps1` ya refresca el PATH por su cuenta.
- **`Qt 6.x.y not found at ...`:** ejecuta el script de setup, o revisa `QT_BASE_DIR` / `QT_ROOT_DIR`.
- **`Could not find a configuration file for package "Qt6" that is compatible with requested version`:** la Qt encontrada no coincide con `.qt-version`. Borra `build/<preset>` y reconfigura.
- **Rutas largas con vcpkg (Windows):** vcpkg está en `C:\vcpkg` para no pasar de MAX_PATH. Si aun así falla, activa `LongPathsEnabled` (requiere admin): `New-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force`.
- **Linux: `Could not load the Qt platform plugin "xcb"`:** falta `libxcb-cursor0` u otra `libxcb-*`. Ejecuta de nuevo `scripts/setup.sh` o ejecuta con `QT_DEBUG_PLUGINS=1` para ver qué biblioteca falta.
- **Linux sin pantalla (CI, SSH):** `QT_QPA_PLATFORM=offscreen`.
- **Warning `Qt6QmlAssetDownloaderPrivate could not be found because dependency Qt6TaskTree could not be found`:** es benigno. Es un paquete privado opcional de Qt 6.12 que depende del addon `qttasktree`, que no instalamos a propósito para mantener los módulos de Qt al mínimo.
- **`"vswhere.exe" no se reconoce...` al ejecutar `Launch-VsDevShell.ps1`:** es benigno. Lo imprime un script interno de VS; el entorno queda configurado igual (compruébalo con `cl`).
