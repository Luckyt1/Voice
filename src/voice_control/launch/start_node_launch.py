from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    # 1. 定义麦克风采集节点
    mic_node = Node(
        package='voice_control', # 替换为你的包名
        executable='mic_pub_node',   # 替换为 setup.py 中定义的 entry_point 名字
        name='mic_publisher',
        output='screen'
    )

    # 2. 定义豆包 ASR 识别节点
    asr_node = Node(
        package='voice_control', # 替换为你的包名
        executable='doubao_asr_node', # 替换为 setup.py 中定义的 entry_point 名字
        name='doubao_asr',
        parameters=[{
            # 如果你在代码中用了 self.declare_parameter，可以在这里传参
            # 'app_key': '8640203250',
        }],
        output='screen'
    )

    return LaunchDescription([
        mic_node,
        asr_node
    ])