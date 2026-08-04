#include <sence/sim_scene.h>
#include <iostream>

namespace ECMSim {

void SimScene::addRadar(const Radar& r) {
    m_radars.push_back(r);
}

void SimScene::addJammer(const Jammer& j) {
    m_jammers.push_back(j);
}

std::vector<RadarSimResult> SimScene::runOneStep() {
    std::vector<RadarSimResult> res_list;
    for (const auto& radar : m_radars) {
        RadarSimResult res;
        res.radar_id = radar.getId();
        auto [rx, ry] = radar.getPos();
        double freq_r = radar.getFreq();

        // 1. 计算目标回波功率（目标默认在雷达原点等效距离演示）
        // 此处简化：假设目标与雷达距离10km，可后续扩展目标实体
        double dist_target = 10000.0;
        res.signal_power = ECMAlgo::radarEchoPower(
            radar.getPtLin(),
            radar.getGLin(),
            radar.getGLin(),
            radar.getRCS(),
            dist_target,
            freq_r
        );

        // 2. 累加所有干扰机到达该雷达的干扰功率
        double sum_jam = 0.0;
        for (const auto& jam : m_jammers) {
            auto [jx, jy] = jam.getPos();
            double dist = ECMAlgo::calc2DDistance(rx, ry, jx, jy);
            double pj = ECMAlgo::jammerReceivePower(
                jam.getPjLin(),
                jam.getGjLin(),
                radar.getGLin(),
                dist,
                jam.getJamFreq()
            );
            sum_jam += pj;
        }
        res.total_jam_power = sum_jam;
        res.noise_power = radar.getNoisePower();

        // 3. SINR计算
        res.sinr_lin = ECMAlgo::calcSINR(res.signal_power, sum_jam, res.noise_power);
        res.sinr_db = lin2db(res.sinr_lin);
        res.detect_ok = radar.isDetectSuccess(res.sinr_lin);

        res_list.push_back(res);
    }
    return res_list;
}

void SimScene::clearAll() {
    m_radars.clear();
    m_jammers.clear();
}

}