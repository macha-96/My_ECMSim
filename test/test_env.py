# test_env.py
from client import ECMSimClient
cfg = ...
client = ECMSimClient(...)
client.reset_session("session_1")
# 多次尝试不同频点、功率
test_cases = [
    (0, 雷达中心频点),
    (30, 雷达中心频点),
    (30, 雷达中心频点 + 偏移),
]
for p, f in test_cases:
    client.execute_action(jammer_id=1, power_dbm=p, jam_freq=f)
    res = list(client.step_simulation())
    sinr = res[0].sinr_db
    zeta = sum(res[0].freq_match_factors)/max(len(res[0].freq_match_factors),1)
    print(f"P={p}, F={f}, SINR={sinr}, ζ={zeta}")
