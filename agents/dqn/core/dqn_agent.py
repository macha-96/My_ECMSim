import torch
import torch.optim as optim
import numpy as np
from model.q_network import QNetwork
from core.buffer import ReplayBuffer

class DQNJammerAgent:
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

    def choose_action(self, state_np):
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
