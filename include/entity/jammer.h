#ifndef JAMMER_H
#define JAMMER_H

#include "../algo/radar_eq.h"
#include <string>

namespace ECMSim {

// 干扰样式枚举
enum class JamType {
    NOISE_JAM,    // 噪声压制干扰
    RANGE_DECEPT  // 距离欺骗干扰
};

class Jammer {
public:
    Jammer(int id, double x, double y, double Pj_dBm, double Gj_dB, double jam_freq, JamType type);

    void setPos(double x, double y);
    std::pair<double, double> getPos() const;

    // 调整干扰功率(dBm)
    void setJamPowerDBm(double dbm);
    // 调整瞄频干扰频率
    void setJamFreq(double freq);

    int getId() const;
    double getPjLin() const;
    double getGjLin() const;
    double getJamFreq() const;
    JamType getJamType() const;

private:
    int m_id;
    double m_x, m_y;
    double m_Pj_lin;
    double m_Gj_lin;
    double m_jam_freq;
    JamType m_type;
};

}

#endif