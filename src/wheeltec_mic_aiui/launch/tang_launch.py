import os
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    #初始化麦克风

    voice_aiui = Node(
        package="wheeltec_mic_aiui",
        executable="voice_recognition_node",
        output='screen',
    )
 #转换
    wheeltec_mic_aiui = Node(
        package="wheeltec_mic_aiui",
        executable="wheeltec_mic_aiui",
        output='screen',
    )
    voice_transform = Node(
        package="wheeltec_mic_aiui",
        executable="transform_node",
        output='screen',
    )
    chat_service= Node(
        package="wheeltec_mic_aiui",
        executable="chat_service",
        output='screen',
    )
    ld = LaunchDescription()

    ld.add_action(voice_aiui)
    ld.add_action(voice_transform)
    ld.add_action(wheeltec_mic_aiui)
    ld.add_action(chat_service)
    
    return ld