#ifndef RADAR_EQ_H
#define RADAR_EQ_H

#include "math_const.h"
#include <vector>

namespace ECMAlgo {

// 自由空间路径损耗 距离r(m), 频率f(Hz) 返回损耗线性值
double freeSpaceLoss(double r, double freq);

// 雷达回波功率计算
// Pt:发射功率(W), Gt:发射增益(线性), Gr接收增益(线性), sigma:RCS(m²),
// r:目标距离(m), freq:工作频率(Hz)
double radarEchoPower(double Pt, double Gt, double Gr, double sigma, double r, double freq);

// 干扰到达雷达天线功率
// Pj干扰功率W, Gj干扰天线增益线性, Gr雷达接收增益线性, r距离m, freq频率Hz
double jammerReceivePower(double Pj, double Gj, double Gr, double r, double freq);

// 接收机热噪声功率 B:带宽Hz
double thermalNoisePower(double B);

// 计算SINR(线性值)：信号功率 / (噪声功率 + 干扰功率)
double calcSINR(double signal_pow, double jam_pow, double noise_pow);

// 二维距离计算 (x1,y1)雷达坐标 (x2,y2)干扰/目标坐标
double calc2DDistance(double x1, double y1, double x2, double y2);

}

#endif