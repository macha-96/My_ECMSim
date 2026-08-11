from core.dqn_agent import DQNJammerAgent
from data.env_parser import EnvParser
from data.action_writer import ActionWriter
from utils.common import mkdir_if_not_exist
import yaml

class DQNTrainer:
    def __init__(self):
        # 加载配置
        with open("./script/config.yaml", "r", encoding="utf-8") as f:
            self.cfg = yaml.safe_load(f)
        mkdir_if_not_exist("./tmp")
        mkdir_if_not_exist("./weights")

        self.agent = DQNJammerAgent(self.cfg)
        self.env_parser = EnvParser(self.cfg["obs_json_path"])
        self.action_writer = ActionWriter(self.cfg["action_json_path"])
        self.max_episode = self.cfg["max_train_episode"]

    def run_episode(self):
        # 1. 读取C++仿真输出观测state
        state = self.env_parser.get_state_vector()
        done = False
        total_reward = 0
        step = 0
        while not done:
            # 2. DQN选择动作
            act_idx = self.agent.choose_action(state)
            # 3. 将动作转为干扰机参数写入JSON，供C++仿真读取执行一步
            jam_action_dict = self.action_writer.index_to_action(act_idx)
            self.action_writer.write_action_json(jam_action_dict)

            # 4. 等待C++仿真完成单步，读取下一状态、奖励、结束标记
            next_state, reward, done = self.env_parser.get_next_state_reward_done()
            total_reward += reward

            # 5. 存入回放池
            self.agent.buffer.push(state, act_idx, reward, next_state, done)
            # 6. 更新网络
            loss = self.agent.update()

            state = next_state
            step += 1
        return total_reward

    def train(self):
        print("Start DQN jammer training ...")
        for ep in range(self.max_episode):
            ep_reward = self.run_episode()
            if (ep + 1) % 100 == 0:
                self.agent.save_model(self.cfg["weight_save_path"])
                print(f"Episode {ep+1}, total reward: {ep_reward:.2f}, save checkpoint")
