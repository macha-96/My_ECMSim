import torch
import torch.optim as optim
import numpy as np
from model.q_network import QNetwork
from core.buffer import ReplayBuffer

class DQNJammerAgent:
    """Single-jammer DQN agent with coverage-aware state.

    State: 13-dim vector = 9 base threat-weighted + 4 coverage features
    Action: 21 = 7 power × 3 freq_shift (fixed, radar-count agnostic)
    """

    def __init__(self, cfg):
        self.cfg = cfg
        self.device = cfg["device"]
        self.state_dim = cfg["state_dim"]
        self.action_dim = cfg["action_dim"]

        # 主网络 & 目标网络
        self.q_net = QNetwork(self.state_dim, self.action_dim).to(self.device)
        self.target_q_net = QNetwork(self.state_dim, self.action_dim).to(self.device)
        self.target_q_net.load_state_dict(self.q_net.state_dict())

        self.optimizer = optim.Adam(self.q_net.parameters(), lr=cfg["lr"])
        self.buffer = ReplayBuffer(cfg["buffer_capacity"])

        self.gamma = cfg["gamma"]
        self.epsilon = cfg["epsilon_start"]
        self.epsilon_end = cfg["epsilon_end"]
        self.epsilon_decay = cfg["epsilon_decay"]
        self.target_step = cfg["target_update_step"]
        self.step_count = 0

    def parse_state(self, raw_state):
        """Parse raw flat radar table from C++ into threat-weighted 9-dim vector.

        raw_state format: [radar_count, radar1[rx,ry,freq,bw,pt,dist,delta_f], ..., jammer[pj,freq]]
        Returns: np.array of shape (9,) — threat-weighted aggregated features

        Note: Coverage features (dims 9-12) are appended by trainer.parse_state_with_coverage()
        """
        radar_count = int(raw_state[0])
        offset = 1
        radar_features = []
        for i in range(radar_count):
            feat = raw_state[offset:offset+7]
            radar_features.append(feat)
            offset += 7
        jammer_pj = raw_state[offset]
        jammer_freq = raw_state[offset + 1]

        radar_features = np.array(radar_features, dtype=np.float64)  # (N, 7)

        if radar_count == 0:
            return np.zeros(9, dtype=np.float32)

        # Threat weight: inverse distance (state indices: 5=dist/30000)
        dists = radar_features[:, 5]  # normalized dist / 30000 → real dist = dist*30000
        threats = 1.0 / (dists * 30000.0 + 1.0)
        w_sum = threats.sum()

        if w_sum > 0.0:
            weights = threats / w_sum  # (N,)
            # Weighted average of spatial/power features
            avg_rx   = float(np.sum(radar_features[:, 0] * weights))
            avg_ry   = float(np.sum(radar_features[:, 1] * weights))
            avg_pt   = float(np.sum(radar_features[:, 4] * weights))
            avg_dist = float(np.sum(radar_features[:, 5] * weights))

            # Frequency features from the most threatening radar
            max_idx = int(np.argmax(threats))
            max_freq   = float(radar_features[max_idx, 2])
            max_bw     = float(radar_features[max_idx, 3])
            max_delta_f = float(radar_features[max_idx, 6])
        else:
            avg_rx = avg_ry = avg_pt = avg_dist = 0.0
            max_freq = max_bw = max_delta_f = 0.0

        return np.array([
            avg_rx, avg_ry, avg_pt, avg_dist,
            max_freq, max_bw, max_delta_f,
            jammer_pj, jammer_freq
        ], dtype=np.float32)

    def choose_action(self, state):
        """Select action via ε-greedy.

        Args:
            state: 13-dim numpy array (pre-parsed) or raw_state list
        Returns:
            action index (0-20)
        """
        if isinstance(state, (list, np.ndarray)) and len(state) == 13:
            state_np = state
        elif isinstance(state, (list, np.ndarray)):
            state_np = self.parse_state(state)
        else:
            state_np = state
        # ε-greedy
        self.step_count += 1
        self.epsilon = max(self.epsilon_end, self.epsilon - 1 / self.epsilon_decay)
        if np.random.random() < self.epsilon:
            return np.random.randint(0, self.action_dim)
        else:
            state = torch.from_numpy(state_np).float().unsqueeze(0).to(self.device)
            q_val = self.q_net(state)
            return torch.argmax(q_val, dim=1).item()

    def update(self):
        if self.buffer.size() < self.cfg["batch_size"]:
            return 0.0
        # 采样批次
        s, a, r, s_next, done = self.buffer.sample(self.cfg["batch_size"])
        s_tensor = torch.from_numpy(s).float().to(self.device)
        a_tensor = torch.from_numpy(a).long().unsqueeze(1).to(self.device)
        r_tensor = torch.from_numpy(r).float().unsqueeze(1).to(self.device)
        s_next_tensor = torch.from_numpy(s_next).float().to(self.device)
        done_tensor = torch.from_numpy(done).float().unsqueeze(1).to(self.device)

        # 当前Q值
        current_q = self.q_net(s_tensor).gather(1, a_tensor)
        # 目标Q值
        next_q_max = self.target_q_net(s_next_tensor).max(dim=1, keepdim=True)[0]
        target_q = r_tensor + self.gamma * next_q_max * (1 - done_tensor)

        loss = torch.mean((current_q - target_q.detach()) ** 2)
        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()

        # 定时更新target网络
        if self.step_count % self.target_step == 0:
            self.target_q_net.load_state_dict(self.q_net.state_dict())
        return loss.item()

    def save_model(self, path):
        torch.save(self.q_net.state_dict(), path)

    def load_model(self, path):
        ckpt = torch.load(path, map_location=self.device)
        self.q_net.load_state_dict(ckpt)
        self.target_q_net.load_state_dict(ckpt)
