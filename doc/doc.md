1:
已基于 my-reactor-server 的 HTTP 库（libreactor.so）和现有的 ECMSim 仿真算法模块，实现了完整的 Web 图形界面。
新增/修改的文件
文件
test/web_server.cpp
test/Makefile
构建与运行
cd test
make web           # 编译生成 ../bin/web_serv
make web-run       # 编译 + 启动服务器 (http://localhost:8080)
或者直接：
cd /path/to/ECMSim
./bin/web_serv config/sence_config.json
REST API 端点
方法
GET
GET
POST
POST
Web GUI 功能
- **态势显示
- **参数控制
- **仿真
- **
- **运行

2：
新增 static/index.html — 完整的 Web GUI 页面（19KB，HTML+CSS+JS 全部包含）
修改 test/web_server.cpp：
- 移除了嵌入
- 新增 `g
- handleIndex
- 支持 `argv[
修改 test/Makefile — web-run 目标传递 static 参数
现在可以直接编辑 static/index.html 修改页面，无需重新编译 C++。
