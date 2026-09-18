#include <sence/sim_scene.h>
#include <algo/radar_eq.h>
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
    double dist_target = 10000.0;

    for (const auto& radar : m_radars) {
        RadarSimResult res;
        res.radar_id = radar.getId();
        auto [rx, ry] = radar.getPos();
        double freq_r = radar.getFreq();
        double B_r   = radar.getBandwidth();

        // 1. 计算目标回波功率（目标默认在雷达原点等效距离演示）
        // 此处简化：假设目标与雷达距离10km，可后续扩展目标实体
        res.signal_power = ECMAlgo::radarEchoPower(
            radar.getPtLin(), radar.getGLin(), radar.getGLin(),
            radar.getRCS(), dist_target, freq_r);

        // 2. 遍历所有干扰机，计算频域匹配+有效干扰功率
        double sum_jam_space   = 0.0;
        double sum_jam_eff     = 0.0;
        double max_decept_zeta = 0.0;
        int    decept_jam_id   = -1;

        for (const auto& jam : m_jammers) {
            auto [jx, jy] = jam.getPos();
            double dist = ECMAlgo::calc2DDistance(rx, ry, jx, jy);
            double freq_j = jam.getJamFreq();
            double delta_f = freq_j - freq_r;

            // 空间到达功率（原有公式不变）
            double Pj_space = ECMAlgo::jammerReceivePower(
                jam.getPjLin(), jam.getGjLin(),
                radar.getGLin(), dist, freq_j);

            // 根据干扰样式选择频域匹配模型
            double zeta = 0.0;
            if (jam.getJamType() == JamType::NOISE_JAM) {
                // 压制干扰：矩形窗硬截止模型（工程标准）
                zeta = ECMAlgo::freqMatchRect(delta_f, B_r);
            } else {
                // 欺骗干扰：高斯平滑模型（训练用，连续梯度）
                zeta = ECMAlgo::freqMatchGauss(delta_f, B_r);
            }

            double Pj_eff = Pj_space * zeta;

            // 记录每个干扰机的详细数据
            res.jammer_ids.push_back(jam.getId());
            res.jam_freq_deltas.push_back(delta_f);
            res.freq_match_factors.push_back(zeta);
            res.original_jam_powers.push_back(Pj_space);
            res.effective_jam_powers.push_back(Pj_eff);

            // 累加（压制干扰参与SINR计算，欺骗干扰不降低SINR）
            if (jam.getJamType() == JamType::NOISE_JAM) {
                sum_jam_space += Pj_space;
                sum_jam_eff   += Pj_eff;
            } else {
                // 欺骗干扰仅记录，不参与SINR分母累加
                if (zeta > max_decept_zeta) {
                    max_decept_zeta = zeta;
                    decept_jam_id   = jam.getId();
                }
            }
        }

        res.total_jam_power        = sum_jam_space;
        res.total_effective_jam_power = sum_jam_eff;
        res.noise_power            = radar.getNoisePower();

        // 3. SINR计算（仅压制干扰参与分母）
        res.sinr_lin = ECMAlgo::calcSINR(res.signal_power, sum_jam_eff, res.noise_power);
        res.sinr_db  = lin2db(res.sinr_lin);

        // 4. 多维度评估指标
        res.jsr_lin = ECMAlgo::calcJSR(sum_jam_eff, res.signal_power);
        res.jsr_db  = lin2db(res.jsr_lin);

        // 5. 压制干扰检测判定
        res.detect_ok = radar.isDetectSuccess(res.sinr_lin);

        // 6. 欺骗干扰评估
        res.is_deception_active = (max_decept_zeta >= 0.8 && decept_jam_id >= 0);
        if (res.is_deception_active) {
            // 假目标幅度 = 欺骗信号强度（用干扰机的空间功率近似）
            double decept_power = 0.0;
            for (size_t i = 0; i < res.jammer_ids.size(); i++) {
                if (res.jammer_ids[i] == decept_jam_id) {
                    decept_power = res.original_jam_powers[i];
                    break;
                }
            }
            double target_power = res.signal_power;
            double power_ratio  = decept_power / (target_power + 1e-30);
            // 假目标优势度：假目标能量大于真目标时 >1
            double advantage = std::min(power_ratio, 10.0) / 10.0;
            res.decept_effect_score = 0.4 * max_decept_zeta + 0.4 * advantage + 0.2 * 1.0;
            res.decept_effect_score = std::min(res.decept_effect_score, 1.0);
        } else {
            res.decept_effect_score = 0.0;
        }

        // 7. 统一干扰成功率评分 (0~1)
        bool has_noise_jammer = false;
        double avg_zeta_noise = 0.0;
        int noise_count = 0;
        for (size_t i = 0; i < res.jammer_ids.size(); i++) {
            // Need to check jammer type by finding the jammer
            for (const auto& jam : m_jammers) {
                if (jam.getId() == res.jammer_ids[i]) {
                    if (jam.getJamType() == JamType::NOISE_JAM) {
                        has_noise_jammer = true;
                        avg_zeta_noise += res.freq_match_factors[i];
                        noise_count++;
                    }
                    break;
                }
            }
        }
        if (noise_count > 0) avg_zeta_noise /= noise_count;

        if (has_noise_jammer) {
            // 压制干扰评分
            double sinr_suppression = 1.0 - res.sinr_lin / (res.sinr_lin + radar.getThreshLin());
            double freq_match_score = avg_zeta_noise;
            double power_eff = (sum_jam_space > 1e-30) ? (sum_jam_eff / sum_jam_space) : 0.0;
            res.jam_success_score = 0.5 * sinr_suppression + 0.3 * freq_match_score + 0.2 * power_eff;
            res.jam_success_score = std::max(0.0, std::min(res.jam_success_score, 1.0));
        } else {
            // 欺骗干扰评分
            res.jam_success_score = res.is_deception_active ? res.decept_effect_score : 0.0;
        }

        res_list.push_back(res);
    }
    return res_list;
}

void SimScene::clearAll() {
    m_radars.clear();
    m_jammers.clear();
}

// ========== DQN预留接口 ==========

std::vector<double> SimScene::getStateForJammer(int jammer_id) const {
    // 返回标准化状态向量: [雷达位置, 雷达频率, 干扰机位置, 干扰机频率, 距离, 频差]
    // 后续对接Python RL框架时使用
    std::vector<double> state;
    // 查找指定干扰机
    const Jammer* jam = nullptr;
    for (const auto& j : m_jammers) {
        if (j.getId() == jammer_id) { jam = &j; break; }
    }
    if (!jam) return state;

    auto [jx, jy] = jam->getPos();
    // 遍历所有雷达，构造状态
    for (const auto& radar : m_radars) {
        auto [rx, ry] = radar.getPos();
        double dist = ECMAlgo::calc2DDistance(rx, ry, jx, jy);
        double delta_f = std::abs(jam->getJamFreq() - radar.getFreq());
        // 归一化状态值
        state.push_back(rx / 20000.0);        // 雷达X [-1,1] 归一化
        state.push_back(ry / 20000.0);        // 雷达Y
        state.push_back(radar.getFreq() / 20e9); // 雷达频率 [0,1] 归一化
        state.push_back(radar.getBandwidth() / 10e6); // 带宽 [0,1]
        state.push_back(radar.getPtLin() / 1000.0); // 雷达功率 [0,1] (max 60dBm=1000W)
        state.push_back(dist / 30000.0);      // 距离 [0,1]
        state.push_back(delta_f / 10e9);      // 频差 [0,1]
    }
    // 干扰机自身状态
    state.push_back(jam->getPjLin() / 1000.0);  // 功率 [0,1] (max 60dBm=1000W)
    state.push_back(jam->getJamFreq() / 20e9); // 频率 [0,1]

    return state;
}

void SimScene::executeJammerAction(int jammer_id, double power_dbm, double freq) {
    Jammer* jam = findJammer(jammer_id);
    if (!jam) return;
    jam->setJamPowerDBm(power_dbm);
    jam->setJamFreq(freq);
}

double SimScene::calcJammerReward(int jammer_id) const {
    // 暂存: 简单评分，后续由Python RL框架接管
    // R = α·Score - β·Pcost - γ·Δf_norm
    const double alpha = 1.0, beta = 0.5, gamma = 0.3;

    const Jammer* jam = nullptr;
    for (const auto& j : m_jammers) {
        if (j.getId() == jammer_id) { jam = &j; break; }
    }
    if (!jam) return 0.0;

    double score    = 0.5;  // 暂估
    double Pcost    = jam->getPjLin() / 1000.0;  // 归一化功耗
    double delta_f  = 0.0;
    if (!m_radars.empty()) {
        delta_f = std::abs(jam->getJamFreq() - m_radars[0].getFreq()) / 10e9;
    }
    return alpha * score - beta * Pcost - gamma * delta_f;
}

Jammer* SimScene::findJammer(int id) {
    for (auto& j : m_jammers) {
        if (j.getId() == id) return &j;
    }
    return nullptr;
}

}