#ifndef SIM_SCENE_H
#define SIM_SCENE_H

#include "../entity/radar.h"
#include "../entity/jammer.h"
#include <vector>
#include <map>

namespace ECMSim {

// 单雷达仿真结果结构体
struct RadarSimResult {
    int radar_id;
    double signal_power;    // 回波功率 W
    double total_jam_power; // 所有干扰机总干扰功率 W
    double noise_power;
    double sinr_lin;
    double sinr_db;
    bool detect_ok;
};

class SimScene {
public:
    void addRadar(const Radar& r);
    void addJammer(const Jammer& j);

    // 全场景批量计算所有雷达的SINR
    std::vector<RadarSimResult> runOneStep();

    void clearAll();

private:
    std::vector<Radar> m_radars;
    std::vector<Jammer> m_jammers;
};

}

#endif