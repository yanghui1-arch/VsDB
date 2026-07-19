# VsDB

VsDB 是一个以低内存占用和快速交互为目标的原生数据库可视化 GUI。

当前版本实现了 [Issue #1](https://github.com/yanghui1-arch/VsDB/issues/1) 的高保真工作台原型：连接浏览器、SQL 编辑器、结果表格、对象检查器、查询标签页、搜索、快捷键和持久化布局。数据为确定性的演示数据；本里程碑尚未连接真实数据库。

## 技术选择

- C++20 + Qt 6 Widgets，保持原生启动速度和较小运行时开销。
- `QAbstractTableModel` 按需生成演示单元格，不为表格中的每个单元创建 QWidget。
- 查询执行使用事件循环定时器模拟异步完成，界面线程不会阻塞。
- 使用 Qt 自带的树、表格、分割器、标签页和样式系统，不引入额外 UI 框架或动态依赖。

## Windows 构建与启动

需要 CMake 3.21+、Qt 6.5+ 和与 Qt 套件匹配的 C++ 编译器。仓库当前开发环境使用 Qt 6.9.2 MinGW：

```powershell
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.9.2\mingw_64 `
  -DCMAKE_CXX_COMPILER=C:\Qt\Tools\mingw1310_64\bin\g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe `
  -DBUILD_TESTING=ON
C:\Qt\Tools\CMake_64\bin\cmake.exe --build build --parallel 4
$env:PATH = "C:\Qt\6.9.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build --output-on-failure
.\build\VsDB.exe
```

如果要把程序复制到没有 Qt 开发环境的机器，再执行：

```powershell
C:\Qt\6.9.2\mingw_64\bin\windeployqt.exe build\VsDB.exe
```

如果使用 Qt Creator，直接打开根目录的 `CMakeLists.txt`，选择 Qt 6.5+ Desktop Kit 后运行 `VsDB` target。

## 原型交互

- `Ctrl+Enter` 执行当前 SQL，`Esc` 可停止模拟执行。
- `Ctrl+T` 新建查询，`Ctrl+W` 关闭当前查询。
- 双击连接树中的表会创建对应的查询。
- 拖动三栏和编辑器/结果区之间的分隔线，布局会在退出时保存。

## 后续里程碑

真实数据库连接需要增加后台连接池、可取消的工作线程任务、凭据安全存储，以及基于 `fetchMore()` 的分页结果模型。这些边界已在 UI 原型中保留，但不在 Issue #1 的布局范围内。
