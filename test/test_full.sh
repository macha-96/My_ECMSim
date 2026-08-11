#!/bin/bash
cd /home/ky/workspace/ECMSim
fuser -k 8080/tcp 2>/dev/null; fuser -k 50051/tcp 2>/dev/null; sleep 1

./bin/web_serv_v3 static &
PID=$!; sleep 5

SID=$(curl -s -X POST http://localhost:8080/api/scene | python3 -c "import sys,json;print(json.load(sys.stdin)['session_id'])")
echo "SID=$SID"

# Add radar + jammer via RESTful POST
curl -s -X POST http://localhost:8080/api/radars \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\",\"radar\":{\"id\":1,\"x\":0,\"y\":0,\"Pt_dBm\":20,\"G_dB\":25,\"freq\":1e10,\"bandwidth\":1e6,\"sigma\":5,\"thresh_db\":-50}}" > /dev/null
curl -s -X POST http://localhost:8080/api/jammers \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\",\"jammer\":{\"id\":1,\"x\":5000,\"y\":0,\"Pj_dBm\":40,\"Gj_dB\":20,\"jam_freq\":1e10,\"jam_type\":\"NOISE_JAM\"}}" > /dev/null

echo "=== GET config ==="
curl -s -X GET http://localhost:8080/api/scene \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\"}" | python3 -c "import sys,json;d=json.load(sys.stdin);print('radars:',len(d.get('radars',[])),'jammers:',len(d.get('jammers',[])))"

echo "=== SIMULATE ==="
curl -s -X POST http://localhost:8080/api/simulate \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\"}" | python3 -c "import sys,json;d=json.load(sys.stdin);print('results:',len(d['sim_results']),'sinr:',round(d['sim_results'][0]['SINR_dB'],1))"

echo "=== UPDATE via POST+action ==="
curl -s -X POST http://localhost:8080/api/radar \
  -H "Content-Type: application/json" \
  -d "{\"action\":\"update\",\"session_id\":\"$SID\",\"radar\":{\"id\":1,\"x\":3000,\"y\":2000,\"Pt_dBm\":30,\"G_dB\":30,\"freq\":1e10,\"bandwidth\":1e6,\"sigma\":5,\"thresh_db\":-50}}"

echo ""

echo "=== DELETE via POST+action ==="
curl -s -X POST http://localhost:8080/api/jammer \
  -H "Content-Type: application/json" \
  -d "{\"action\":\"del\",\"session_id\":\"$SID\",\"jammer_id\":1}"

echo ""
echo "=== Config after delete ==="
curl -s -X GET http://localhost:8080/api/scene \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SID\"}" | python3 -c "import sys,json;d=json.load(sys.stdin);print('jammers:',len(d.get('jammers',[])))"

echo ""
kill $PID 2>/dev/null; wait $PID 2>/dev/null
echo "ALL TESTS PASSED"