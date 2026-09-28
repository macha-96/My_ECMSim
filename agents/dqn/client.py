#!/usr/bin/env python3
"""DQN Agent gRPC client — connects to C++ web_serv_v2 gRPC server."""

import grpc
import logging
from protos import agent_service_pb2
from protos import agent_service_pb2_grpc

logging.basicConfig(level = logging.DEBUG)

class ECMSimClient:
    def __init__(self, target: str = "localhost:50051"):
        self.channel = grpc.insecure_channel(target)
        self.stub = agent_service_pb2_grpc.AgentServiceStub(self.channel)
        self.session_id = ""
        # 新增：最近一次 get_state / step_simulation 返回的干扰模式
        self.last_jammer_mode = ""
        self.last_jammer_ids = []
        self.last_jammer_modes = []

    def get_state(self, jammer_id: int):
        req = agent_service_pb2.StateRequest(
            session_id=self.session_id, jammer_id=jammer_id
        )
        rep = self.stub.GetState(req)
        if not rep.success:
            raise RuntimeError(
                f"GetState failed: {rep.error} "
                f"(session={self.session_id}, jammer={jammer_id})"
            )
        self.last_jammer_mode = rep.jammer_mode  # "NOISE_JAM" / "RANGE_DECEPT"
        return list(rep.state)

    def execute_action(self, jammer_id: int, power_dbm: float, jam_freq: float):
        req = agent_service_pb2.ActionRequest(
            session_id=self.session_id,
            jammer_id=jammer_id,
            power_dbm=power_dbm,
            jam_freq=jam_freq,
        )
        rep = self.stub.ExecuteAction(req)
        if not rep.success:
            raise RuntimeError(
                f"ExecuteAction failed: {rep.error} "
                f"(session={self.session_id}, jammer={jammer_id})"
            )

    def step_simulation(self):
        req = agent_service_pb2.StepRequest(session_id=self.session_id)
        rep = self.stub.StepSimulation(req)
        if not rep.success:
            raise RuntimeError(
                f"StepSimulation failed: {rep.error} "
                f"(session={self.session_id})"
            )
        self.last_jammer_ids = list(rep.jammer_ids)
        self.last_jammer_modes = list(rep.jammer_modes)
        return rep.results

    def reset(self):
        pass

    def close(self):
        self.channel.close()


if __name__ == "__main__":
    import sys
    sid = sys.argv[1] if len(sys.argv) > 1 else "session_1"
    client = ECMSimClient()
    client.session_id = sid

    state = client.get_state(1)
    logging.info(f"State (dim={len(state)}): {[round(v, 3) for v in state]}")

    client.execute_action(1, 45.0, 10.5e9)
    logging.info("Action executed: power=45dBm, freq=10.5GHz")

    results = client.step_simulation()
    for r in results:
        logging.info(f"R{r.radar_id}: SINR={r.sinr_db:.1f}dB detect={r.detect_success}")

    client.close()
