#include <iostream>
#include <SDL2/SDL.h>
#include <atomic>
#include <thread>
#include <chrono>

#include "queue.h"
#include "demuxer.h"
#include "audio_decoder.h"
#include "video_decoder.h"

SafeQueue<AVFrame*> video_frame_queue(50); // 增加容量限制
SafeQueue<Audiochunk> audio_frame_queue(100); // 音频帧较小，可以适当给多点

// 新增：全局音频缓冲池
SafeQueue<std::vector<uint8_t>> free_audio_buffers(200);

std::atomic<bool> paused{false};
std::atomic<bool> quit{false};
std::atomic<double> audio_clock{0.0};

int g_audio_sample_rate = 0;
int g_audio_bytes_per_sample = 0;

void audioCallback(void* userdata, Uint8* stream, int len){
    static Audiochunk current_chunk;
    static size_t pos = 0;
    static double start_pts = 0.0;
    static int played_samples = 0;

    while (len > 0){
        if (current_chunk.data.empty() || pos >= current_chunk.data.size()){
            if (!current_chunk.data.empty()){
                // 这里的 data 其实是一个 vector
                // 我们调用 clear() 清除内容，但 vector 占用的内存空间 (capacity) 不会释放
                current_chunk.data.clear(); 
                
                // 将这个保留了内存结构的“空盘子”还给 free_buffers
                // 这样下次 AudioDecoder 就能直接拿到它，不用重新申请内存了
                free_audio_buffers.push(std::move(current_chunk.data));
            }
            auto opt = audio_frame_queue.tryPopFor(std::chrono::milliseconds(10));
            if(opt.has_value()){
                current_chunk = std::move(opt.value());
                pos = 0;
                start_pts = current_chunk.pts;
                played_samples = 0;
                audio_clock = start_pts;
            }else{
                SDL_memset(stream, 0, len);
                return;
            }
        }
        size_t to_copy = std::min((size_t)len, current_chunk.data.size() - pos);
        memcpy(stream, current_chunk.data.data() + pos, to_copy);
        stream += to_copy;
        len -= to_copy;
        pos += to_copy;

        int samples_copied = to_copy / g_audio_bytes_per_sample;
        played_samples += samples_copied;
        audio_clock = start_pts + (double)played_samples / g_audio_sample_rate;
    }
}

void videoRenderLoop (SDL_Renderer* renderer, SDL_Texture* texture, AVRational video_time_base){
    const double SYNC_THRESHOLD = 0.01;
    const double DROP_THRESHOLD = 0.05;

    while (!quit){
        if (paused){
        SDL_Delay(10);
        continue;
        }

        auto opt_frame = video_frame_queue.tryPopFor(std::chrono::milliseconds(1));
        if (!opt_frame.has_value()){
            SDL_Delay(5);
            continue;
        }
        AVFrame* frame = opt_frame.value();

        double pts = frame->best_effort_timestamp * av_q2d(video_time_base);
        double diff = pts - audio_clock.load();

        if (std::abs(diff) > 1.0) {
            /* 情况 A: 视频严重落后 (diff < -1.0)
               通常发生在窗口拖动、解码卡顿后。
               解决：不要一帧帧丢，直接清空视频队列，让解码器赶上最新的音频点。
            */
            if (diff < 0) {
                printf("Video too late (%.2fs), flushing queue...\n", diff);
                // 清空当前的视频帧队列，让渲染线程直接拿到解码器最新的帧
                while(auto old_frame = video_frame_queue.tryPopFor(std::chrono::milliseconds(0))) {
                    av_frame_free(&old_frame.value());
                }
                av_frame_free(&frame);
                continue; // 放弃当前旧帧，重新 Pop
            }
            
        }

        if (diff > SYNC_THRESHOLD){
            //视频超前，等待
            int wait_ms = static_cast<int>(diff * 900);
            if (wait_ms > 50) wait_ms = 50;
            SDL_Delay(wait_ms);
        } else if (diff < -DROP_THRESHOLD){
            av_frame_free(&frame);
            continue;
        }

        int ret =SDL_UpdateYUVTexture(texture, nullptr,
                             frame->data[0], frame->linesize[0],
                             frame->data[1], frame->linesize[1],                                                   
                             frame->data[2], frame->linesize[2]);
        if (ret != 0){
            std::cerr << "SDL_UpdateYUVTexture error: " << SDL_GetError() << std::endl;
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);                                                        
        
        av_frame_free(&frame);
    }
}

int main(int argc, char* argv[]){
    if (argc < 2){
        std::cerr << "Usage: " << argv[0] << " <media_file" << std::endl;
        return -1;
    }
    std::string filename = argv[1];

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11"); 

    if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0){
        std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 初始化队列时限制包队列的容量，防止 Demuxer 暴走
    SafeQueue<AVPacket*> video_pkt_queue(200); 
    SafeQueue<AVPacket*> audio_pkt_queue(300); 

    Demuxer demuxer(video_pkt_queue, audio_pkt_queue);
    if(!demuxer.open(filename)){
        return -1;
    }

    AudioDecoder decoder(audio_pkt_queue, audio_frame_queue, free_audio_buffers);
    decoder.setCodecContext(demuxer.getAudioCodecContex());
    decoder.setTimeBase(demuxer.getAudioTimeBase());

    SDL_AudioSpec wanted_spec, obtained_spec;
    SDL_zero(wanted_spec);
    wanted_spec.freq = decoder.getOutSampleRate();
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = decoder.getOutChannels();
    wanted_spec.samples = 1024;
    wanted_spec.callback = audioCallback;
    wanted_spec.userdata = nullptr;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &obtained_spec, 0);
    if (dev == 0){
        std::cerr << "Open audio device error: " << SDL_GetError() << std::endl;
        return -1;
    }
    std::cout << "SDL audio open: freq=" << obtained_spec.freq << ", format=" << obtained_spec.format 
        << ", channels=" << (int)obtained_spec.channels << std::endl;
    
    g_audio_sample_rate = obtained_spec.freq;
    int bytes_per_channel = SDL_AUDIO_BITSIZE(obtained_spec.format) / 8;
    g_audio_bytes_per_sample = obtained_spec.channels * bytes_per_channel;

    AVCodecContext* video_codec_ctx = demuxer.getVideoCodecContex();
    if (!video_codec_ctx){
        std::cerr << "No video stream found, exiting." << std::endl;
        return -1;
    }

    int video_width = video_codec_ctx->width;
    int video_height = video_codec_ctx->height;
    AVRational video_time_base = demuxer.getVideoTimeBase();

    SDL_Window* window = SDL_CreateWindow("ikun-Dust media player",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          video_width,
                                          video_height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window){
        std::cerr << "SDL_CreatWindow error: " << SDL_GetError() << std::endl;
        return -1;
    }

    //int x, y;
    //SDL_GetWindowPosition(window, &x, &y);
    //std::cout << "Window position: (" << x << ", " << y << ")" << std::endl;
    //SDL_DisplayMode dm;
    //if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
    //int screen_w = dm.w;
    //int screen_h = dm.h;
    //std::cout << "Screen size: " << screen_w << " x " << screen_h << std::endl;
    //if (video_width > screen_w || video_height > screen_h) {
    //    // 按比例缩小窗口尺寸，但保留原始宽高比
    //    float ratio = std::min((float)screen_w / video_width, (float)screen_h / video_height);
    //    video_width = (int)(video_width * ratio);
    //    video_height = (int)(video_height * ratio);
    // }
    //}

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer){
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!renderer){
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if(!renderer){
        std::cerr << "renderer open failed." << std::endl;
        return -1;
    }
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, video_width, video_height);


    VideoDecoder video_decoder(video_pkt_queue, video_frame_queue);
    video_decoder.setCodecContext(video_codec_ctx);
    video_decoder.setTimeBase(video_time_base);

    demuxer.start();
    decoder.start();
    video_decoder.start();

    std::thread video_display_thread(videoRenderLoop, renderer, texture, video_time_base);

    SDL_PauseAudioDevice(dev, 0);
    SDL_Event event;

    while (!quit) {
        // 使用 WaitEvent 而不是 PollEvent，在没动作时节省 CPU
        if (SDL_WaitEvent(&event)) { 
            if (event.type == SDL_QUIT) {
                quit = true;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym){
                    case SDLK_SPACE:
                        paused = !paused;
                        if (!demuxer.is_seekable()){
                            if (!paused){
                                video_pkt_queue.clearAndfree([](AVPacket* pkt){av_packet_free(&pkt);});
                                audio_pkt_queue.clearAndfree([](AVPacket* pkt){av_packet_free(&pkt);});
                                AVPacket* flush_pkt = flush_pkt_sentinel();
                                video_pkt_queue.push(flush_pkt);
                                audio_pkt_queue.push(flush_pkt);
                                }
                            }
                        SDL_PauseAudioDevice(dev,paused ? 1 : 0);
                        break;
                    case SDLK_ESCAPE:
                        quit = true;
                        break;
                    case SDLK_LEFT:
                    case SDLK_RIGHT:{
                        if (!demuxer.is_seekable()){
                            std::cout << "Seek not supported for Live Streams." << std::endl;
                            break;
                        }
                        double duration_sec = demuxer.getDuration() / 1000000.0;
                        double current_sec = audio_clock.load();
                        double delta = (event.key.keysym.sym == SDLK_LEFT) ? -10.0 : 10.0;
                        double new_sec = std::max(0.0, std::min(duration_sec,current_sec + delta));
                        double percent = new_sec / duration_sec;
                        demuxer.requestSeek(percent);
                        break;
                        }
                }
            // 窗口调整大小时，SDL 会自动处理 Texture 的缩放渲染（因为渲染线程在跑）
            }
        }  
    }  

    SDL_PauseAudioDevice(dev, 1);
    SDL_CloseAudioDevice(dev);

    demuxer.stop();
    decoder.stop();
    video_decoder.stop();
    demuxer.join();
    decoder.join();
    video_decoder.join();
    if (video_display_thread.joinable()) video_display_thread.join();

    while (auto pkt = audio_pkt_queue.pop()){ av_packet_free(&pkt);}
    while (auto pkt = video_pkt_queue.pop()){ av_packet_free(&pkt);}
    while (auto frame = video_frame_queue.pop()) av_frame_free(&frame);
    audio_frame_queue.clear();

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}