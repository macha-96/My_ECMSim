#ifndef RADAR_H
#define RADAR_H

#include "../algo/radar_eq.h"
#include <string>

namespace ECMSim {

class Radar {
public:
    // 构造函数：ID、坐标、发射功率(W)、增益dB、工作频率Hz、带宽Hz、RCS、检测门限dB
    Radar(int id, double x, double y, double Pt_dBm, double G_dB, double freq, double B, double sigma, double thresh_db);

    // 坐标读写
    void setPos(double x, double y);
    std::pair<double, double> getPos() const;

    // 参数获取
    int getId() const;
    double getFreq() const;
    double getPtLin() const;
    double getGLin() const;
    double getBandwidth() const;
    double getRCS() const;
    double getThreshLin() const;

    // 热噪声功率缓存
    double getNoisePower() const;

    // 判断是否检测成功：输入SINR线性值
    bool isDetectSuccess(double sinr_lin) const;

private:
    int m_id;
    double m_x, m_y;
    double m_Pt_lin;    // 发射功率线性(W)
    double m_G_lin;     // 天线增益线性
    double m_freq;      // 工作频率 Hz
    double m_BW;        // 接收机带宽 Hz
    double m_sigma;     // 目标RCS m²
    double m_thresh_lin;// 检测门限线性
    double m_noise_pow; // 预计算热噪声
};

}

#endif