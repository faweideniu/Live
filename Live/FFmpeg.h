#pragma once
extern "C"
{
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/mathematics.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/imgutils.h"



#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

    typedef struct InputConext {
        AVFormatContext *ctx;
        AVInputFormat *ifmt;
        AVStream* stream;
        //AVCodecContext* codeCtx;
        //AVCodec* codec;
    } InputConext;

    typedef struct OutputContext {
        AVFormatContext* ctx;
        AVCodecContext* audio_ctx;
        AVCodecContext* video_ctx;
        AVStream* audio_stream;
        AVStream* video_stream;
    } OutputContext;


    typedef struct TimeControl {
        int64_t dela;
        int64_t duration;
    } TimeControl;
};

class CFFmpeg
{
public:
    CFFmpeg();
    ~CFFmpeg();

    int initDevice();
    int process();
protected:
    InputConext m_video;
    InputConext m_audio;
    OutputContext m_output;

    struct SwsContext *m_convertCtx;

    TimeControl m_videoTime;
    TimeControl m_audioTime;
    //struct SwrContext *aud_convert_ctx;

private:
    int openVideo();
    int openAudio();
    int openOutput();
    int initChange();
    int initTime();
    int processVideo(int &encode_video);
    int processAudio(int &encode_audio);

};

