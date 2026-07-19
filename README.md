# VsDB

VsDB 是基于 Qt 6 的轻量数据库工作台，目前提供真实的 PostgreSQL 连接、结构浏览和 SQL 数据操作能力。

## PostgreSQL 功能

- 使用主机名或 `postgresql://` 地址、端口、用户、密码、数据库和 SSL 模式建立连接；密码不会写入本地配置。
- 浏览服务器中的登录用户、数据库、模式、表和视图；双击用户可切换会话身份，双击数据库可切换当前数据库。
- 查看表的字段、PostgreSQL 类型、可空性、默认值、主键、索引、外键、大小和说明。
- 直接执行查询以及 `INSERT`、`UPDATE`、`DELETE`、DDL 等 SQL，结果按 50/100/500 行限制读取。
- 双击有主键的表后可编辑结果单元格，并在一个事务中提交或回滚修改。
- 查询读取使用只进游标，结构树按展开动作加载，避免一次性载入完整数据库结构和大结果集。

## 目录结构

```text
src/
├─ database/
│  ├─ DatabaseTypes.h                 # 数据库领域数据结构
│  └─ postgres/PostgresSession.*      # PostgreSQL 连接、元数据、SQL 与事务
├─ ui/
│  ├─ PostgresConnectionDialog.*      # PostgreSQL 连接参数与校验
│  ├─ QueryPage.*                     # SQL 编辑、结果展示、导出与编辑交互
│  └─ IconProvider.*                  # 图标资源加载
├─ MainWindow.*                       # 工作台编排和结构树交互
├─ WorkbenchModels.*                  # 真实查询结果模型与待提交修改
├─ SqlEditor.*                        # SQL 编辑器与语法高亮
└─ main.cpp                           # 程序入口及截图验收入口
tests/
├─ WorkbenchModelsTest.cpp            # 结果模型测试
└─ PostgresSessionTest.cpp            # PostgreSQL 真实集成测试
```

## 构建与启动

需要安装：

- CMake 3.21+
- Qt 6.5+，包含 Widgets、Svg、Sql 和 QPSQL 驱动
- PostgreSQL 客户端库 `libpq`
- 支持 C++20 的 MinGW 编译器

在项目根目录执行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="<Qt安装目录>/<Qt版本>/mingw_64" -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
.\build\VsDB.exe
```

如果 Qt 已加入 CMake 的搜索路径，可以省略 `CMAKE_PREFIX_PATH`。Windows 发布包需要同时部署 Qt 的 `sqldrivers/qsqlpsql.dll` 及其 `libpq` 运行库依赖。

`VsDB.PostgresSession` 在未设置测试数据库时会安全跳过；设置以下环境变量后，会运行完整的 PostgreSQL 集成测试：

```powershell
$env:VSDB_TEST_PG_HOST = "127.0.0.1"
$env:VSDB_TEST_PG_PORT = "5432"
$env:VSDB_TEST_PG_USER = "postgres"
$env:VSDB_TEST_PG_PASSWORD = "<密码>"
$env:VSDB_TEST_PG_DATABASE = "postgres"
ctest --test-dir build -R PostgresSession --output-on-failure
```
