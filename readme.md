# 常见问题
- 1.’/home/tang/github/Voice/install/wheeltec_mic_aiui/lib/wheeltec_mic_aiui/voice_recognition_node: error while loading shared libraries: libaiui.so: cannot open shared object file: No such file or directory
[wheeltec_mic_aiui-3] /home/tang/github/Voice/install/wheeltec_mic_aiui/lib/wheeltec_mic_aiui/wheeltec_mic_aiui: error while loading shared libraries: libaiui.so: cannot open shared object file: No such file or directory‘

解决：
find /home/tang/github/Voice/ -name "libaiui.so"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/tang/github/Voice/src/wheeltec_mic_aiui/libs/x64
即可解决