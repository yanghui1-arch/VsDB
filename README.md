# VsDB

## 构建与启动

需要安装：

- CMake 3.21+
- Qt 6.5+
- 支持 C++20 的 MinGW 编译器

在项目根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="<Qt安装目录>/<Qt版本>/mingw_64"
cmake --build build
.\build\VsDB.exe
```

如果 Qt 已加入 CMake 的搜索路径，可以省略 `CMAKE_PREFIX_PATH`：

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
.\build\VsDB.exe
```
