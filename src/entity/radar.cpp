#include <entity/radar.h>

namespace ECMSim {

Radar::Radar(int id, double x, double y, double Pt_dBm, double G_dB, double freq, double B, double sigma, double thresh_db)
    : m_id(id), m_x(x), m_y(y), m_freq(freq), m_BW(B), m_sigma(sigma) {
    // dBm转W：0dBm=1mW=0.001W
    double Pt_mw = db2lin(Pt_dBm);
    m_Pt_lin = Pt_mw * 1e-3;
    m_G_lin = db2lin(G_dB);
    m_thresh_lin = db2lin(thresh_db);
    m_noise_pow = ECMAlgo::thermalNoisePower(m_BW);
}

void Radar::setPos(double x, double y) {
    m_x = x;
    m_y = y;
}

std::pair<double, double> Radar::getPos() const {
    return {m_x, m_y};
}

int Radar::getId() const { return m_id; }
double Radar::getFreq() const { return m_freq; }
double Radar::getPtLin() const { return m_Pt_lin; }
double Radar::getGLin() const { return m_G_lin; }
double Radar::getBandwidth() const { return m_BW; }
double Radar::getRCS() const { return m_sigma; }
double Radar::getThreshLin() const { return m_thresh_lin; }
double Radar::getNoisePower() const { return m_noise_pow; }

bool Radar::isDetectSuccess(double sinr_lin) const { return sinr_lin >= m_thresh_lin; }

}