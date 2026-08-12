#!/usr/bin/env python3
"""
DQN 智能干扰机训练入口

通过 gRPC 连接 C++ 后端，独立进程运行。

用法:
  python3 train_grpc.py --radar-id=1 --jammer-id=1 --episodes=500
  python3 train_grpc.py --radar-id=2 --jammer-id=1 --http-target=http://localhost:8080 --grpc-target=localhost:50051
"""

import sys
import os
import time
import argparse
import requests
import yaml

from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from client import ECMSimClient
# from protos import agent_service_pb2
from train.trainer import DQNTrainer


def load_config(path="./script/config.yaml"):
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def setup_scene(cfg, http_target, grpc_target):
    """通过 HTTP 创建会话并添加雷达/干扰机"""
    r = requests.post(f"{http_target}/api/scene", json={})
    r.raise_for_status()
    data = r.json()
    if not data.get("success"):
        raise RuntimeError(f"create session failed: {data}")
    session_id = data["session_id"]

    for rc in cfg["scene_radars"]:
        rr = requests.post(f"{http_target}/api/radars",
                           json={"session_id": session_id, "radar": rc})
        if rr.json().get("success"):
            print(f"[INFO] radar R{rc['id']} added")

    for jc in cfg["scene_jammers"]:
        rr = requests.post(f"{http_target}/api/jammers",
                           json={"session_id": session_id, "jammer": jc})
        if rr.json().get("success"):
            print(f"[INFO] jammer J{jc['id']} added")

    client = ECMSimClient(grpc_target)
    client.session_id = session_id
    return client


def parse_args():
    p = argparse.ArgumentParser(description="ECMSim DQN Jammer Trainer")
    p.add_argument("--radar-id", type=int, default=1, help="target radar ID")
    p.add_argument("--jammer-id", type=int, default=1, help="jammer ID to control")
    p.add_argument("--episodes", type=int, default=None, help="override max episodes")
    p.add_argument("--http-target", default=None, help="HTTP target (e.g. http://localhost:8080)")
    p.add_argument("--grpc-target", default=None, help="gRPC target (e.g. localhost:50051)")
    p.add_argument("--config", default="./script/config.yaml", help="config YAML path")
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

    if args.http_target:
        cfg["http_target"] = args.http_target
    if args.grpc_target:
        cfg["grpc_target"] = args.grpc_target
    if args.episodes:
        cfg["max_train_episode"] = args.episodes

    http_t = cfg["http_target"]
    grpc_t = cfg["grpc_target"]

    print(f"[INFO] HTTP: {http_t}, gRPC: {grpc_t}")
    print(f"[INFO] radar_id={args.radar_id}, jammer_id={args.jammer_id}, episodes={cfg['max_train_episode']}")

    if not wait_for_backend(http_t):
        print("[ERROR] backend not ready")
        sys.exit(1)

    client = setup_scene(cfg, http_t, grpc_t)

    trainer = DQNTrainer(cfg, client, args.radar_id, args.jammer_id)
    trainer.train()

    client.close()


if __name__ == "__main__":
    main()