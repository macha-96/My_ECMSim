import sys, os, time, numpy as np, requests, yaml, random, logging
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
logging.basicConfig(level=logging.DEBUG)
from client import ECMSimClient
from core.dqn_agent import DQNJammerAgent
from utils.common import mkdir_if_not_exist


class DQNTrainer:
    def __init__(self, cfg, client, jammer_ids):
        self.cfg = cfg
        self.client = client
        self.jammer_ids = list(jammer_ids)
        self.agents = {jid: DQNJammerAgent(cfg) for jid in self.jammer_ids}
        self.max_ep = cfg["max_train_episode"]

        self.radar_freqs = {}
        self.radar_bws = {}
        for rc in cfg["scene_radars"]:
            self.radar_freqs[rc["id"]] = rc["freq"]
            self.radar_bws[rc["id"]] = rc["bandwidth"]

        rw = cfg.get("reward_weights", {})
        self.wd = rw.get("wd", 1.0)
        self.wf = rw.get("wf", 0.3)
        self.wp = rw.get("wp", 0.01)
        self.wred = rw.get("wred", 0.5)
        self.sig_alpha = cfg.get("sigmoid_alpha", 0.5)
        self.sig_gamma = cfg.get("sigmoid_gamma", -50.0)
        self.max_db = 60.0
        self.pd_threshold = 0.5

        self.freq_shift_num = cfg.get("freq_shift_num", 3)
        self.freq_shift_mult = cfg.get("freq_shift_mult", 2.5)
        self.ep = 0

    def _decode_action(self, idx):
        """Decode action index into (target_idx, freq_idx, power_idx)."""
        pn = self.cfg["power_level_num"]
        fn = self.freq_shift_num
        per_radar = fn * pn
        target_idx = idx // per_radar
        remainder = idx % per_radar
        f_idx = remainder // pn
        p_idx = remainder % pn
        return target_idx, f_idx, p_idx

    def index_to_action_multi(self, idx, state_raw):
        """Map action index to (power_dbm, jam_freq) using target radar from state."""
        radar_count = int(state_raw[0])
        target_idx, f_idx, p_idx = self._decode_action(idx)
        target_idx = min(target_idx, radar_count - 1) if radar_count > 0 else 0

        power_dbm = p_idx * 10.0

        if radar_count > 0:
            offset = 1 + target_idx * 7
            actual_radar_freq = state_raw[offset + 2] * 20e9
            actual_radar_bw = state_raw[offset + 3] * 10e6
        else:
            actual_radar_freq = 10e9
            actual_radar_bw = 1e6

        center = self.freq_shift_num // 2
        freq_shift = (f_idx - center) * (actual_radar_bw * self.freq_shift_mult)
        jam_freq = actual_radar_freq + freq_shift
        return power_dbm, jam_freq, target_idx

    def randomize_radar(self):
        try:
            for rc in self.cfg["scene_radars"]:
                new_power = random.choice([0, 10, 20, 30, 40, 50, 60])
                new_freq = rc["freq"] + random.uniform(-500e6, 500e6)
                rc["Pt_dBm"] = new_power
                rc["freq"] = new_freq
                radar_cfg = rc.copy()
                http_target = self.cfg["http_target"]
                requests.put(f"{http_target}/api/radar",
                    json={"session_id": self.client.session_id, "radar": radar_cfg},
                    timeout=10)
        except Exception:
            pass

    def _pd_from_sinr(self, sinr_db):
        return 1.0 / (1.0 + np.exp(-self.sig_alpha * (sinr_db - self.sig_gamma)))

    def _get_zeta(self, res, jammer_id):
        jids = list(res.jammer_ids)
        zetas = list(res.freq_match_factors)
        if jammer_id in jids:
            idx = jids.index(jammer_id)
            if idx < len(zetas):
                return zetas[idx]
        return 0.0

    def compute_reward(self, results, jammer_id, power_dbm):
        n = max(len(results), 1)
        p_norm = power_dbm / self.max_db

        pd_list = []
        zeta_sum = 0.0
        for res in results:
            pd = self._pd_from_sinr(res.sinr_db)
            pd_list.append(pd)
            zeta_sum += self._get_zeta(res, jammer_id)

        coverage = np.mean([1.0 - pd for pd in pd_list])
        freq_match = zeta_sum / n

        # Redundancy: assign each jammer to its best-matched radar
        jammer_targets = {}
        for jid in self.jammer_ids:
            best_r, best_z = -1, -1.0
            for res in results:
                z = self._get_zeta(res, jid)
                if z > best_z:
                    best_z = z
                    best_r = res.radar_id
            jammer_targets[jid] = best_r

        target_counts = {}
        for r in jammer_targets.values():
            target_counts[r] = target_counts.get(r, 0) + 1

        my_target = jammer_targets.get(jammer_id, -1)
        pileup = max(0, target_counts.get(my_target, 0) - 1)
        uncovered = np.mean([1.0 if pd > self.pd_threshold else 0.0 for pd in pd_list])
        dup_penalty = pileup * uncovered

        reward = (self.wd * coverage
                  + self.wf * freq_match
                  - self.wred * dup_penalty
                  - self.wp * p_norm)
        return reward

    def run_episode(self, verbose=False):
        try:
            if self.ep > 100:
                self.randomize_radar()
            self.client.reset()
        except Exception as e:
            logging.warning(f"reset failed: {e}")
            return -10.0, {}

        states_raw = {}
        states_np = {}
        for jid in self.jammer_ids:
            states_raw[jid] = self.client.get_state(jid)
            states_np[jid] = self.agents[jid].parse_state(states_raw[jid])

        total_rewards = {jid: 0.0 for jid in self.jammer_ids}
        step_log = []
        step = 0
        while step < 50:
            actions = {}
            for jid in self.jammer_ids:
                act_idx = self.agents[jid].choose_action(states_raw[jid])
                power_dbm, jam_freq, target_idx = self.index_to_action_multi(act_idx, states_raw[jid])
                actions[jid] = (act_idx, power_dbm, jam_freq, target_idx)

            try:
                for jid in self.jammer_ids:
                    _, power_dbm, jam_freq, _ = actions[jid]
                    self.client.execute_action(jid, power_dbm, jam_freq)
                results = self.client.step_simulation()
            except Exception as e:
                logging.warning(f"gRPC step {step} failed: {e}")
                for jid in self.jammer_ids:
                    total_rewards[jid] -= 5.0
                break

            next_states_raw = {}
            next_states_np = {}
            for jid in self.jammer_ids:
                next_states_raw[jid] = self.client.get_state(jid)
                next_states_np[jid] = self.agents[jid].parse_state(next_states_raw[jid])

            for jid in self.jammer_ids:
                act_idx, power_dbm, _, tgt = actions[jid]
                reward = self.compute_reward(list(results), jid, power_dbm)
                self.agents[jid].buffer.push(states_np[jid], act_idx, reward, next_states_np[jid], False)
                self.agents[jid].update()
                total_rewards[jid] += reward

            if results:
                r0 = results[0]
                pd0 = self._pd_from_sinr(r0.sinr_db)
                zeta0 = sum(self._get_zeta(r0, jid) for jid in self.jammer_ids) / max(len(self.jammer_ids), 1)
                step_info = {
                    "step": step,
                    "sinr_db": r0.sinr_db, "Pd": pd0, "zeta": zeta0,
                    "hit": r0.detect_success,
                    "rewards": {jid: round(total_rewards[jid], 3) for jid in self.jammer_ids},
                    "targets": {jid: self._get_zeta(r0, jid) for jid in self.jammer_ids},
                }
                step_log.append(step_info)

            if verbose:
                r0 = results[0] if results else None
                sinr = f"{r0.sinr_db:+.1f}" if r0 else "N/A"
                pd = f"{self._pd_from_sinr(r0.sinr_db):.4f}" if r0 else "N/A"
                print(f"  [STEP] s{step:2d} SINR={sinr}dB Pd={pd} "
                      f"R1={total_rewards[self.jammer_ids[0]]:+.3f} "
                      f"R2={total_rewards[self.jammer_ids[1]]:+.3f}" if len(self.jammer_ids) > 1 else "")

            states_raw = next_states_raw
            states_np = next_states_np
            step += 1

        return total_rewards, step_log

    def train(self):
        wdir = self.cfg["weight_save_dir"]
        mkdir_if_not_exist(wdir)
        print(f"[TRAIN] jammers={self.jammer_ids} episodes={self.max_ep}")
        for ep in range(1, self.max_ep + 1):
            self.ep = ep
            verbose = (ep == 1 or ep % 100 == 0)
            ep_rewards, log = self.run_episode(verbose=verbose)
            if ep % 20 == 0 or ep == 1:
                avg_pd = 0.0
                avg_z = 0.0
                if log:
                    avg_pd = sum(s["Pd"] for s in log) / len(log)
                    avg_z = sum(s["zeta"] for s in log) / len(log)
                losses = {jid: self.agents[jid].update() for jid in self.jammer_ids}
                loss_str = " ".join(f"J{jid}={losses[jid]:.4f}" for jid in self.jammer_ids)
                r_str = " ".join(f"J{jid}={ep_rewards[jid]:+.3f}" for jid in self.jammer_ids)
                print(f"[TRAIN] Ep {ep:4d}/{self.max_ep} | {r_str} | "
                      f"ε={self.agents[self.jammer_ids[0]].epsilon:.3f} | "
                      f"buf={self.agents[self.jammer_ids[0]].buffer.size()} | "
                      f"{loss_str} | ⟨Pd⟩={avg_pd:.4f} ⟨ζ⟩={avg_z:.3f}")
            if ep % 200 == 0:
                for jid in self.jammer_ids:
                    sp = os.path.join(wdir, f"dqn_jammer_j{jid}_ep{ep}.pth")
                    self.agents[jid].save_model(sp)
                    print(f"[SAVE] {sp}")
        for jid in self.jammer_ids:
            fp = os.path.join(wdir, f"dqn_jammer_j{jid}_final.pth")
            self.agents[jid].save_model(fp)
            print(f"[DONE] model saved: {fp}")
