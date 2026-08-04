#include <algo/radar_eq.h>
#include <cmath>

/**
 * @namespace ECMAlgo
 * @brief 电子对抗电磁仿真核心算法命名空间
 * 封装雷达方程、自由空间损耗、干扰功率、SINR、几何距离等底层电磁计算公式
 * 所有物理量单位统一：距离m、频率Hz、功率W、带宽Hz、RCS m²
 */
namespace ECMAlgo {

/**
 * @brief 计算自由空间传播损耗（线性值，非dB）
 * @param r 收发端直线距离，单位m
 * @param freq 电磁波工作频率，单位Hz
 * @return 自由空间路径损耗线性值，数值越大衰减越严重
 * @formula L = [(4πr)/λ]^2 ，λ = C/freq
 * @note λ为波长，C为真空光速常量；自由空间无多径、无遮挡理想传播模型
 */
double freeSpaceLoss(double r, double freq) {
    // 计算电磁波波长 λ = 光速 / 工作频率
    double lambda = C / freq;
    // 自由空间损耗标准公式，输出线性损耗系数
    double loss_lin = std::pow((4 * M_PI * r) / lambda, 2.0);
    return loss_lin;
}

/**
 * @brief 雷达目标回波接收功率计算（雷达方程）
 * @param Pt 雷达发射功率，单位W（线性值，非dB）
 * @param Gt 雷达发射天线增益，线性值
 * @param Gr 雷达接收天线增益，线性值
 * @param sigma 目标雷达散射截面积RCS，单位m²
 * @param r 雷达到目标的距离，单位m
 * @param freq 雷达工作载波频率，单位Hz
 * @return 雷达接收机收到的目标回波功率，单位W
 * @formula Pr = (Pt * Gt * Gr * σ * λ²) / [(4π)^3 * r^4]
 * @note 回波功率与距离4次方成反比，距离越远功率衰减极快
 */
double radarEchoPower(double Pt, double Gt, double Gr, double sigma, double r, double freq) {
    // 获取电磁波波长
    double lambda = C / freq;
    // 标准雷达方程计算目标回波功率
    double Pr = (Pt * Gt * Gr * sigma * lambda * lambda) / std::pow(4 * M_PI, 3) / std::pow(r, 4);
    return Pt * Gt * Gr * sigma * lambda * lambda / (std::pow(4 * M_PI, 3) * std::pow(r, 4));
}

/**
 * @brief 雷达天线收到的干扰机辐射功率计算（干扰方程）
 * @param Pj 干扰机发射功率，单位W（线性值）
 * @param Gj 干扰机发射天线增益，线性值
 * @param Gr 雷达接收天线增益，线性值
 * @param r 干扰机到雷达的直线距离，单位m
 * @param freq 干扰信号发射频率，单位Hz
 * @return 雷达接收机输入端的干扰信号功率，单位W
 * @formula Pjr = (Pj * Gj * Gr * λ²) / (4πr)^2
 * @note 干扰功率随距离平方衰减，相比目标回波衰减更慢，远距离压制优势明显
 */
double jammerReceivePower(double Pj, double Gj, double Gr, double r, double freq) {
    // 计算信号波长
    double lambda = C / freq;
    // 干扰信号到达雷达天线功率公式
    return (Pj * Gj * Gr * lambda * lambda) / std::pow(4 * M_PI * r, 2);
}

/**
 * @brief 计算接收机热噪声基底功率
 * @param B 雷达接收机瞬时带宽，单位Hz
 * @return 热噪声功率，单位W
 * @formula Pn = k * T0 * B
 * @param K_BOLTZ 玻尔兹曼常数，T0标准室温290K（行业通用常温噪声模型）
 */
double thermalNoisePower(double B) {
    return K_BOLTZ * T0 * B;
}

/**
 * @brief 计算信干噪比SINR（线性值）
 * @param signal_pow 目标回波有用信号功率 W
 * @param jam_pow 外部干扰总功率 W
 * @param noise_pow 接收机热噪声功率 W
 * @return SINR线性值；值越大雷达检测性能越好
 * @formula SINR = Ps / (Pn + Pj_total)
 * @note 分母为总干扰（噪声+人为干扰）；极小值保护：分母接近0时返回极大值避免除零崩溃
 */
double calcSINR(double signal_pow, double jam_pow, double noise_pow) {
    // 总干扰功率 = 热噪声 + 所有干扰机叠加干扰
    double total_interf = noise_pow + jam_pow;
    // 数值保护：防止分母为0造成浮点异常
    if (total_interf <= 1e-20)
        return 1e12;
    // 信干噪比 = 有用信号 / 总干扰
    return signal_pow / total_interf;
}

/**
 * @brief 二维平面两点欧氏距离计算
 * @param x1 点1横坐标（雷达/干扰机X坐标）
 * @param y1 点1纵坐标
 * @param x2 点2横坐标
 * @param y2 点2纵坐标
 * @return 两点直线距离，单位m
 * @note 用于仿真场景中雷达、干扰机、目标之间距离求解
 */
double calc2DDistance(double x1, double y1, double x2, double y2) {
    // X、Y轴坐标差值
    double dx = x1 - x2;
    double dy = y1 - y2;
    // 勾股定理计算欧式距离
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace ECMAlgo