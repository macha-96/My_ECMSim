#!/bin/bash
# Test gRPC connection: start server, run Python client
cd /home/ky/workspace/ECMSim

fuser -k 8080/tcp 2>/dev/null; fuser -k 50051/tcp 2>/dev/null; sleep 1

./bin/web_serv_v3 static &
SRV_PID=$!
sleep 5

echo "=== Server started, PID=$SRV_PID ==="

# Create session and add entities via HTTP
SID=$(curl -s -X POST http://localhost:8080/api/scene | python3 -c "import sys,json;print(json.load(sys.stdin)['session_id'])")
echo "SID=$SID"

curl -s -X POST http://localhost:8080/api/radars \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\",\"radar\":{\"id\":1,\"x\":0,\"y\":0,\"Pt_dBm\":20,\"G_dB\":25,\"freq\":1e10,\"bandwidth\":1e6,\"sigma\":5,\"thresh_db\":-50}}" > /dev/null

curl -s -X POST http://localhost:8080/api/jammers \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\",\"jammer\":{\"id\":1,\"x\":5000,\"y\":0,\"Pj_dBm\":40,\"Gj_dB\":20,\"jam_freq\":1e10,\"jam_type\":\"NOISE_JAM\"}}" > /dev/null

echo "=== Entities added ==="

# Run gRPC client
echo "=== Running gRPC client ==="
.venv/bin/python3 -c "
import sys
sys.path.insert(0, 'agents/dqn')
from client import ECMSimClient

c = ECMSimClient()
c.session_id = '$SID'

state = c.get_state(1)
print(f'gRPC GetState OK: dim={len(state)}')

c.execute_action(1, 45.0, 10.5e9)
print(f'gRPC ExecuteAction OK')

results = c.step_simulation()
print(f'gRPC StepSimulation OK: {len(results)} results')
for r in results:
    print(f'  R{r.radar_id}: SINR={r.sinr_db:.1f}dB detect={r.detect_success}')

c.close()
print('ALL gRPC TESTS PASSED')
" 2>&1

echo "=== Server status ==="
kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null
echo "DONE"
" filePath": "/home/ky/workspace/ECMSim/test/test_grpc.sh"}