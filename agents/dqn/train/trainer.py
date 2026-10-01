import sys, os, time, numpy as np, requests, yaml, random, logging
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
logging.basicConfig(level=logging.DEBUG)
from client import ECMSimClient
from core.dqn_agent import DQNJammerAgent
from utils.common import mkdir_if_not_exist


class DQNTrainer:
    """Multi-jammer DQN trainer with coverage-aware coordination.

    Each jammer is controlled by an independent DQN agent. Coordination emerges
    from coverage-aware state features + redundancy penalty in reward.
    Supports variable numbers of radars and jammers.
    """

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
        self.freq_shift_mult = cfg.get("freq_shift_mult", 2500)
        self.ep = 0

    def index_to_action_with_freq(self, idx, actual_radar_freq, actual_radar_bw):
        pn = self.cfg["power_level_num"]
        p_idx = idx % pn
        f_idx = idx // pn
        power_dbm = p_idx * 10.0
        center = self.freq_shift_num // 2
        freq_shift = (f_idx - center) * (actual_radar_bw * self.freq_shift_mult)
        jam_freq = actual_radar_freq + freq_shift
        return power_dbm, jam_freq

    def index_to_action_with_coverage(self, idx, raw_state, state_np):
        """Map action to (power_dbm, jam_freq, ref_freq) using coverage-aware reference.

        Reference radar selection:
        - If uncovered radars exist (state_np[9] > 0): use nearest uncovered radar
        - Otherwise: use most threatening radar (state_np[4] = max_freq)
        """
        pn = self.cfg["power_level_num"]
        p_idx = idx % pn
        f_idx = idx // pn
        power_dbm = p_idx * 10.0

        radar_count = int(raw_state[0])
        uncovered_count_norm = state_np[9] if len(state_np) > 9 else 0.0

        if uncovered_count_norm > 0 and radar_count > 0:
            # Use nearest uncovered radar frequency (from coverage features)
            ref_freq = state_np[11] * 20e9  # nearest_uncovered_freq
            # Find matching radar in raw_state for bandwidth
            actual_radar_bw = 1e6
            for i in range(radar_count):
                offset = 1 + i * 7
                if abs(raw_state[offset + 2] * 20e9 - ref_freq) < 1e6:
                    actual_radar_bw = raw_state[offset + 3] * 10e6
                    break
        else:
            # Use most threatening radar (max_freq from base state)
            ref_freq = state_np[4] * 20e9 if len(state_np) > 4 else 10e9
            actual_radar_bw = state_np[5] * 10e6 if len(state_np) > 5 else 1e6

        center = self.freq_shift_num // 2
        freq_shift = (f_idx - center) * (actual_radar_bw * self.freq_shift_mult)
        jam_freq = ref_freq + freq_shift
        return power_dbm, jam_freq, ref_freq

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

    def parse_state_with_coverage(self, raw_state, results, jammer_id):
        """Return 13-dim state: 9 base + 4 coverage features.

        Coverage features: [uncovered_count_norm, nearest_uncovered_dist,
                            nearest_uncovered_freq, nearest_uncovered_delta_f]
        """
        base = self.agents[jammer_id].parse_state(raw_state)

        if not results:
            return np.concatenate([base, [0.0, 0.0, 0.0, 0.0]]).astype(np.float32)

        radar_count = int(raw_state[0])
        pd_list = [self._pd_from_sinr(r.sinr_db) for r in results]
        uncovered = [i for i, pd in enumerate(pd_list) if pd > self.pd_threshold]

        if not uncovered or radar_count == 0:
            return np.concatenate([base, [0.0, 0.0, 0.0, 0.0]]).astype(np.float32)

        uncovered_count_norm = len(uncovered) / len(results)

        # Find nearest uncovered radar using raw_state
        jammer_freq = raw_state[-1] * 20e9
        best_dist = float('inf')
        best_freq = 0.0
        best_delta = 0.0

        for i in uncovered:
            offset = 1 + i * 7
            rx = raw_state[offset] * 20000
            ry = raw_state[offset + 1] * 20000
            freq = raw_state[offset + 2] * 20e9
            dist = raw_state[offset + 5] * 30000

            if dist < best_dist:
                best_dist = dist
                best_freq = freq
                best_delta = (freq - jammer_freq) / 20e9

        nearest_dist_norm = min(best_dist / 30000.0, 1.0)
        nearest_freq_norm = min(best_freq / 20e9, 1.0)
        nearest_delta_norm = max(-1.0, min(1.0, best_delta))

        coverage = [uncovered_count_norm, nearest_dist_norm, nearest_freq_norm, nearest_delta_norm]
        return np.concatenate([base, coverage]).astype(np.float32)

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
            states_np[jid] = self.parse_state_with_coverage(states_raw[jid], [], jid)

        total_rewards = {jid: 0.0 for jid in self.jammer_ids}
        step_log = []
        step = 0
        while step < 50:
            actions = {}
            for jid in self.jammer_ids:
                act_idx = self.agents[jid].choose_action(states_np[jid])
                power_dbm, jam_freq, ref_freq = self.index_to_action_with_coverage(
                    act_idx, states_raw[jid], states_np[jid])
                actions[jid] = (act_idx, power_dbm, jam_freq, ref_freq)

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
                next_states_np[jid] = self.parse_state_with_coverage(next_states_raw[jid], results, jid)

            for jid in self.jammer_ids:
                act_idx, power_dbm, _, _ = actions[jid]
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
