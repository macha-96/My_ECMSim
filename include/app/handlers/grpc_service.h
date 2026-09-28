#pragma once

#include <grpcpp/grpcpp.h>
#include <grpc_gen/agent_service.grpc.pb.h>

class AgentSvc final : public ecmsim::AgentService::Service {
    grpc::Status GetState(
        grpc::ServerContext*,
        const ecmsim::StateRequest*,
        ecmsim::StateResponse*) override;

    grpc::Status ExecuteAction(
        grpc::ServerContext*,
        const ecmsim::ActionRequest*,
        ecmsim::ActionResponse*) override;

    grpc::Status StepSimulation(
        grpc::ServerContext*,
        const ecmsim::StepRequest*,
        ecmsim::StepResponse*) override;
};
