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

// ========== 瞄频干扰频域匹配模型 ==========

// 矩形窗频域匹配系数（工程标准压制干扰）
// Δf:频差Hz, B:雷达带宽Hz, 返回[0,1]
double freqMatchRect(double delta_f, double B);

// 高斯平滑频域匹配系数（智能训练用，连续梯度）
// Δf:频差Hz, B:雷达带宽Hz, σ=B/4, 返回[0,1]
double freqMatchGauss(double delta_f, double B);

// 计算有效干扰功率（含频域匹配衰减）
// Pj_space:空间到达功率W, delta_f:频差Hz, B:带宽Hz, useGauss:是否使用高斯模型
double effectiveJamPower(double Pj_space, double delta_f, double B, bool useGauss = false);

// 计算干信比JSR(线性值)：有效干扰功率 / 信号功率
double calcJSR(double jam_power, double signal_power);

}

#endif