# VsDB

VsDB is a low-memory-oriented, native database visualization workbench built with Qt 6 and CMake.

> **Current status:** this first milestone implements the interface from [issue #1](https://github.com/yanghui1-arch/VsDB/issues/1) with deterministic preview data. It does **not** connect to a real database, execute SQL, or store credentials yet.

## Preview features

- Compact three-column workbench: connection explorer, SQL workspace, and object inspector
- Multiple closable query tabs with resizable editor/results areas
- Results and Messages views backed by Qt model/view classes
- Recursive connection-tree filtering and table-to-query navigation
- Shared menu/toolbar commands and keyboard shortcuts
- Honest mock execution state, row count, timing, and connection status
- Persisted window, splitter, and pane visibility settings
- A polished light theme with bundled project-owned SVG icons

## Why Qt Widgets

The shell uses Qt Widgets because VsDB is a dense, keyboard-friendly desktop tool built around trees, tables, splitters, menus, and native window behavior. `QTreeView` and `QTableView` render through models instead of allocating a widget per database cell. This keeps the UI layer compact and leaves a direct path to bounded, paged result models when real database execution is added.

## Requirements

- CMake 3.21 or newer
- A C++20 compiler
- Qt 6.5 or newer with `Core`, `Gui`, `Widgets`, and (for tests) `Test`
- Ninja is recommended, but any CMake generator compatible with your Qt installation works

The compiler ABI must match the Qt kit (for example, use an MSVC Qt kit with MSVC).

## Build, test, and run

### Ninja / single-config generators

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/your-kit
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/VsDB
```

On Windows, the executable is `build/src/VsDB.exe`.

If Qt is already discoverable through `Qt6_DIR`, `CMAKE_PREFIX_PATH`, or your toolchain, omit `-DCMAKE_PREFIX_PATH`.

### Visual Studio / multi-config generators

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\src\Debug\VsDB.exe
```

### Qt Creator

1. Open the root `CMakeLists.txt`.
2. Select a Qt 6.5+ desktop kit.
3. Configure the project.
4. Build and run the `VsDB` target.

## Architecture

```text
MainWindow and pane widgets
          │
          ▼
  WorkspaceController
    │       │       │
    ▼       ▼       ▼
 schema   result  inspector
  model    model    model
          │
          ▼
 future bounded query/session service
```

- **`src/core`** contains compact value types and centralized deterministic preview data.
- **`src/models`** contains read-only `QAbstractItemModel` implementations. Views do not own or materialize database cells as widgets.
- **`src/app/WorkspaceController`** coordinates selection and preview execution. It is the seam where a future thread-aware query/session service will be introduced.
- **`src/ui`** contains focused, composable widgets. `CommandRegistry` creates each `QAction` once for menus, toolbar controls, shortcuts, and state.
- **`resources`** contains target-bundled icons and styling, so runtime behavior does not depend on the working directory.
- **`tests`** checks model contracts and key workbench interactions using Qt Test.

Qt parent-child ownership manages UI and model lifetimes. Models are owned by the controller, panes are owned by the main window, and shared actions are owned by the command registry. User layout preferences are stored with `QSettings`; query results and credentials are not persisted.

The project starts with internal CMake targets rather than a dynamic plugin API. Database/provider boundaries will be designed once real backend requirements are known, avoiding an unstable premature ABI.

## Scope and next steps

This branch intentionally uses small, bounded preview data. Real database support will require:

- connection profiles and secure credential storage
- thread-local database connections and cancellable background execution
- bounded result batches with `canFetchMore()` / `fetchMore()`
- server-side sorting and filtering for large results
- SQL editor services such as highlighting, completion, and diagnostics
- driver deployment and integration tests

No performance number is claimed before representative backend benchmarks exist.

## Qt references

The architecture follows the official documentation for:

- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html)
- [Qt and CMake](https://doc.qt.io/qt-6/cmake-get-started.html)
- [Model/View Programming](https://doc.qt.io/qt-6/model-view-programming.html)
- [Object Trees & Ownership](https://doc.qt.io/qt-6/objecttrees.html)
- [QMainWindow](https://doc.qt.io/qt-6/qmainwindow.html)
- [The Qt Resource System](https://doc.qt.io/qt-6/resources.html)
- [QSettings](https://doc.qt.io/qt-6/qsettings.html)
- [Qt Test](https://doc.qt.io/qt-6/qttest-index.html)

## License

[MIT](LICENSE)
