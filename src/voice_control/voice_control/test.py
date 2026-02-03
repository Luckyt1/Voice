import asyncio
import aiohttp
import uuid
import json 
import struct
import gzip  # 导入压缩库
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
async def voice_recognition_demo():
    # 替换为你真实的参数
    APP_ID = "8640203250"
    ACCESS_TOKEN = "OGYbGgNaajzcwpYcfo7UBKeqAQsLCEdn"
    RESOURCE_ID = "volc.seedasr.sauc.duration"
    uri = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async"
    file_path = "/home/tang/voice/AI_ws/test.wav"
    headers = {
        "X-Api-App-Key": APP_ID,
        "X-Api-Access-Key": ACCESS_TOKEN,
        "X-Api-Resource-Id": RESOURCE_ID,
        "X-Api-Connect-Id": str(uuid.uuid4())
    }

    async with aiohttp.ClientSession() as session:
        try:
            async with session.ws_connect(uri, headers=headers) as ws:
                logid = ws._response.headers.get("X-Tt-Logid")
                print(f"--- 握手成功 --- LogID: {logid}")

                # 发送压缩后的配置包
                config_packet = construct_full_client_request(APP_ID)
                await ws.send_bytes(config_packet)
                print("已发送配置包")

                async for msg in ws:
                    if msg.type == aiohttp.WSMsgType.BINARY:
                        # 服务端返回的包可能也带头，跳过前 8 字节
                        resp_payload = msg.data[8:].decode('utf-8', errors='ignore')
                        print(f"收到响应: {resp_payload}")
                        
                        if '"event":"started"' in resp_payload:
                            print(">>> 终于成功了！可以发送音频流了。")
                    elif msg.type == aiohttp.WSMsgType.CLOSED:
                        break

        except Exception as e:
            print(f"运行出错: {e}")

if __name__ == "__main__":
    asyncio.run(voice_recognition_demo())