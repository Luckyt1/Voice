/************************************************************************************************/
/* Copyright (c) 2025 WHEELTEC Technology, Inc   												*/
/* function:Command controller, command word recognition results into the corresponding action	*/
/* 功能：命令控制器，命令词识别结果转化为对应的执行动作													*/
/************************************************************************************************/
#include "command_recognition.h"
using std::placeholders::_1;

/**************************************************************************
函数功能：寻找语音开启成功标志位sub回调函数
入口参数：voice_flag_msg  voice_control.cpp
返回  值：无
**************************************************************************/
void Command::voice_flag_Callback(std_msgs::msg::Int8::SharedPtr msg){
	voice_flag = msg->data;
	if (voice_flag){
		feedback_text.data = "语音打开成功";
		feedback_words_pub->publish(feedback_text);
		std::cout<<"语音打开成功"<<std::endl;
	}
}

/**************************************************************************
函数功能：识别结果sub回调函数
入口参数：命令词字符串
返回  值：无
**************************************************************************/
void Command::voice_words_Callback(std_msgs::msg::String::SharedPtr msg){
	/***语音指令***/
	std::string str1 = msg->data;    //取传入数据
	std::string str2 ="前进";
	std::string str3 ="后退";
	std::string str4 ="你好";
	std::string str5 ="小杜小杜";	

 	if (str1 == str2){
		feedback_text.data = "收到前进指令，开始前进";
		feedback_words_pub->publish(feedback_text);
		std::cout<<"成功识别"<<std::endl;
	}
	if (str1 == str3){
		feedback_text.data = "收到前进指令，开始后退";
		feedback_words_pub->publish(feedback_text);
		std::cout<<"成功识别"<<std::endl;
	}
	if (str1 == str4){
		feedback_text.data = "你好，祝你有美好的一天";
		feedback_words_pub->publish(feedback_text);
		std::cout<<"成功识别"<<std::endl;
	}
	if (str1 == str5){
		feedback_text.data = "你好，我是小杜";
		feedback_words_pub->publish(feedback_text);
		std::cout<<"成功识别"<<std::endl;
	}
}

Command::Command(const std::string &node_name,
	const rclcpp::NodeOptions &options)
: rclcpp::Node(node_name,options){
	RCLCPP_INFO(this->get_logger(),"%s node init!\n",node_name.c_str());

	/***唤醒标志位话题发布者创建***/
	awake_flag_pub = this->create_publisher<std_msgs::msg::Int8>("awake_flag",10); 
	/***语音反馈文本发布者创建***/
	feedback_words_pub = this->create_publisher<std_msgs::msg::String>("feedback_words",10);
	/***识别结果话题订阅者创建***/
	voice_words_sub = this->create_subscription<std_msgs::msg::String>(
		"voice_words",10,std::bind(&Command::voice_words_Callback,this,_1));

	std::cout<<"您可以语音控制啦!"<<std::endl;
	std::cout<<"请对我说: 你好精灵 "<<std::endl;
}

void Command::run(){
	rclcpp::spin(shared_from_this());
}

Command::~Command(){
	RCLCPP_INFO(this->get_logger(),"command_recognition_node over!\n");
}

int main(int argc, char *argv[])
{
	rclcpp::init(argc,argv);
	auto node = std::make_shared<Command>("command_recognition",rclcpp::NodeOptions());
	rclcpp::spin(node);  
	rclcpp::shutdown();
	return 0;
}