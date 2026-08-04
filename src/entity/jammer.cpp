#include <entity/jammer.h>

namespace ECMSim {

Jammer::Jammer(int id, double x, double y, double Pj_dBm, double Gj_dB, double jam_freq, JamType type)
    : m_id(id), m_x(x), m_y(y), m_jam_freq(jam_freq), m_type(type) {
    double Pj_mw = db2lin(Pj_dBm);
    m_Pj_lin = Pj_mw * 1e-3;
    m_Gj_lin = db2lin(Gj_dB);
}

void Jammer::setPos(double x, double y) {
    m_x = x;
    m_y = y;
}

std::pair<double, double> Jammer::getPos() const { return {m_x, m_y}; }

void Jammer::setJamPowerDBm(double dbm) {
    double mw = db2lin(dbm);
    m_Pj_lin = mw * 1e-3;
}

void Jammer::setJamFreq(double freq) { m_jam_freq = freq; }

int Jammer::getId() const { return m_id; }
double Jammer::getPjLin() const { return m_Pj_lin; }
double Jammer::getGjLin() const { return m_Gj_lin; }
double Jammer::getJamFreq() const { return m_jam_freq; }
JamType Jammer::getJamType() const { return m_type; }

}