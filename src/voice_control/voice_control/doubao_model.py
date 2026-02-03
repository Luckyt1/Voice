import asyncio
import aiohttp
import uuid
import json 
import struct
import gzip  # 导入压缩库
import pyaudio
def tool():
    p = pyaudio.PyAudio()
    print("可用音频输入设备：")
    for i in range(p.get_device_count()):
        info = p.get_device_info_by_index(i)
        # 只显示有输入通道的设备
        if info['maxInputChannels'] > 0:
            print(f"索引 {i}: {info['name']}")
    p.terminate()
def construct_audio_packet(pcm_chunk):
    """构造音频数据包"""
    # Header: Type=2(Audio), Flags=0, Serial=0, Compress=0
    header = struct.pack('BBBB', 0x11, 0x20, 0x00, 0x00)
    # 4字节长度 (大端)
    size = struct.pack('>I', len(pcm_chunk))
    return header + size + pcm_chunk
def construct_full_client_request(app_id):
    payload_dict = {
        "user": {"uid": str(uuid.uuid4())},
        "audio": {
            "format": "pcm",
            "rate": 16000,
            "bits": 16,
            "channel": 1
        },
        "request": {
            "model_name": "", 
            "enable_itn": True,
            "show_utterances": True
        }
    }
    payload_bytes = json.dumps(payload_dict).encode('utf-8')
    # 尝试不压缩，如果还报 ungzip 错误，再换回 gzip.compress
    compressed_payload = gzip.compress(payload_bytes)

    header = struct.pack('BBBB', 
                     0x11, # Byte 0: HeaderSize(1) & MsgType(1)
                     0x11, # Byte 1: JSON(1) & Gzip(1)
                     0x11, # Byte 2: Sequence Number = 1 (对应 autoAssigned 1)
                     0x00  # Byte 3: Reserved
                    )
    
    # 4 字节 Payload 长度，必须是大端序 '>I'
    sequence_int = 1
    sequence_bytes = struct.pack('>i', sequence_int)
    payload_size = struct.pack('>I', len(compressed_payload))

    # 完整包发送
    return header + sequence_bytes + payload_size + compressed_payload
async def microphone_recognition():
    APP_ID = "8640203250"
    ACCESS_TOKEN = "OGYbGgNaajzcwpYcfo7UBKeqAQsLCEdn"
    RESOURCE_ID = "volc.seedasr.sauc.duration"
    uri = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async"
    
    headers = {
        "X-Api-App-Key": APP_ID,
        "X-Api-Access-Key": ACCESS_TOKEN,
        "X-Api-Resource-Id": RESOURCE_ID,
        "X-Api-Connect-Id": str(uuid.uuid4())
    }

    # PyAudio 配置：采样率 16000，单声道，16位深度
    FORMAT = pyaudio.paInt16
    CHANNELS = 1
    RATE = 16000
    CHUNK = 3200 # 每次读取 100ms 的数据
    
    p = pyaudio.PyAudio()
    mic_stream = p.open(format=FORMAT, channels=CHANNELS, rate=RATE, input=True, frames_per_buffer=CHUNK)

    async with aiohttp.ClientSession() as session:
        try:
            async with session.ws_connect(uri, headers=headers) as ws:
                print(f"--- 握手成功 ---")

                # 1. 发送配置包
                await ws.send_bytes(construct_full_client_request(APP_ID))

                async def send_mic_data():
                    print(">>> 正在录音... (按下 Ctrl+C 停止)")
                    try:
                        while True:
                            # 实时读取麦克风数据
                            data = mic_stream.read(CHUNK, exception_on_overflow=False)
                            await ws.send_bytes(construct_audio_packet(data))
                            # 麦克风读取本身是阻塞的，不需要额外的 sleep 模拟语速
                            await asyncio.sleep(0) 
                    except Exception as e:
                        print(f"录音发送中断: {e}")

                async def receive_results():
                    async for msg in ws:
                        if msg.type == aiohttp.WSMsgType.BINARY:
                            resp = msg.data[8:].decode('utf-8', errors='ignore')
                            data = json.loads(resp)
                            # 打印实时识别的文字
                            if "result" in data and "text" in data["result"]:
                                print(f"\r[识别中]: {data['result']['text']}", end="")
                        elif msg.type == aiohttp.WSMsgType.CLOSED:
                            break

                # 启动并发任务
                await asyncio.gather(receive_results(), send_mic_data())

        except Exception as e:
            print(f"\n运行出错: {e}")
        finally:
            mic_stream.stop_stream()
            mic_stream.close()
            p.terminate()

if __name__ == "__main__":
    try:
        asyncio.run(microphone_recognition())
    except KeyboardInterrupt:
        print("\n用户停止录音")