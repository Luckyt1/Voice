/**
 * voice_recognition_node.cpp
 * 语音识别ROS2节点：麦克风 -> AIUI识别 -> ROS2话题发布
 * 
 * 功能：
 * 1. 使用AIUI SDK进行语音识别
 * 2. 发布识别结果到ROS2话题 /voice_recognition
 * 3. 唤醒词检测功能
 * 
 * 编译方法：需要在 CMakeLists.txt 中添加此文件
 * 运行方法：ros2 run wheeltec_mic_aiui voice_recognition_node
 */

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <alsa/asoundlib.h>
#include "aiui/AIUI_V2.h"
#include <signal.h>
#include <unistd.h>
#include "json/json.h"

// ROS2 头文件
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>

// 网络接口获取MAC地址
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <cstring>

using namespace aiui_v2;

// AIUI工作目录
#define AIUI_ROOT_DIR "/home/tang/voice/ws_voice/src/wheeltec_mic_aiui/AIUI/"
#define AIUI_MSC_DIR  "/home/tang/voice/ws_voice/src/wheeltec_mic_aiui/AIUI/msc/"

// ========== 全局变量 ==========
static IAIUIAgent* g_agent = nullptr;
static std::atomic<bool> g_running(true);
static std::atomic<bool> g_is_awake(false);  // 唤醒状态

// ROS2节点指针
static rclcpp::Node::SharedPtr g_node = nullptr;
static rclcpp::Publisher<std_msgs::msg::String>::SharedPtr g_voice_pub = nullptr;
static rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr g_wakeup_pub = nullptr;

// ========== 工具函数 ==========

// 获取可执行文件所在目录
static std::string getExePath() {
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (len != -1) {
        buf[len] = '\0';
        std::string path(buf);
        return path.substr(0, path.rfind('/'));
    }
    return ".";
}

// 读取文件内容
static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "无法打开文件: " << path << std::endl;
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 发布识别结果到ROS2话题
static void publishVoiceResult(const std::string& text) {
    if (g_voice_pub && g_node) {
        std_msgs::msg::String msg;
        msg.data = text;
        g_voice_pub->publish(msg);
        // RCLCPP_INFO(g_node->get_logger(), "发布识别结果: %s", text.c_str());
    }
}

// 发布唤醒状态到ROS2话题
static void publishWakeupStatus(bool is_awake) {
    if (g_wakeup_pub && g_node) {
        std_msgs::msg::Bool msg;
        msg.data = is_awake;
        g_wakeup_pub->publish(msg);
        // RCLCPP_INFO(g_node->get_logger(), "唤醒状态: %s", is_awake ? "已唤醒" : "待唤醒");
    }
}

// 获取MAC地址作为设备唯一标识
static void getMACAddress(char* mac) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        strcpy(mac, "00:00:00:00:00:00");
        return;
    }

    struct ifconf ifc{};
    char buf[1024];
    int success = 0;

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
        close(sock);
        strcpy(mac, "00:00:00:00:00:01");
        return;
    }

    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
    struct ifreq ifr{};

    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
            if (!(ifr.ifr_flags & IFF_LOOPBACK)) {
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                    success = 1;
                    break;
                }
            }
        }
    }

    unsigned char mac_address[6] = {0};
    if (success) {
        memcpy(mac_address, ifr.ifr_hwaddr.sa_data, 6);
    }

    sprintf(mac,
            "%02x:%02x:%02x:%02x:%02x:%02x",
            mac_address[0], mac_address[1], mac_address[2],
            mac_address[3], mac_address[4], mac_address[5]);
    close(sock);
}

// 初始化AIUI设置
static void initAIUISetting() {
    // 设置AIUI工作目录
    AIUISetting::setAIUIDir(AIUI_ROOT_DIR);
    AIUISetting::setMscDir(AIUI_MSC_DIR);
    AIUISetting::setNetLogLevel(aiui_debug);

    // 获取MAC地址作为序列号
    char mac[64] = {0};
    getMACAddress(mac);
    
    std::cout << "[AIUI] 设备序列号(MAC): " << mac << std::endl;
    
    // 设置设备序列号
    AIUISetting::setSystemInfo(AIUI_KEY_SERIAL_NUM, mac);
}

// ========== AIUI结果监听器 ==========
class VoiceRecognitionListener : public IAIUIListener {
public:
    VoiceRecognitionListener() : m_iatTextBuffer("") {}

    void onEvent(const IAIUIEvent& event) override {
        switch (event.getEventType()) {
            // SDK状态事件
            case AIUIConstant::EVENT_STATE:
                switch (event.getArg1()) {
                    case AIUIConstant::STATE_IDLE:
                        std::cout << "[状态] 空闲" << std::endl;
                        break;
                    case AIUIConstant::STATE_READY:
                        std::cout << "[状态] 待唤醒" << std::endl;
                        g_is_awake = false;
                        publishWakeupStatus(false);
                        break;
                    case AIUIConstant::STATE_WORKING:
                        std::cout << "[状态] 已唤醒，可以说话" << std::endl;
                        g_is_awake = true;
                        publishWakeupStatus(true);
                        break;
                }
                break;

            // 唤醒事件
            case AIUIConstant::EVENT_WAKEUP:
                std::cout << "[唤醒] 检测到唤醒词!" << std::endl;
                g_is_awake = true;
                publishWakeupStatus(true);
                break;

            // 休眠事件
            case AIUIConstant::EVENT_SLEEP:
                std::cout << "[休眠] AIUI进入休眠状态" << std::endl;
                g_is_awake = false;
                publishWakeupStatus(false);
                break;

            // VAD事件
            case AIUIConstant::EVENT_VAD:
                switch (event.getArg1()) {
                    case AIUIConstant::VAD_BOS:
                        // std::cout << "[VAD] 检测到语音开始" << std::endl;
                        break;
                    case AIUIConstant::VAD_EOS:
                        // std::cout << "[VAD] 检测到语音结束" << std::endl;
                        break;
                    case AIUIConstant::VAD_BOS_TIMEOUT:
                        std::cout << "[VAD] 等待超时" << std::endl;
                        break;
                    default:
                        break;
                }
                break;

            // 结果事件
            case AIUIConstant::EVENT_RESULT: {
                Json::Value bizParamJson;
                Json::Reader reader;

                if (!reader.parse(event.getInfo(), bizParamJson, false)) {
                    std::cout << "[解析错误] " << event.getInfo() << std::endl;
                    break;
                }

                Json::Value& data = (bizParamJson["data"])[0];
                Json::Value& params = data["params"];
                Json::Value& content = (data["content"])[0];
                std::string sub = params["sub"].asString();
                std::string sid = event.getData()->getString("sid", "");

                // 处理语音识别结果 (在线识别)
                if (sub == "iat") {
                    int dataLen = 0;
                    std::string cnt_id = content.get("cnt_id", Json::Value()).asString();
                    const char* buffer = event.getData()->getBinary(cnt_id.c_str(), &dataLen);

                    if (buffer && dataLen > 0) {
                        // 新会话，清空缓冲
                        if (sid != m_curSid) {
                            m_curSid = sid;
                            m_iatTextBuffer.clear();
                        }

                        std::string resultStr(buffer, dataLen);
                        Json::Value resultJson;

                        if (reader.parse(resultStr, resultJson, false)) {
                            Json::Value textJson = resultJson["text"];
                            
                            // 解析识别文本
                            std::string text;
                            Json::Value wsArray = textJson["ws"];
                            for (const auto& ws : wsArray) {
                                for (const auto& cw : ws["cw"]) {
                                    text += cw["w"].asString();
                                }
                            }

                            // 判断是否为最后一个结果
                            bool isLast = textJson["ls"].asBool();
                            
                            // 是否使用wpgs（动态修正）
                            bool isWpgs = textJson.isMember("pgs");
                            if (isWpgs) {
                                std::string pgs = textJson["pgs"].asString();
                                if (pgs == "rpl") {
                                    m_iatTextBuffer = text;
                                } else {
                                    m_iatTextBuffer += text;
                                }
                            } else {
                                m_iatTextBuffer += text;
                            }

                            if (isLast) {
                                // std::cout << "【识别结果(在线)】: " << m_iatTextBuffer << std::endl;
                               
                                if (!m_iatTextBuffer.empty()) {
                                    publishVoiceResult(m_iatTextBuffer);
                                }

                                m_iatTextBuffer.clear();
                            }
                        }
                    }
                }
                // 处理离线识别结果
            } break;

            // 错误事件
            case AIUIConstant::EVENT_ERROR:
                std::cout << "[错误] " << event.getArg1() << ": " << event.getInfo() << std::endl;
                break;

            // 连接服务器事件
            case AIUIConstant::EVENT_CONNECTED_TO_SERVER: {
                std::string uid = event.getData()->getString("uid", "");
                std::cout << "[连接] 已连接到服务器, uid=" << uid << std::endl;
            } break;

            default:
                break;
        }
    }

private:
    std::string m_iatTextBuffer;  // 识别文本缓冲
    std::string m_curSid;         // 当前会话ID
};

// ========== 简单麦克风类 ==========
class SimpleMicrophone {
public:
    SimpleMicrophone(const std::string& device = "default") 
        : m_handle(nullptr), m_frames(0), m_buffer(nullptr), m_bufferSize(0) {
        
        snd_pcm_hw_params_t* params;
        unsigned int rate = 16000;
        int retries = 10;
        int err;
        
        // 尝试打开设备
        for (int i = 0; i < retries; ++i) {
            err = snd_pcm_open(&m_handle, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
            if (err == 0) {
                std::cout << "[麦克风] 成功打开设备: " << device << std::endl;
                break;
            }
            std::cerr << "[麦克风] 打开设备失败 (尝试 " << i+1 << "): " << snd_strerror(err) << std::endl;
            usleep(100 * 1000);
        }
        
        if (!m_handle) {
            std::cerr << "[麦克风] 无法打开设备: " << device << std::endl;
            return;
        }
        
        // 配置参数
        snd_pcm_hw_params_alloca(&params);
        snd_pcm_hw_params_any(m_handle, params);
        snd_pcm_hw_params_set_access(m_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(m_handle, params, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(m_handle, params, 1);  // 单声道
        snd_pcm_hw_params_set_rate_near(m_handle, params, &rate, 0);
        snd_pcm_hw_params(m_handle, params);
        snd_pcm_hw_params_get_period_size(params, &m_frames, 0);
        
        m_bufferSize = m_frames * 2;  // 16bit = 2 bytes
        m_buffer = new char[m_bufferSize];
        
        std::cout << "[麦克风] 初始化完成, 采样率: " << rate << "Hz, 帧大小: " << m_frames << std::endl;
    }
    
    ~SimpleMicrophone() {
        if (m_handle) {
            snd_pcm_drain(m_handle);
            snd_pcm_close(m_handle);
        }
        delete[] m_buffer;
    }
    
    bool isValid() const { return m_handle != nullptr; }
    
    const char* read(int& len) {
        if (!m_handle) return nullptr;
        
        int ret = snd_pcm_readi(m_handle, m_buffer, m_frames);
        if (ret == -EPIPE) {
            // 发生了overrun，恢复
            snd_pcm_prepare(m_handle);
            return nullptr;
        }
        if (ret < 0) {
            return nullptr;
        }
        
        len = ret * 2;  // 每帧2字节（16bit）
        return m_buffer;
    }

private:
    snd_pcm_t* m_handle;
    snd_pcm_uframes_t m_frames;
    char* m_buffer;
    int m_bufferSize;
};

// ========== 信号处理 ==========
void signalHandler(int sig) {
    std::cout << "\n[信号] 收到退出信号 " << sig << std::endl;
    g_running = false;
    
    // 通知ROS2关闭
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
}

// ========== 音频采集线程 ==========
void audioThread(const std::string& deviceName) {
    SimpleMicrophone mic(deviceName);
    
    if (!mic.isValid()) {
        std::cerr << "[错误] 麦克风初始化失败!" << std::endl;
        g_running = false;
        return;
    }
    
    std::cout << "[开始] 音频采集线程启动" << std::endl;
    
    while (g_running && g_agent) {
        int len = 0;
        const char* audio = mic.read(len);
        
        if (audio && len > 0) {
            // 发送音频数据给AIUI
            AIUIBuffer frameData = aiui_create_buffer_from_data(audio, len);
            IAIUIMessage* writeMsg = IAIUIMessage::create(
                AIUIConstant::CMD_WRITE, 0, 0,
                "data_type=audio,sample_rate=16000", frameData);
            g_agent->sendMessage(writeMsg);
            writeMsg->destroy();
        }
        
        usleep(40 * 1000);  // 40ms间隔
    }
    
    std::cout << "[结束] 音频采集线程退出" << std::endl;
}

// ========== 主函数 ==========
int main(int argc, char** argv) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 初始化ROS2
    rclcpp::init(argc, argv);
    g_node = rclcpp::Node::make_shared("voice_recognition_node");
    
    // 创建发布者
    g_voice_pub = g_node->create_publisher<std_msgs::msg::String>("/voice_recognition", 10);
    g_wakeup_pub = g_node->create_publisher<std_msgs::msg::Bool>("/voice_wakeup_status", 10);
    
    // 从参数获取麦克风设备名（默认default）
    g_node->declare_parameter("mic_device", "default");
    std::string micDevice = g_node->get_parameter("mic_device").as_string();
    
    RCLCPP_INFO(g_node->get_logger(), "语音识别节点启动");
    RCLCPP_INFO(g_node->get_logger(), "麦克风设备: %s", micDevice.c_str());
    RCLCPP_INFO(g_node->get_logger(), "发布话题: /voice_recognition, /voice_wakeup_status");
    
    // 0. 初始化AIUI设置（必须在创建Agent之前）
    initAIUISetting();
    
    // 1. 创建AIUI监听器和Agent
    VoiceRecognitionListener listener;
    std::string cfgPath = "/home/tang/voice/ws_voice/src/wheeltec_mic_aiui/AIUI/cfg/aiui.cfg";
    // 注意修改
    std::string cfg = readFile(cfgPath);
    
    if (cfg.empty()) {
        RCLCPP_ERROR(g_node->get_logger(), "无法读取AIUI配置文件: %s", cfgPath.c_str());
        return -1;
    }
    
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    g_agent = IAIUIAgent::createAgent(cfg.c_str(), &listener);
#pragma GCC diagnostic pop
    
    if (!g_agent) {
        RCLCPP_ERROR(g_node->get_logger(), "创建AIUI Agent失败!");
        return -1;
    }
    RCLCPP_INFO(g_node->get_logger(), "AIUI Agent创建成功");
    
    // 2. 唤醒AIUI
    IAIUIMessage* wakeupMsg = IAIUIMessage::create(AIUIConstant::CMD_WAKEUP);
    g_agent->sendMessage(wakeupMsg);
    wakeupMsg->destroy();
    
    // 3. 启动音频采集线程
    std::thread audioCapture(audioThread, micDevice);
    
    RCLCPP_INFO(g_node->get_logger(), "开始语音识别，唤醒词: 你好小微/你好精灵/小度小度...");
    
    // 4. ROS2消息循环
    rclcpp::spin(g_node);
    
    // 5. 清理
    RCLCPP_INFO(g_node->get_logger(), "正在退出...");
    g_running = false;
    
    if (audioCapture.joinable()) {
        audioCapture.join();
    }
    
    if (g_agent) {
        g_agent->destroy();
        g_agent = nullptr;
    }
    
    g_voice_pub.reset();
    g_wakeup_pub.reset();
    g_node.reset();
    
    std::cout << "[完成] 语音识别节点已退出" << std::endl;
    return 0;
}
