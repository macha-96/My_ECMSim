#include <sence/sim_scene.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <jsoncpp/json/json.h>

// 字符串转干扰类型枚举
ECMSim::JamType strToJamType(const std::string& typeStr) {
    if (typeStr == "NOISE_JAM")
        return ECMSim::JamType::NOISE_JAM;
    else if (typeStr == "RANGE_DECEPT")
        return ECMSim::JamType::RANGE_DECEPT;
    throw std::runtime_error("unknown jam type in json config");
}

// 加载配置json，生成SimScene
ECMSim::SimScene loadSceneFromJson(const std::string& jsonPath) {
    ECMSim::SimScene scene;
    std::ifstream ifs(jsonPath);
    if (!ifs.is_open()) {
        throw std::runtime_error("cannot open config file: " + jsonPath);
    }

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(ifs, root, false)) {
        throw std::runtime_error("json parse error: " + reader.getFormattedErrorMessages());
    }

    // 解析雷达数组
    Json::Value radarArr = root["radars"];
    for (auto& rJson : radarArr) {
        int id = rJson["id"].asInt();
        double x = rJson["x"].asDouble();
        double y = rJson["y"].asDouble();
        double Pt_dBm = rJson["Pt_dBm"].asDouble();
        double G_dB = rJson["G_dB"].asDouble();
        double freq = rJson["freq"].asDouble();
        double bw = rJson["bandwidth"].asDouble();
        double sigma = rJson["sigma"].asDouble();
        double thresh_db = rJson["thresh_db"].asDouble();

        ECMSim::Radar r(id, x, y, Pt_dBm, G_dB, freq, bw, sigma, thresh_db);
        scene.addRadar(r);
    }

    // 解析干扰机数组
    Json::Value jamArr = root["jammers"];
    for (auto& jJson : jamArr) {
        int id = jJson["id"].asInt();
        double x = jJson["x"].asDouble();
        double y = jJson["y"].asDouble();
        double Pj_dBm = jJson["Pj_dBm"].asDouble();
        double Gj_dB = jJson["Gj_dB"].asDouble();
        double jam_freq = jJson["jam_freq"].asDouble();
        std::string typeStr = jJson["jam_type"].asString();
        ECMSim::JamType jt = strToJamType(typeStr);

        ECMSim::Jammer j(id, x, y, Pj_dBm, Gj_dB, jam_freq, jt);
        scene.addJammer(j);
    }

    ifs.close();
    return scene;
}

// 将仿真结果数组转为Json::Value
Json::Value simResultToJson(const std::vector<ECMSim::RadarSimResult>& results) {
    Json::Value root;
    Json::Value arr(Json::arrayValue);

    for (const auto& res : results) {
        Json::Value item;
        item["radar_id"] = res.radar_id;
        item["signal_power_W"] = res.signal_power;
        item["total_jam_power_W"] = res.total_jam_power;
        item["noise_power_W"] = res.noise_power;
        item["SINR_lin"] = res.sinr_lin;
        item["SINR_dB"] = res.sinr_db;
        item["detect_success"] = res.detect_ok;
        arr.append(item);
    }
    root["sim_results"] = arr;
    return root;
}

int main(int argc, char** argv) {
    try {
        // 配置文件路径，可通过命令行传参，默认config/scene_config.json
        std::string cfgPath = "config/scene_config.json";
        if (argc >= 2) {
            cfgPath = argv[1];
        }

        // 1. 从JSON加载场景
        ECMSim::SimScene scene = loadSceneFromJson(cfgPath);
        std::cout << "=== Load scene config from " << cfgPath << " success ===" << std::endl;

        // 2. 执行一步仿真
        std::vector<ECMSim::RadarSimResult> simRes = scene.runOneStep();

        // 3. 转换结果为JSON并格式化输出
        Json::Value resJson = simResultToJson(simRes);
        Json::StyledWriter writer;
        std::string jsonOut = writer.write(resJson);

        std::cout << "\n========== Simulation Result(JSON Format) ==========\n";
        std::cout << jsonOut << std::endl;

        // 可选：将结果写入result_output.json文件
        std::ofstream outFile("result_output.json");
        outFile << jsonOut;
        outFile.close();
        std::cout << "\nResult saved to result_output.json" << std::endl;
    }catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}