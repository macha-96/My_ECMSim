import sys
import os
import time
import numpy as np
import requests
import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from client import ECMSimClient
from core.dqn_agent import DQNJammerAgent
from utils.common import mkdir_if_not_exist


class DQNTrainer:
    def __init__(self, cfg: dict, client: ECMSimClient, radar_id: int, jammer_id: int):
        self.cfg = cfg
        self.client = client
        self.radar_id = radar_id
        self.jammer_id = jammer_id
        self.agent = DQNJammerAgent(cfg)
        self.max_ep = cfg["max_train_episode"]

        # 从配置获取目标雷达参数，用于动作映射
        radar_cfg = None
        for rc in cfg["scene_radars"]:
            if rc["id"] == radar_id:
                radar_cfg = rc
                break
        if radar_cfg is None:
            raise ValueError(f"radar id {radar_id} not found in config")
        self.radar_freq = radar_cfg["freq"]
        self.radar_bw = radar_cfg["bandwidth"]

    def index_to_action(self, idx: int):
        pn = self.cfg["power_level_num"]
        p_idx = idx % pn
        f_idx = idx // pn
        power_dbm = p_idx * 10.0
        freq_shift = (f_idx - 1) * (self.radar_bw / 2.0)
        jam_freq = self.radar_freq + freq_shift
        return power_dbm, jam_freq

    def compute_reward(self, sim_results) -> float:
        w = self.cfg["reward_weights"]
        r = 0.0
        for res in sim_results:
            if not res.detect_success:
                score = res.jam_success_score if res.jam_success_score > 0 else 0.5
                r += w["detect_fail"] * score
                avg_zeta = sum(res.freq_match_factors) / max(len(res.freq_match_factors), 1)
                r += w["freq_match_bonus"] * min(avg_zeta, 1.0)
            else:
                r += w["detect_success"]
        return r / max(len(sim_results), 1)

    def run_episode(self) -> float:
        state = self.client.get_state(self.jammer_id)
        state_np = np.array(state, dtype=np.float32)
        total_reward = 0.0
        step = 0
        while step < 50:
            act_idx = self.agent.choose_action(state_np)
            power_dbm, jam_freq = self.index_to_action(act_idx)
            self.client.execute_action(self.jammer_id, power_dbm, jam_freq)
            results = self.client.step_simulation()
            reward = self.compute_reward(list(results))
            next_state = self.client.get_state(self.jammer_id)
            next_np = np.array(next_state, dtype=np.float32)
            self.agent.buffer.push(state_np, act_idx, reward, next_np, False)
            self.agent.update()
            state_np = next_np
            total_reward += reward
            step += 1
        return total_reward

    def train(self):
        wdir = self.cfg["weight_save_dir"]
        mkdir_if_not_exist(wdir)
        print(f"[TRAIN] radar_id={self.radar_id} jammer_id={self.jammer_id} episodes={self.max_ep}")
        for ep in range(1, self.max_ep + 1):
            ep_reward = self.run_episode()
            if ep % 20 == 0 or ep == 1:
                avg_z = 0.0
                try:
                    s = self.client.get_state(self.jammer_id)
                    if len(s) >= 6:
                        df = abs(s[5])
                        avg_z = np.exp(-0.5 * (df * 10)**2 / (2 * 0.25**2))
                except Exception:
                    pass
                loss = self.agent.update()
                print(f"[TRAIN] Ep {ep:4d}/{self.max_ep} | "
                      f"Reward: {ep_reward:+.3f} | "
                      f"Eps: {self.agent.epsilon:.3f} | "
                      f"Buf: {self.agent.buffer.size()} | "
                      f"Loss: {loss:.4f} | ζ: {avg_z:.3f}")
            if ep % 200 == 0:
                sp = os.path.join(wdir, f"dqn_jammer_ep{ep}.pth")
                self.agent.save_model(sp)
                print(f"[SAVE] {sp}")
        fp = os.path.join(wdir, "dqn_jammer_final.pth")
        self.agent.save_model(fp)
        print(f"[DONE] model saved: {fp}")