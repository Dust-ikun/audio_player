# 项目名称: ikun-Dust 媒体播放器

**执行摘要:** ikun-Dust 媒体播放器是一个使用 C++ 及 FFmpeg/SDL2 库开发的简单本地音视频播放器。项目目标是演示如何使用 FFmpeg 解码多媒体流并通过 SDL2 输出音频和视频。该播放器支持视频播放、音视频同步、暂停/继续（空格键）、快进/快退（左右键）等功能，适合在 Linux/Windows 等桌面系统上运行。以下文档详细介绍了项目的安装、使用、部署以及开发协作指导，方便快速上手并进行扩展维护。

## 项目简介

ikun-Dust 媒体播放器旨在实现一个基础的音视频播放功能，以示例项目的形式展示如何使用 FFmpeg 的解复用（Demux）和解码（Decode）API，以及 SDL2 的渲染和音频播放接口。主要功能包括：

- **多媒体解复用**：使用 FFmpeg 打开媒体文件，分离视频流和音频流（Demuxer）。
- **音视频解码**：分别为音频流和视频流创建解码器，解码出 PCM 音频和 YUV 视频帧（AudioDecoder、VideoDecoder）。
- **音视频播放**：通过 SDL2 的音频回调播放 PCM 数据，通过 SDL2 渲染视频帧；实现音视频同步。
- **控制操作**：支持暂停/继续（空格键）、前进/后退（左右键）、退出（ESC 键或窗口关闭）等基本控制功能。
- **线程与缓冲**：使用安全队列（SafeQueue）在多个线程间传递 AVPacket 与 AVFrame 数据，实现多线程异步处理，提高播放平滑度。

项目背景：该播放器作为学习 FFmpeg/SDL2 的示例项目，可以帮助开发者理解多媒体解码播放流程。播放器基于现代 C++ (C++11+) 实现，采用跨平台的依赖，适用于 Linux（可移植到 Windows），需要安装对应的开发库。

## 安装与依赖

本项目依赖以下环境和库：

- **操作系统**：推荐使用 Linux (Ubuntu/Debian)；也可在 Windows 上使用 MinGW 或 MSYS2 编译。
- **语言/运行时**：C++ (至少 C++11)，编译器支持如 g++、clang 或 Visual C++。
- **FFmpeg**：需要安装 FFmpeg 开发库，包括 `libavformat-dev`, `libavcodec-dev`, `libavutil-dev`, `libswresample-dev`, `libswscale-dev` 等。FFmpeg 是音视频处理的核心库，其官方文档提供了详细的 API 说明【3†L32-L40】。
- **SDL2**：Simple DirectMedia Layer (SDL2) 库用于音频和视频输出。安装 `libsdl2-dev`（或 Windows 下的 SDL2 SDK）。
- **包管理器**：在 Debian/Ubuntu 上可使用 `apt` 安装依赖；在 Windows 可使用 vcpkg 或手动安装预编译库。
- **可选组件**：无特别的第三方服务或 API；可选安装 `ffmpeg` 工具（命令行工具）以便于测试和文件格式转换。

示例（以 Ubuntu 为例）安装依赖命令：  
```bash
sudo apt update
sudo apt install -y libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libsdl2-dev build-essential cmake
```  
在 Windows 上可以使用 vcpkg：  
```bash
vcpkg install ffmpeg sdl2
```


## 使用说明

### 主要功能示例

- **播放视频**：运行程序并传入视频文件路径，如 `./media_player movie.mp4`，程序自动解码并播放音视频。
- **暂停/继续**：播放过程中按 `空格键` 切换暂停状态；暂停时视频停止，音频静音，继续时恢复播放。
- **快进/快退**：按 `→（右箭头）` 快进约 10 秒，按 `←（左箭头）` 快退约 10 秒。此操作通过 Demuxer 请求 Seek 完成，前提是视频流支持随机访问。
- **退出**：按 `ESC` 键或关闭窗口将退出程序，并在退出前安全地停止解码线程和清理资源。

### 接口说明

- **命令行接口（CLI）**：程序通过命令行接收一个参数，即待播放的多媒体文件路径。  
  ```
  Usage: ./media_player <media_file>
  ```
- **程序内部接口**：项目主要类包括 `Demuxer`, `AudioDecoder`, `VideoDecoder`, `SafeQueue` 等。Demuxer 负责读取并分发 AVPacket，解码器负责将 Packet 转为帧并推入队列，主线程通过 SDL 渲染音视频帧。
- **示例输入输出**：输入为视频文件（如 MP4、MKV 等），输出为 SDL 窗口实时显示的视频和音频输出。以下是示例的伪输出日志：
  ```
  Video stream found: 1280x720
  Audio stream found: sample_rate=44100, channels=2, format=fltp
  SDL audio open: freq=44100, format=208, channels=2
  demuxloop start
  decoderdemo start
  videodecoder start
  ```
  程序运行时会打印类似信息，可帮助调试问题。

