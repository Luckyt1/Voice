import rclpy
from rclpy.node import Node
from std_msgs.msg import Int16MultiArray
import pyaudio
import numpy as np

class MicPublisher(Node):
    def __init__(self):
        super().__init__('mic_publisher_node')
        self.publisher_ = self.create_publisher(Int16MultiArray, 'audio_raw', 10)
        
        # 音频配置：必须匹配豆包 ASR 模型要求
        self.CHUNK = 1600  # 每 100ms 采集一次 (16000 * 0.1)
        self.FORMAT = pyaudio.paInt16
        self.CHANNELS = 1
        self.RATE = 16000
        
        self.p = pyaudio.PyAudio()
        self.stream = self.p.open(
            format=self.FORMAT,
            channels=self.CHANNELS,
            rate=self.RATE,
            input=True,
            frames_per_buffer=self.CHUNK
        )
        
        # 创建定时器，以 10Hz 频率发布音频包 (每 100ms 一包)
        self.timer = self.create_timer(0.1, self.timer_callback)
        self.get_logger().info("麦克风采集节点已启动，正在发布音频到 /audio_raw...")

    def timer_callback(self):
        try:
            # 读取原始音频数据
            data = self.stream.read(self.CHUNK, exception_on_overflow=False)
            # 转换为 int16 数组发送
            audio_array = np.frombuffer(data, dtype=np.int16)
            
            msg = Int16MultiArray()
            msg.data = audio_array.tolist()
            self.publisher_.publish(msg)
        except Exception as e:
            self.get_logger().error(f"采集出错: {e}")

    def destroy_node(self):
        self.stream.stop_stream()
        self.stream.close()
        self.p.terminate()
        super().destroy_node()

def main(args=None):
    rclpy.init(args=args)
    node = MicPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()