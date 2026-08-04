#ifndef MATH_CONST_H
#define MATH_CONST_H

#include <cmath>

// 光速 m/s
constexpr double C = 3e8;
// 玻尔兹曼常数 J/K
constexpr double K_BOLTZ = 1.38e-23;
// 标准室温 290K
constexpr double T0 = 290.0;
// 分贝转换系数
constexpr double DB_COEFF = 10.0;

// 线性值转dB
inline double lin2db(double val) {
    return DB_COEFF * std::log10(val);
}

// dB转线性值
inline double db2lin(double db_val) {
    return std::pow(10.0, db_val / DB_COEFF);
}

#endif