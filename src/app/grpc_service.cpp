#include <app/grpc_service.h>
#include <app/common.h>

grpc::Status AgentSvc::GetState(
    grpc::ServerContext*,
    const ecmsim::StateRequest* rq,
    ecmsim::StateResponse* rp) {
    auto s = g_scene_mgr.getStateForJammer(rq->session_id(), rq->jammer_id());
    if (s.empty()) {
        rp->set_success(false);
        rp->set_error("not found");
        return grpc::Status::OK;
    }
    for (double v : s) rp->add_state(v);
    rp->set_success(true);
    return grpc::Status::OK;
}

grpc::Status AgentSvc::ExecuteAction(
    grpc::ServerContext*,
    const ecmsim::ActionRequest* rq,
    ecmsim::ActionResponse* rp) {
    bool ok = g_scene_mgr.executeJammerAction(
        rq->session_id(), rq->jammer_id(), rq->power_dbm(), rq->jam_freq());
    rp->set_success(ok);
    if (!ok) {
        rp->set_error("not found");
    } else {
        broadcastScene(rq->session_id());
    }
    return grpc::Status::OK;
}

grpc::Status AgentSvc::StepSimulation(
    grpc::ServerContext*,
    const ecmsim::StepRequest* rq,
    ecmsim::StepResponse* rp) {
    auto rs = g_scene_mgr.runSimulation(rq->session_id());
    if (rs.empty()) {
        rp->set_success(false);
        rp->set_error("no radars");
        return grpc::Status::OK;
    }
    for (const auto& r : rs) {
        auto* p = rp->add_results();
        p->set_radar_id(r.radar_id);
        p->set_sinr_db(r.sinr_db);
        p->set_detect_success(r.detect_ok);
        p->set_jam_success_score(r.jam_success_score);
        p->set_total_effective_jam_power(r.total_effective_jam_power);
        p->set_jsr_db(r.jsr_db);
        p->set_is_deception_active(r.is_deception_active);
        p->set_decept_effect_score(r.decept_effect_score);
        for (double d : r.jam_freq_deltas) p->add_jam_freq_deltas(d);
        for (double z : r.freq_match_factors) p->add_freq_match_factors(z);
    }
    rp->set_success(true);
    // broadcastScene(rq->session_id());
    return grpc::Status::OK;
}
