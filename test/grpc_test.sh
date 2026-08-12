#!/bin/bash
# grpc_test.sh - Test gRPC end-to-end
cd /home/ky/workspace/ECMSim
fuser -k 8080/tcp 2>/dev/null
fuser -k 50051/tcp 2>/dev/null
sleep 1
./bin/web_serv_v3 static &>/tmp/v3.log &
SRV=$!
sleep 5
echo "=== HTTP test ==="
SID=$(curl -s -X POST http://localhost:8080/api/scene | python3 -c "import sys,json;print(json.load(sys.stdin)['session_id'])")
echo "SID=$SID"
curl -s -X POST http://localhost:8080/api/radars \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\",\"radar\":{\"id\":1,\"x\":0,\"y\":0,\"Pt_dBm\":20,\"G_dB\":25,\"freq\":1e10,\"bandwidth\":1e6,\"sigma\":5,\"thresh_db\":-50}}" > /dev/null
curl -s -X POST http://localhost:8080/api/jammers \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\",\"jammer\":{\"id\":1,\"x\":5000,\"y\":0,\"Pj_dBm\":40,\"Gj_dB\":20,\"jam_freq\":1e10,\"jam_type\":\"NOISE_JAM\"}}" > /dev/null
echo "HTTP entities OK"
echo "=== gRPC test ==="
.venv/bin/python3 -c "
import sys
sys.path.insert(0, 'agents/dqn')
from client import ECMSimClient
c = ECMSimClient()
c.session_id = '$SID'
s = c.get_state(1)
print(f'get_state: dim={len(s)} values={[round(v,2) for v in s]}')
c.execute_action(1, 45.0, 10.5e9)
print('execute_action: OK')
r = c.step_simulation()
print(f'step_simulation: {len(r)} results')
for res in r:
    print(f'  R{res.radar_id}: SINR={res.sinr_db:.1f}dB detect={res.detect_success}')
c.close()
print('ALL gRPC OK')
" 2>&1
kill $SRV 2>/dev/null
wait $SRV 2>/dev/null
cat /tmp/v3.log 2>/dev/null | tail -3
echo "DONE"
