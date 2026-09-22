#!/bin/bash
echo "=== ECMSim会话资源回收测试 ==="

# 启动服务器后台运行
echo "1. 启动服务器..."
cd /home/ky/workspace/ECMSim
./bin/ecmsim_http_beast &
SERVER_PID=$!
sleep":2  # 等待服务器启动

echo "2. 创建测试会话..."
SESSION1=$(curl -s -X POST http://localhost:8080/api/scene \
  -H "Content-Type: application/json" \
  -d '{}' | jq -r '.session_id')
echo "   会话1: $SESSION1"

SESSION2=$(curl -s -X POST http://localhost:8080/api/scene \
  -H "Content-Type: application/json" \
  -d '{}' | jq -r '.session_id')
echo "   会话2: $SESSION2"

echo "3. 查看当前会话列表..."
curl -s -X GET http://localhost:8080/api/scene/list \
  -H "Content-Type: application/json" \
  -d '{}' | jq

echo "4. 查看服务器统计..."
curl -s -X GET http://localhost:8080/api/scene/stats \
  -H "Content-Type: application/json" \
  -d '{}' | jq

echo "5. 手动测试会话超时清理..."
echo "   修改SceneManager中的清理阈值进行测试..."

echo "6. 停止服务器..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo "=== 测试完成 ==="