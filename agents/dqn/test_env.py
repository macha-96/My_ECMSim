# test_env.py
import sys
import os
import yaml
from client import ECMSimClient

_SCRIPT_DIR = '/home/ky/workspace/ECMSim/agents/dqn/script'

def load_config(path=None):
    if path is None:
        path = os.path.join(_SCRIPT_DIR, "config.yaml")
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)

if __name__ == "__main__":
    cfg = load_config()
    client = ECMSimClient("127.0.0.1:50051")
    client.session_id = "session_1"

    test_cases = [
        (0, 10000000000),
        (30, 10000000000),
        (30, 10000000000 - 1_000_000),
    ]

    for p, f in test_cases:
        # 推荐每组测试前重置仿真，避免状态叠加
        # client.reset_simulation()
        client.execute_action(jammer_id=1, power_dbm=p, jam_freq=f)
        res = list(client.step_simulation())
        obs = res[0]
        sinr = obs.sinr_db
        fm_list = obs.freq_match_factors
        zeta = sum(fm_list) / max(len(fm_list), 1)
        print(f"P={p}, F={f}, SINR={sinr:.4f}, ζ={zeta}")