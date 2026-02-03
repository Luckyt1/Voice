/**
 * tranform_node.cpp
 * 语音识别结果转换节点：订阅语音识别结果，发布唤醒状态和识别文字
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>

// 全局变量
rclcpp::Node::SharedPtr transform_node = nullptr;
rclcpp::Subscription<std_msgs::msg::String>::SharedPtr voice_sub = nullptr;
rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr awake_pub = nullptr;
rclcpp::Publisher<std_msgs::msg::String>::SharedPtr words_pub = nullptr;
rclcpp::Publisher<std_msgs::msg::String>::SharedPtr feedback_words_pub = nullptr;

std::atomic<bool> awake_flag(false);
std::string get_words = "";
std::atomic<bool> running(true);
static void publish_awake(bool flag) {
    if (awake_pub && transform_node) {
        std_msgs::msg::Bool msg;
        msg.data = flag;
        awake_pub->publish(msg);
        RCLCPP_INFO(transform_node->get_logger(), "唤醒状态: %s", flag ? "开始识别" : "等待说话");
    }
}
static void publish_feedback_words(const std::string& text) {
    if (feedback_words_pub && transform_node && !text.empty()) {
        std_msgs::msg::String msg;
        msg.data = text;
        feedback_words_pub->publish(msg);
        RCLCPP_INFO(transform_node->get_logger(), "发布识别结果: %s", text.c_str());
    }
}

// 发布识别结果
static void publish_words(const std::string& text) {
    if (words_pub && transform_node && !text.empty()) {
        std_msgs::msg::String msg;
        msg.data = text;
        words_pub->publish(msg);
        RCLCPP_INFO(transform_node->get_logger(), "发布识别结果: %s", text.c_str());
    }
}

// 语音识别结果回调
void voice_callback(const std_msgs::msg::String::SharedPtr msg)
{
    // 检测唤醒词
    if (msg->data.find("你好精灵") != std::string::npos )
    {
        awake_flag = true;
        publish_feedback_words("我在，有什么可以帮助你的");
        std::cout << "[唤醒] 检测到唤醒词: " << msg->data << std::endl;

    } else if (awake_flag) {
        // 已唤醒状态下，保存识别结果
        get_words = msg->data;
        awake_flag=false;
        publish_awake(false);
        std::cout << "[识别] 收到文字: " << msg->data << std::endl;
    }
}

// 发布唤醒状态

// 处理线程
void processThread() {
    bool last_awake_flag = false;
    
    while (running && rclcpp::ok()) {
        // 唤醒状态变化时发布
        if (awake_flag != last_awake_flag) {
            publish_awake(awake_flag);
            last_awake_flag = awake_flag;
        }
        
        // 有新的识别结果时发布
        if (!get_words.empty()) {
            publish_words(get_words);
            get_words = "";  // 清空已发布的文字
        }
        
        usleep(100 * 1000);  // 100ms间隔
    }
    
    std::cout << "[结束] 处理线程退出" << std::endl;
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    transform_node = rclcpp::Node::make_shared("transform_node");

    // 创建订阅者和发布者
    voice_sub = transform_node->create_subscription<std_msgs::msg::String>(
        "/voice_recognition", 10, voice_callback);
    awake_pub = transform_node->create_publisher<std_msgs::msg::Bool>("/t_voice_awake", 10);
    words_pub = transform_node->create_publisher<std_msgs::msg::String>("/voice_words", 10);
    feedback_words_pub = transform_node->create_publisher<std_msgs::msg::String>("/feedback_words", 10);

    RCLCPP_INFO(transform_node->get_logger(), "Transform节点启动");
    RCLCPP_INFO(transform_node->get_logger(), "订阅: /voice_recognition");
    RCLCPP_INFO(transform_node->get_logger(), "发布: /voice_awake, /voice_words");

    // 启动处理线程（detach而不是join，这样不会阻塞spin）
    std::thread process_thread(processThread);
    process_thread.detach();

    // ROS2消息循环
    rclcpp::spin(transform_node);
    
    // 清理
    running = false;
    std::cout << "已退出" << std::endl;
    
    rclcpp::shutdown();
    return 0;
}