#!/usr/bin/env python3
"""DQN inference — connects to existing session, controls a jammer.

Usage:
  python3 script/inference.py --session-id=session_1 --jammer-id=1
  python3 script/inference.py --session-id=session_1 --jammer-id=1 \
      --model=weights/dqn_jammer_final.pth --steps=100
"""
import sys, os, time, argparse, numpy as np

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJ_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PROJ_DIR)

from client import ECMSimClient
from core.dqn_agent import DQNJammerAgent
from utils.common import mkdir_if_not_exist

def load_config(path=None):
    import yaml
    if path is None:
        path = os.path.join(_SCRIPT_DIR, "config.yaml")
    with open(path) as f:
        return yaml.safe_load(f)

def index_to_action(idx, radar_freq, radar_bw, cfg):
    pn = cfg["power_level_num"]
    p_idx = idx % pn
    f_idx = idx // pn
    power_dbm = p_idx * 10.0
    # Wider shift: (f_idx - center) * (bw * mult)
    fn = cfg.get("freq_shift_num", 3)
    mult = cfg.get("freq_shift_mult", 2.5)
    center = fn // 2
    freq_shift = (f_idx - center) * (radar_bw * mult)
    jam_freq = radar_freq + freq_shift
    return power_dbm, jam_freq

def parse_args():
    p = argparse.ArgumentParser(description="ECMSim DQN Inference")
    p.add_argument("--session-id", required=True)
    p.add_argument("--jammer-id", type=int, required=True)
    p.add_argument("--grpc-target", default="localhost:50051")
    p.add_argument("--model", default=None)
    p.add_argument("--steps", type=int, default=10)
    p.add_argument("--interval", type=float, default=1.0)
    p.add_argument("--config", default=None)
    return p.parse_args()

def main():
    args = parse_args()
    cfg = load_config(args.config)

    # Get radar bw from config for action mapping (freq will come from state)
    radar_bw   = cfg["scene_radars"][0]["bandwidth"]

    # Connect via gRPC
    client = ECMSimClient(args.grpc_target)
    client.session_id = args.session_id
    print(f"[INFO] session={args.session_id} jammer={args.jammer_id}")

    # Load trained model if given
    agent = None
    if args.model:
        agent = DQNJammerAgent(cfg)
        agent.load_model(args.model)
        agent.epsilon = 0.0
        print(f"[INFO] model loaded: {args.model}")

    try:
        for step in range(1, args.steps + 1):
            print(f"\n--- Step {step}/{args.steps} ---")

            # 1. Get state
            state = client.get_state(args.jammer_id)
            print(f"  state ({len(state)}): {[round(v, 3) for v in state]}")

            # Extract actual radar freq from state (index 2: radar_freq / 20e9)
            actual_radar_freq = state[2] * 20e9

            # 2. Choose action
            if agent:
                state_np = np.array(state, dtype=np.float32)
                act = agent.choose_action(state_np)
                power_dbm, jam_freq = index_to_action(act, actual_radar_freq, radar_bw, cfg)
                print(f"  DQN act {act}: {power_dbm:.0f}dBm {jam_freq/1e9:.3f}GHz")
            else:
                act, power_dbm, jam_freq = 0, 0.0, actual_radar_freq
                print(f"  no model — skip action")

            # 3. Execute action
            client.execute_action(args.jammer_id, power_dbm, jam_freq)
            print(f"  exec: P={power_dbm:.0f}dBm f={jam_freq/1e9:.3f}GHz")

            # 4. Step simulation
            results = client.step_simulation()
            for r in results:
                z = (sum(r.freq_match_factors) / max(len(r.freq_match_factors), 1)
                     if r.freq_match_factors else 0)
                print(f"  R{r.radar_id}: SINR={r.sinr_db:.1f}dB "
                      f"detect={r.detect_success} "
                      f"score={r.jam_success_score:.3f} ζ={z:.3f}")

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n[INFO] interrupted")
    except Exception as e:
        print(f"[ERROR] {e}")
    finally:
        client.close()
        print("[INFO] done")

if __name__ == "__main__":
    main()
