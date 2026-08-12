import sys, os, time, numpy as np, requests, yaml
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from client import ECMSimClient
from core.dqn_agent import DQNJammerAgent
from utils.common import mkdir_if_not_exist


class DQNTrainer:
    def __init__(self, cfg, client, radar_id, jammer_id):
        self.cfg = cfg
        self.client = client
        self.radar_id = radar_id
        self.jammer_id = jammer_id
        self.agent = DQNJammerAgent(cfg)
        self.max_ep = cfg["max_train_episode"]

        radar_cfg = None
        for rc in cfg["scene_radars"]:
            if rc["id"] == radar_id:
                radar_cfg = rc
                break
        if radar_cfg is None:
            raise ValueError(f"radar id {radar_id} not found")
        self.radar_freq = radar_cfg["freq"]
        self.radar_bw   = radar_cfg["bandwidth"]

        # Reward parameters (from optimize.md)
        rw = cfg.get("reward_weights", {})
        self.wd = rw.get("wd", 1.0)
        self.wf = rw.get("wf", 0.5)
        self.wp = rw.get("wp", 0.1)
        self.sig_alpha = cfg.get("sigmoid_alpha", 0.5)
        self.sig_gamma = cfg.get("sigmoid_gamma", -50.0)
        self.max_db = 60.0

        # Freq shift parameters
        self.freq_shift_num = cfg.get("freq_shift_num", 3)
        self.freq_shift_mult = cfg.get("freq_shift_mult", 2.5)

    def index_to_action(self, idx):
        pn = self.cfg["power_level_num"]
        p_idx = idx % pn
        f_idx = idx // pn
        power_dbm  = p_idx * 10.0
        # Wider shift: (f_idx - center) * (bw * mult)
        # center = freq_shift_num // 2
        center = self.freq_shift_num // 2
        freq_shift = (f_idx - center) * (self.radar_bw * self.freq_shift_mult)
        jam_freq = self.radar_freq + freq_shift
        return power_dbm, jam_freq

    def compute_reward(self, results, power_dbm):
        """Continuous multi-dimensional reward (optimize.md §4.2):
           R = wd*(1-Pd) + wf*avg_zeta - wp*(PdBm/maxPd)
           Pd = 1/(1+exp(-alpha*(SINR-gamma)))
        """
        n = max(len(results), 1)
        p_norm = power_dbm / self.max_db
        total   = 0.0
        for res in results:
            sinr_db   = res.sinr_db
            Pd        = 1.0 / (1.0 + np.exp(-self.sig_alpha * (sinr_db - self.sig_gamma)))
            zeta_list = list(res.freq_match_factors) if hasattr(res, 'freq_match_factors') else []
            avg_z     = sum(zeta_list) / max(len(zeta_list), 1)
            total    += self.wd * (1.0 - Pd) + self.wf * avg_z - self.wp * p_norm
        return total / n

    def run_episode(self, verbose=False):
        try:
            state = self.client.get_state(self.jammer_id)
        except Exception as e:
            print(f"[WARN] get_state failed: {e}")
            return -10.0, {}
        state_np = np.array(state, dtype=np.float32)
        total_reward = 0.0
        step = 0
        step_log = []
        while step < 50:
            act_idx = self.agent.choose_action(state_np)
            power_dbm, jam_freq = self.index_to_action(act_idx)
            freq_shift_khz = (jam_freq - self.radar_freq) / 1e3
            try:
                self.client.execute_action(self.jammer_id, power_dbm, jam_freq)
                results = self.client.step_simulation()
                reward  = self.compute_reward(list(results), power_dbm)
                next_state = self.client.get_state(self.jammer_id)
            except Exception as e:
                print(f"[WARN] gRPC step {step} failed: {e}")
                return total_reward - 5.0, step_log
            next_np = np.array(next_state, dtype=np.float32)

            # Collect per-step stats
            sinr_db = results[0].sinr_db if results else -999
            zeta_avg = (sum(results[0].freq_match_factors) / max(len(results[0].freq_match_factors), 1)
                        if results and results[0].freq_match_factors else 0.0)
            hit = results[0].detect_success if results else True
            Pd = 1.0 / (1.0 + np.exp(-self.sig_alpha * (sinr_db - self.sig_gamma)))
            step_info = {
                "step": step, "act": act_idx,
                "pwr_dbm": power_dbm, "freq_shift_khz": freq_shift_khz,
                "sinr_db": sinr_db, "zeta": zeta_avg, "Pd": Pd,
                "hit": hit, "reward": reward
            }
            step_log.append(step_info)

            if verbose:
                print(f"  [STEP] s{step:2d} act={act_idx:2d} "
                      f"P={power_dbm:3.0f}dBm Δf={freq_shift_khz:+.1f}kHz "
                      f"SINR={sinr_db:+.1f}dB ζ={zeta_avg:.3f} "
                      f"Pd={Pd:.4f} {'HIT' if hit else 'MISS'} "
                      f"R={reward:+.4f}")

            self.agent.buffer.push(state_np, act_idx, reward, next_np, False)
            self.agent.update()
            state_np = next_np
            total_reward += reward
            step += 1
        return total_reward, step_log

    def train(self):
        wdir = self.cfg["weight_save_dir"]
        mkdir_if_not_exist(wdir)
        print(f"[TRAIN] radar_id={self.radar_id} jammer_id={self.jammer_id} episodes={self.max_ep}")
        for ep in range(1, self.max_ep + 1):
            verbose = (ep == 1 or ep % 100 == 0)
            ep_reward, log = self.run_episode(verbose=verbose)
            if ep % 20 == 0 or ep == 1:
                avg_z = 0.0
                avg_p = 0.0
                avg_pd = 0.0
                if log:
                    avg_z = sum(s["zeta"] for s in log) / len(log)
                    avg_p = sum(s["pwr_dbm"] for s in log) / len(log)
                    avg_pd = sum(s["Pd"] for s in log) / len(log)
                loss = self.agent.update()
                print(f"[TRAIN] Ep {ep:4d}/{self.max_ep} | "
                      f"R={ep_reward:+.3f} | "
                      f"ε={self.agent.epsilon:.3f} | "
                      f"buf={self.agent.buffer.size()} | "
                      f"loss={loss:.4f} | "
                      f"⟨ζ⟩={avg_z:.3f} ⟨P⟩={avg_p:.1f}dBm ⟨Pd⟩={avg_pd:.4f}")
            if ep % 200 == 0:
                sp = os.path.join(wdir, f"dqn_jammer_ep{ep}.pth")
                self.agent.save_model(sp)
                print(f"[SAVE] {sp}")
        fp = os.path.join(wdir, "dqn_jammer_final.pth")
        self.agent.save_model(fp)
        print(f"[DONE] model saved: {fp}")
