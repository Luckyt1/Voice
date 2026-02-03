import rclpy
from rclpy.node import Node
from std_msgs.msg import String, Int16MultiArray
import asyncio
import aiohttp
import json
import struct
import gzip
import uuid
import threading
import numpy as np

# 复用例程中的常量定义
class MessageType:
    CLIENT_FULL_REQUEST = 0b0001
    CLIENT_AUDIO_ONLY_REQUEST = 0b0010

class MessageTypeSpecificFlags:
    POS_SEQUENCE = 0b0001
    NEG_WITH_SEQUENCE = 0b0011

class DoubaoASRNode(Node):
    def __init__(self):
        super().__init__('doubao_asr_node')
        
        # 1. 配置参数 (根据你的 App 信息修改)
        self.app_key = "8640203250"
        self.access_key = "OGYbGgNaajzcwpYcfo7UBKeqAQsLCEdn"
        self.resource_id = "volc.seedasr.sauc.duration"
        self.url = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async"
        
        self.publisher_ = self.create_publisher(String, 'speech_to_text_result', 10)
        self.subscription = self.create_subscription(
            Int16MultiArray, 'audio_raw', self.audio_callback, 10)
        
        # 2. 内部状态管理
        self.audio_queue = asyncio.Queue() # 异步音频队列
        self.seq = 1
        
        # 3. 启动异步循环线程
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.run_async_loop, daemon=True)
        self.thread.start()
        
        # 在异步循环中启动 ASR 任务
        asyncio.run_coroutine_threadsafe(self.asr_main_task(), self.loop)

    def run_async_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def audio_callback(self, msg):
        """将 ROS 话题数据放入异步队列"""
        # 转换为字节流
        audio_bytes = np.array(msg.data, dtype=np.int16).tobytes()
        # 线程安全地放入队列
        self.loop.call_soon_threadsafe(self.audio_queue.put_nowait, audio_bytes)

    # --- 核心协议封装函数 (源自例程) ---
    def construct_request(self, payload, msg_type, is_last=False):
        # 1. Header (4字节)
        # V1(0001) | HeaderSize(1) -> 0x11
        # MsgType | Flags
        flags = MessageTypeSpecificFlags.NEG_WITH_SEQUENCE if is_last else MessageTypeSpecificFlags.POS_SEQUENCE
        header = bytes([(0x01 << 4) | 1, (msg_type << 4) | flags, (0x01 << 4) | 0x01, 0x00])
        
        # 2. Sequence (4字节) & Size (4字节)
        cur_seq = -self.seq if is_last else self.seq
        payload = gzip.compress(payload) # 必须压缩
        
        full_msg = bytearray()
        full_msg.extend(header)
        full_msg.extend(struct.pack('>i', cur_seq))
        full_msg.extend(struct.pack('>I', len(payload)))
        full_msg.extend(payload)
        return full_msg

    async def asr_main_task(self):
        """主异步任务：管理连接、发送和接收"""
        headers = {
            "X-Api-Resource-Id": self.resource_id,
            "X-Api-Access-Key": self.access_key,
            "X-Api-App-Key": self.app_key,
            "X-Api-Connect-Id": str(uuid.uuid4())
        }
        
        async with aiohttp.ClientSession() as session:
            async with session.ws_connect(self.url, headers=headers) as ws:
                self.get_logger().info("WebSocket 已连接，正在握手...")
                
                # 1. 发送 Full Client Request
                config_payload = {
                    "user": {"uid": "8640203250"},
                    "audio": {"format": "pcm", "rate": 16000, "bits": 16, "channel": 1},
                    "request": {"model_name": "bigmodel", "show_utterances": False}
                }
        
                await ws.send_bytes(self.construct_request(json.dumps(config_payload).encode(), MessageType.CLIENT_FULL_REQUEST))
                # 2. 启动并发任务：持续发送音频和接收结果
                send_task = asyncio.create_task(self.send_audio_loop(ws))
                
                async for msg in ws:
                    if msg.type == aiohttp.WSMsgType.BINARY:
                        # 解析返回的私有帧 (跳过前8字节Header+Size)
                        # 注意：实际解析需根据例程中的 ResponseParser 处理更复杂的消息位
                        try:
                            # 简化解析：假设返回的是完整 Response
                            # self.get_logger().info(msg.type)
                            # ResponseParser.parse_response(msg.data)
                            # 这里暂展示逻辑思路
                            pass 
                        except:
                            pass
                await send_task

    async def send_audio_loop(self, ws):
        """从队列读取音频并发送"""
        while True:
            audio_data = await self.audio_queue.get()
            request = self.construct_request(audio_data, MessageType.CLIENT_AUDIO_ONLY_REQUEST)
            await ws.send_bytes(request)
            self.seq += 1

def main(args=None):
    rclpy.init(args=args)
    node = DoubaoASRNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()