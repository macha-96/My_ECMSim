#ifndef SIM_SCENE_H
#define SIM_SCENE_H

#include "../entity/radar.h"
#include "../entity/jammer.h"
#include <vector>
#include <map>

namespace ECMSim {

// 单雷达仿真结果结构体（扩展：含多维度评估）
struct RadarSimResult {
    int radar_id;
    double signal_power;    // 回波功率 W
    double total_jam_power; // 所有干扰机总干扰功率 W（空间到达）
    double noise_power;
    double sinr_lin;
    double sinr_db;
    bool detect_ok;

    // === 频域匹配参数（每干扰机） ===
    std::vector<int>    jammer_ids;            // 干扰机ID列表
    std::vector<double> jam_freq_deltas;       // 各干扰机频差 Δf (Hz)
    std::vector<double> freq_match_factors;    // 各干扰机频域匹配系数 ζ
    std::vector<double> original_jam_powers;   // 各干扰机空间到达功率 Pj_space (W)
    std::vector<double> effective_jam_powers;  // 各干扰机有效干扰功率 Pj_eff (W)

    // === 多维度评估指标 ===
    double total_effective_jam_power;  // 总有效干扰功率（滤波后）W
    double jsr_lin;                    // 干信比 JSR = Pj_eff / Ps (线性)
    double jsr_db;                     // 干信比 dB
    double jam_success_score;          // 0~1 归一化干扰成功率

    // === 欺骗干扰专属 ===
    bool   is_deception_active;        // 是否存在有效欺骗干扰
    double decept_effect_score;        // 欺骗干扰效能分数 0~1
};

class SimScene {
public:
    void addRadar(const Radar& r);
    void addJammer(const Jammer& j);

    // 全场景批量计算所有雷达的SINR（含频域匹配+多维度评估）
    std::vector<RadarSimResult> runOneStep();

    void clearAll();

    // === DQN智能干扰预留接口 ===
    // 获取指定干扰机的标准化状态向量（供Python强化学习框架）
    std::vector<double> getStateForJammer(int jammer_id) const;
    // 执行智能动作：设置干扰功率(dBm)和频率(Hz)
    void executeJammerAction(int jammer_id, double power_dbm, double freq);
    // 计算单步奖励：含干扰效果、功耗、瞄频精度
    double calcJammerReward(int jammer_id) const;

    // 场景数据读取（供DQN接口使用）
    const std::vector<Radar>&  getRadars()  const { return m_radars; }
    const std::vector<Jammer>& getJammers() const { return m_jammers; }
    // 查找指定ID的干扰机（可变引用，供DQN动作执行）
    Jammer* findJammer(int id);

private:
    std::vector<Radar>  m_radars;
    std::vector<Jammer> m_jammers;
};

}

#endif