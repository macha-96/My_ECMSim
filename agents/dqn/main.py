#!/usr/bin/env python3
"""DQN training entry point — connects via gRPC to C++ backend."""
import sys, os, time, argparse, requests, yaml
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from client import ECMSimClient
from train.trainer import DQNTrainer

def load_config(path="./script/config.yaml"):
    with open(path) as f: return yaml.safe_load(f)

def setup_scene(cfg, http_target, grpc_target):
    r = requests.post(f"{http_target}/api/scene", json={})
    d = r.json()
    if not d.get("success"): raise RuntimeError(f"create session failed: {d}")
    sid = d["session_id"]
    for rc in cfg["scene_radars"]:
        rr = requests.post(f"{http_target}/api/radars", json={"session_id": sid, "radar": rc})
        if rr.json().get("success"): print(f"[INFO] radar R{rc['id']} added")
    for jc in cfg["scene_jammers"]:
        rr = requests.post(f"{http_target}/api/jammers", json={"session_id": sid, "jammer": jc})
        if rr.json().get("success"): print(f"[INFO] jammer J{jc['id']} added")
    c = ECMSimClient(grpc_target)
    c.session_id = sid
    return c

def parse_args():
    p = argparse.ArgumentParser(description="ECMSim DQN Trainer")
    p.add_argument("--radar-id", type=int, default=1)
    p.add_argument("--jammer-id", type=int, default=1)
    p.add_argument("--episodes", type=int, default=None)
    p.add_argument("--http-target", default=None)
    p.add_argument("--grpc-target", default=None)
    p.add_argument("--config", default="./script/config.yaml")
    return p.parse_args()

def wait_for_backend(http_target, timeout=60):
    start = time.time()
    while time.time() - start < timeout:
        try:
            requests.get(f"{http_target}/api/scene", timeout=2)
            return True
        except Exception:
            time.sleep(2)
    return False

def main():
    args = parse_args()
    cfg = load_config(args.config)
    if args.http_target: cfg["http_target"] = args.http_target
    if args.grpc_target: cfg["grpc_target"] = args.grpc_target
    if args.episodes: cfg["max_train_episode"] = args.episodes

    http_t, grpc_t = cfg["http_target"], cfg["grpc_target"]
    print(f"[INFO] http={http_t} grpc={grpc_t}")
    print(f"[INFO] radar_id={args.radar_id} jammer_id={args.jammer_id} episodes={cfg['max_train_episode']}")

    if not wait_for_backend(http_t):
        print("[ERROR] backend not ready"); sys.exit(1)

    client = setup_scene(cfg, http_t, grpc_t)
    trainer = DQNTrainer(cfg, client, args.radar_id, args.jammer_id)
    trainer.train()
    client.close()

if __name__ == "__main__":
    main()
