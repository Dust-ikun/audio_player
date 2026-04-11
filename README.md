# 第一阶段：音频播放器 - 基于 FFmpeg 和 SDL2 的命令行音频播放器

本项目是“从零实现支持网络流的高性能媒体播放器”系列的第一阶段成果。该阶段实现了完整的音频播放功能：解封装、解码、重采样、多线程数据流水线、音频播放和优雅退出。

## ✨ 功能特性

- 支持常见的音频格式（MP3、AAC、FLAC、WAV 等，基于 FFmpeg 解封装）
- 从本地文件读取音频流
- 独立解封装线程与音频解码线程，通过线程安全队列传递数据包
- 自动重采样为 SDL 兼容的格式（S16、立体声、原始采样率）
- SDL2 音频回调播放，支持动态填充音频数据
- 优雅退出：响应 `std::cin.get()` 等待用户按回车，线程安全退出并释放资源

## 🛠️ 技术栈

- **C++17** 标准
- **FFmpeg** (libavformat, libavcodec, libavutil, libswresample) – 解封装、解码、重采样
- **SDL2** – 音频设备管理与播放
- **CMake** – 跨平台构建

## 📁 项目结构
audio_player/
├── CMakeLists.txt
├── README.md
├── src/
│ ├── main.cpp # 主程序，初始化 SDL，启动线程
│ ├── queue.h # 线程安全队列模板
│ ├── demuxer.h / .cpp # 解封装模块
│ └── audio_decoder.h / .cpp # 音频解码 + 重采样模块
└── media/ # 测试媒体文件（可选）

text

## 🔧 构建与运行

### 依赖安装

#### Windows (MSYS2)
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make
pacman -S mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-SDL2
Linux (Ubuntu/Debian)
bash
sudo apt install cmake build-essential libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libsdl2-dev
macOS (Homebrew)
bash
brew install cmake ffmpeg sdl2
构建步骤
bash
git clone <your-repo-url>
cd audio_player
mkdir build && cd build
cmake .. -G "MinGW Makefiles"   # Windows 使用 MinGW；Linux/macOS 可省略 -G
make                            # 或 mingw32-make (Windows)
```
运行示例
bash
./audio_player ../media/test.mp3
播放期间按 Enter 键退出程序。

🧠 核心实现解析
1. 多线程流水线
主线程：初始化 SDL，创建解封装器和解码器，启动工作线程，等待用户输入。

解封装线程：调用 av_read_frame 读取 AVPacket，将音频包推入 SafeQueue<AVPacket*>。

解码线程：从包队列取包，解码为 AVFrame，通过 swr_convert 重采样为 S16 交错格式，将 PCM 数据块（std::vector<uint8_t>）推入音频帧队列。

SDL 音频回调：在独立音频线程中从音频帧队列取数据，拷贝到硬件缓冲区；队列空时填充静音。

2. 线程安全队列
使用 std::mutex + std::condition_variable 实现阻塞队列，支持 pop()（无限阻塞）和 tryPopFor()（超时等待）。队列负责传递 AVPacket* 和 PCM 数据块，明确所有权转移，避免内存泄漏。

3. 音频重采样
由于解码器输出格式不固定（可能是平面/交错、浮点/整型、多声道），而 SDL 要求 S16 交错立体声，因此使用 libswresample 统一转换。配置输入/输出参数后，调用 swr_convert 完成转换。

4. 内存管理
AVPacket* 在解封装线程 alloc，推入队列后所有权转移；解码线程使用完毕后 av_packet_free。

重采样输出缓冲区使用 av_samples_alloc 分配，转换为 std::vector<uint8_t> 后立即 av_free。

AVFrame 复用，每次使用后 av_frame_unref。

📄 主要 API 参考
模块	函数	作用
FFmpeg	avformat_open_input	打开媒体文件
FFmpeg	av_find_best_stream	查找音频流
FFmpeg	avcodec_parameters_to_context	将流参数复制到解码器上下文
FFmpeg	avcodec_send_packet / receive_frame	解码一包 / 取一帧
FFmpeg	swr_alloc / swr_init / swr_convert	重采样配置与执行
SDL2	SDL_Init / SDL_OpenAudioDevice	初始化音频设备
SDL2	SDL_PauseAudioDevice	开始/暂停播放
C++	std::thread / std::mutex / std::condition_variable	多线程同步
🧪 测试情况
本地测试文件：MP3（44100 Hz, 立体声）、AAC（48000 Hz, 立体声）、WAV（PCM S16）均能正常播放，无卡顿、无爆音。

内存泄漏检测：Valgrind / AddressSanitizer 无泄漏（注意：FFmpeg 全局初始化有少量“仍可达”内存，可忽略）。

线程退出：按回车后所有线程正常退出，无死锁。

📌 待优化（未来阶段）
视频解码与显示

音视频同步（以音频时钟为基准）

网络流支持（HTTP/RTMP）

进度条跳转（seek）

音量控制

🙏 致谢
FFmpeg 与 SDL2 社区

开源项目 FFplay 参考实现

📜 许可证
本项目采用 MIT 许可证，详见 LICENSE 文件。