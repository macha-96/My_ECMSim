#!/usr/bin/env python3
"""DQN training entry point — connects via gRPC to C++ backend.

Supports variable numbers of radars and jammers. Each jammer is controlled
by an independent DQN agent with coverage-aware coordination.

Usage:
  python3 script/train_agent.py --jammer-ids=1,2 --episodes=1000
  python3 script/train_agent.py --jammer-ids=1,2 --num-radars=3 --num-jammers=2
  python3 script/train_agent.py --session-id=session_1 --jammer-ids=1,2
"""
import sys, os, time, argparse, requests, yaml, numpy as np
import logging

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJ_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PROJ_DIR)

from client import ECMSimClient
from train.trainer import DQNTrainer

MAX_RETRIES = 3

def load_config(path = None):
    if path is None:
        path = os.path.join(_SCRIPT_DIR, "config.yaml")
    with open(path) as f:
        return yaml.safe_load(f)


def setup_scene(cfg, http_target, grpc_target, num_radars=None, num_jammers=None, max_retries=3):
    for attempt in range(max_retries):
        try:
            r = requests.post(f"{http_target}/api/scene", json={}, timeout=10)
            d = r.json()
            if not d.get("success"):
                raise RuntimeError(f"create session failed: {d}")
            sid = d["session_id"]

            import random
            radars = cfg["scene_radars"]
            jammers = cfg["scene_jammers"]

            if num_radars is not None:
                radars = random.sample(radars, min(num_radars, len(radars)))
            if num_jammers is not None:
                jammers = random.sample(jammers, min(num_jammers, len(jammers)))

            for rc in radars:
                rr = requests.post(f"{http_target}/api/radars",
                    json={"session_id": sid, "radar": rc}, timeout=10)
                if not rr.json().get("success"):
                    logging.warning(f"radar R{rc['id']} failed: {rr.text}")
            for jc in jammers:
                rr = requests.post(f"{http_target}/api/jammers",
                    json={"session_id": sid, "jammer": jc}, timeout=10)
                if not rr.json().get("success"):
                    logging.warning(f"jammer J{jc['id']} failed: {rr.text}")
            c = ECMSimClient(grpc_target)
            c.session_id = sid
            return c
        except (requests.ConnectionError, RuntimeError, TimeoutError) as e:
            logging.warning(f"setup attempt {attempt+1}/{max_retries}: {e}")
            if attempt < max_retries - 1:
                time.sleep(3)
            else:
                raise


def parse_args():
    p = argparse.ArgumentParser(description="ECMSim DQN Trainer")
    p.add_argument("--session-id", default = None)
    p.add_argument("--radar-id", type = int, default = 1)
    p.add_argument("--jammer-ids", default = "1,2",
                   help="Comma-separated jammer IDs to train (e.g. '1,2')")
    p.add_argument("--num-radars", type = int, default = None,
                   help="Number of radars to use (random subset)")
    p.add_argument("--num-jammers", type = int, default = None,
                   help="Number of jammers to use (random subset)")
    p.add_argument("--episodes", type = int, default = None)
    p.add_argument("--http-target", default = None)
    p.add_argument("--grpc-target", default = None)
    p.add_argument("--config", default = None)
    return p.parse_args()


def wait_for_backend(http_target, timeout=60):
    start = time.time()
    while time.time() - start < timeout:
        try:
            r = requests.get(f"{http_target}/api/scene", timeout=5)
            if r.status_code == 200:
                return True
        except requests.ConnectionError:
            time.sleep(2)
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

    jammer_ids = [int(x.strip()) for x in args.jammer_ids.split(",") if x.strip()]

    http_t, grpc_t = cfg["http_target"], cfg["grpc_target"]
    logging.info(f"http={http_t} grpc={grpc_t}")
    logging.info(f"radar_id={args.radar_id} jammer_ids={jammer_ids} "
          f"episodes={cfg['max_train_episode']}")

    if not wait_for_backend(http_t):
        logging.error("backend not ready after 60s")
        sys.exit(1)
    logging.info("backend ready")

    if args.session_id:
        # 使用已有会话 — 不创建新场景，直接连接
        sid = args.session_id
        logging.info(f"using existing session: {sid}")
        client = ECMSimClient(grpc_t)
        client.session_id = sid
    else:
        # 创建新会话 + 添加雷达/干扰机
        logging.info("creating new session with radars and jammers")
        try:
            client = setup_scene(cfg, http_t, grpc_t, args.num_radars, args.num_jammers)
        except Exception as e:
            logging.error(f"setup failed: {e}")
            sys.exit(1)

    trainer = DQNTrainer(cfg, client, jammer_ids)

    try:
        trainer.train()
    except (requests.ConnectionError, RuntimeError) as e:
        logging.error(f"training failed: {e}")
    except KeyboardInterrupt:
        logging.error("\ntraining interrupted")
    finally:
        try:
            client.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
