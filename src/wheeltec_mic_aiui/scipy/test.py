import pyaudio
import wave
import numpy as np
def main():
   p = pyaudio.PyAudio()
#    for i in range(p.get_device_count()):
#       info = p.get_device_info_by_index(i)
#       if info["maxInputChannels"] > 0:
#          print("Device %d: %s" % (i, info["name"]))

   CHUNK = 1024              # 每次读取的音频块大小
   FORMAT = pyaudio.paInt16  # 采样深度 (16位)
   CHANNELS = 1              # 声道数 (1=单声道, 2=立体声)
   RATE = 16000              # 采样率 (Hz, CD质量)
   stream = p.open(format=FORMAT,
                   channels=CHANNELS,
                   rate=RATE,
                   input=True,
                   frames_per_buffer=CHUNK)

   frames = []

   for i in range(0, int(RATE / CHUNK * 5)):
        data = stream.read(CHUNK)
        frames.append(data)

        audio_data = np.frombuffer(data, dtype=np.int16)
        amplitude = np.abs(audio_data).mean()
        print(f"当前音量: {amplitude:.2f}") # 如果你说话时数值没变化，说明没录进去
   
   stream.stop_stream()
   stream.close()
   p.terminate()
   try:
      wf = wave.open("test.wav", "wb")
      wf.setnchannels(CHANNELS)
      wf.setsampwidth(p.get_sample_size(FORMAT))
      wf.setframerate(RATE)
      wf.writeframes(b"".join(frames))
      wf.close()
      print("音频文件保存成功")
   except Exception as e:
      print("保存音频文件时出错: ", e)

if __name__ == "__main__":
    main()

