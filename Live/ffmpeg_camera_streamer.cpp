//#define USEFILTER 1
//#define USEFILTER 1
//#include <stdio.h>
#include <conio.h>
#include "stdafx.h"
#include "ffmpeg_camera_streamer.h"
#include "DS_AudioVideoDevices.h"
#include "HelperChar.h"
#define snprintf _snprintf
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



#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

};

#define FRAME_RATE 3
#define YUV_FORMAT  AVPixelFormat::AV_PIX_FMT_YUVJ420P
#define CODEC_CAP_DELAY AV_CODEC_CAP_DELAY
#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER
#define FINISH_TIME 1000*60


extern char* w2c(WCHAR* WStr);
char* threshold="0.089";
char* ratio="9";


#pragma region[filter]
typedef struct FilteringContext {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
} FilteringContext;

static FilteringContext *filter_ctx;

static int init_filter(FilteringContext* fctx, AVCodecContext *dec_ctx,
                       AVCodecContext *enc_ctx, const char *filter_spec);
static int init_filters(AVFormatContext *ifmt_ctx, AVCodecContext* dec_ctx, AVCodecContext* enc_ctx)
{
    const char *filter_spec;
    unsigned int i;
    int ret;
    filter_ctx = (FilteringContext*) av_malloc_array(ifmt_ctx->nb_streams, sizeof(*filter_ctx));
    if (!filter_ctx)
        return AVERROR(ENOMEM);

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        filter_ctx[i].buffersrc_ctx = NULL;
        filter_ctx[i].buffersink_ctx = NULL;
        filter_ctx[i].filter_graph = NULL;
        if (!(ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
              || ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;


        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            filter_spec = "null"; /* passthrough (dummy) filter for video */
        else
            filter_spec = "acompressor=threshold=0.089:ratio=9:attack=200:release=1000"; /* passthrough (dummy) filter for audio */
        ret = init_filter(&filter_ctx[i], dec_ctx,
                          enc_ctx, filter_spec);
        if (ret)
            return ret;
    }
    return 0;
}
static int init_filter(FilteringContext* fctx, AVCodecContext *dec_ctx,
                       AVCodecContext *enc_ctx, const char *filter_spec)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                 dec_ctx->time_base.num, dec_ctx->time_base.den,
                 dec_ctx->sample_aspect_ratio.num,
                 dec_ctx->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                           args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                           NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                             (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (!dec_ctx->channel_layout)
            dec_ctx->channel_layout =
                av_get_default_channel_layout(dec_ctx->channels);
        snprintf(args, sizeof(args),
                 "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                 dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
                 av_get_sample_fmt_name(dec_ctx->sample_fmt),
                 dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                           args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                           NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                             (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts",
                             (uint8_t*)&enc_ctx->channel_layout,
                             sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                             (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    /* Fill FilteringContext */
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
static int encode_write_frame(AVFrame *filt_frame, unsigned int stream_index, int *got_frame,
                              //AVCodecContext *dec_ctx,
                              AVCodecContext *enc_ctx,
                              AVFormatContext *ifmt_ctx,
                              AVFormatContext *ofmt_ctx,
                              unsigned int stream_index_a,
                              int nb_samples, int &aud_next_pts)
{

    //@lw 修改

    int ret;
    int got_frame_local;
    AVPacket enc_pkt;
    int(*enc_func)(AVCodecContext *, AVPacket *, const AVFrame *, int *) =
        (ifmt_ctx->streams[stream_index]->codecpar->codec_type ==
         AVMEDIA_TYPE_VIDEO) ? avcodec_encode_video2 : avcodec_encode_audio2;

    if (!got_frame)
        got_frame = &got_frame_local;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);
    ret = enc_func(enc_ctx, &enc_pkt,
                   filt_frame, got_frame);
    av_frame_free(&filt_frame);
    if (ret < 0)
        return ret;
    if (!(*got_frame))
        return 0;


    //enc_pkt.stream_index = 1;

    AVRational time_base = ofmt_ctx->streams[stream_index_a]->time_base;
    AVRational r_framerate1 = { ifmt_ctx->streams[stream_index]->codec->sample_rate, 1 };// { 44100, 1};
    int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));  //内部时间戳

    AVRational time_base_q = { 1, AV_TIME_BASE };
    enc_pkt.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
    enc_pkt.dts = enc_pkt.pts;
    enc_pkt.duration = filt_frame->nb_samples;

    //printf("audio pts : %d\n", output_packet.pts);
    aud_next_pts = nb_samples*calc_duration;

    /* prepare packet for muxing */
    enc_pkt.stream_index = stream_index_a;
    av_packet_rescale_ts(&enc_pkt,
                         enc_ctx->time_base,
                         ofmt_ctx->streams[stream_index_a]->time_base);

    av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
    /* mux encoded frame */
    ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
    return ret;
}
static int filter_encode_write_frame(AVFrame *frame, unsigned int stream_index,
                                     AVCodecContext *enc_ctx,
                                     AVFormatContext *ifmt_ctx,
                                     AVFormatContext *ofmt_ctx,
                                     unsigned int stream_index_a,
                                     int nb_samples, int & aud_next_pts)
{
    int ret;
    AVFrame *filt_frame;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter_ctx[stream_index].buffersrc_ctx,
                                       frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        filt_frame = av_frame_alloc();
        if (!filt_frame) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(filter_ctx[stream_index].buffersink_ctx,
                                      filt_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
            * if flushed and no more frames for output - returns AVERROR_EOF
            * rewrite retcode to 0 to show it as normal procedure completion
            */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            av_frame_free(&filt_frame);
            break;
        }

        filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(filt_frame, stream_index, NULL,
                                 enc_ctx, ifmt_ctx, ofmt_ctx, stream_index_a, nb_samples, aud_next_pts);
        if (ret < 0)
            break;
    }

    return ret;
}

#pragma endregion[filter]



void setThresholdRatio(CString* pThreshold,CString* pRatio)
{

    threshold = w2c(pThreshold->GetBuffer());
    ratio = w2c(pRatio->GetBuffer());



    //pThreshold->ReleaseBuffer();
    //pRatio->ReleaseBuffer();
}



char *dup_wchar_to_utf8(const wchar_t *w)
{
    char *s = NULL;
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    s = (char *)av_malloc(l);
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
    return s;
}

int flush_encoder(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, unsigned int stream_index, int framecnt);
int flush_encoder_a(AVFormatContext *ifmt_ctx_a, AVFormatContext *ofmt_ctx, unsigned int stream_index, int nb_samples);

enum TYPE_THREAD {
    INIT,
    PROCESS,
    FINISH
};

TYPE_THREAD exit_thread = TYPE_THREAD::INIT;

DWORD WINAPI MyThreadFunction(LPVOID lpParam)
{
    int i = 0;
    while (i++<1) {
        Sleep(FINISH_TIME);
    }
    exit_thread =TYPE_THREAD::FINISH;
    return 0;
}

//DWORD WINAPI ProcessVideo(LPVOID lpParam)
DWORD WINAPI mainC(LPVOID lpParam)
{
    //for (int i = 0; i < 10000; i++) {
    //    printf("xxxxxxxxxxxxxxxxxxxxxxxxxx");
    //}

    //return -1;

    if (exit_thread!=TYPE_THREAD::INIT)
        return -1;

    exit_thread = TYPE_THREAD::PROCESS;


    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ifmt_ctx_a = NULL;
    AVFormatContext *ofmt_ctx;
    AVInputFormat* ifmt;
    AVInputFormat* ifmt_a;
    AVStream* video_st;
    AVStream* audio_st;
    AVCodecContext* pCodecCtx;
    AVCodecContext* pCodecCtx_a;
    AVCodec* pCodec;
    AVCodec* pCodec_a;
    AVPacket *dec_pkt, enc_pkt;
    AVPacket *dec_pkt_a, enc_pkt_a;
    AVFrame *pframe, *pFrameYUV;
    struct SwsContext *img_convert_ctx;
    struct SwrContext *aud_convert_ctx;

    char capture_name[80] = { 0 };
    char device_name[80] = { 0 };
    //char device_name_a[80] = { 0 };
    int framecnt = 0;
    int nb_samples = 0;
    int videoindex;
    int audioindex;
    int i;
    int ret;
    // HANDLE  hThread;

    char out_path[32];
    int dec_got_frame, enc_got_frame;
    int dec_got_frame_a, enc_got_frame_a;

    int aud_next_pts = 0;
    int vid_next_pts = 0;
    int encode_video = 1, encode_audio = 1;

    AVRational time_base_q = { 1, AV_TIME_BASE };

    av_register_all();
    //Register Device
    avdevice_register_all();
    avformat_network_init();

    //avfilter_register_all();

    //Show Dshow Device
    //show_dshow_device();

    //printf("\nChoose video capture device: ");
    //if (gets(capture_name) == 0) {
    //    printf("Error in gets()\n");
    //    return -1;
    //}
    //sprintf(device_name, "video=%s", capture_name);

    //printf("\nChoose audio capture device: ");
    //if (gets(capture_name) == 0) {
    //    printf("Error in gets()\n");
    //    return -1;
    //}


    std::vector<TDeviceName> deviceList;

    DS_GetAudioVideoInputDevices(deviceList, CLSID_AudioInputDeviceCategory);

    if (deviceList.empty()) {
        printf("Couldn't open input stream.（无法打开音频设备）\n");
        return -1;
    }

    CString tmp = L"audio=";

    tmp.Append(deviceList[0].FriendlyName);

    char * psDevName = dup_wchar_to_utf8(tmp.GetBuffer());

    tmp.ReleaseBuffer();

    // sprintf(device_name_a, "%s", psDevName);

    //wchar_t *cam = L"video=Integrated Camera";
    //wchar_t *cam = L"video=YY伴侣";
    //char *device_name_utf8 = dup_wchar_to_utf8(cam);
    //wchar_t *cam_a = L"audio=麦克风阵列 (Realtek High Definition Audio)";
    //char *device_name_utf8_a = dup_wchar_to_utf8(cam_a);

    ifmt = av_find_input_format("gdigrab");

    //AVDictionary *options = NULL;
    //char tmpString[16];
    //sprintf_s(tmpString, "%d", 3);

    //av_dict_set(&options, "framerate", tmpString, NULL);

    ifmt_a = av_find_input_format("dshow");
    // Set device params
    AVDictionary *device_param = NULL;

    char tmpString[64];
    ////AVDictionary *options = NULL;
    sprintf_s(tmpString, "%d", FRAME_RATE);

    av_dict_set(&device_param, "framerate", tmpString, NULL);
    //av_dict_set(&device_param, "r", tmpString, NULL);
    //av_dict_set(&device_param, "nr", "10000", NULL);

    AVDictionary *device_param_a = NULL;

    // sprintf(tmpString, "threshold=%s:ratio=%s:attack=200:release=1000", threshold, ratio);



    // av_dict_set(&device_param_a, "acompressor", tmpString, 0);
    //char tmpString[16];
    ////AVDictionary *options = NULL;
    //sprintf_s(tmpString, "%d", FRAME_RATE);

    //av_dict_set(&device_param, "framerate", tmpString, NULL);
    //if not setting rtbufsize, error messages will be shown in cmd, but you can still watch or record the stream correctly in most time
    //setting rtbufsize will erase those error messages, however, larger rtbufsize will bring latency
    //av_dict_set(&device_param, "rtbufsize", "10M", 0);



    //Set own video device's name
    if (avformat_open_input(&ifmt_ctx, "desktop", ifmt, &device_param) != 0) {

        printf("Couldn't open input video stream.（无法打开输入流）\n");
        return -1;
    }
    //Set own audio device's name
    if (avformat_open_input(&ifmt_ctx_a, psDevName, ifmt_a, &device_param_a) != 0) {

        printf("Couldn't open input audio stream.（无法打开输入流）\n");
        return -1;
    }

    //回收
    av_free(psDevName);



    sprintf_s(tmpString, "%d", FRAME_RATE);

    av_dict_set(&device_param, "framerate", tmpString, NULL);
    //input video initialize
    if (avformat_find_stream_info(ifmt_ctx, &device_param) < 0) {
        printf("Couldn't find video stream information.（无法获取流信息）\n");
        return -1;
    }
    videoindex = -1;
    for (i = 0; i < ifmt_ctx->nb_streams; i++)
        if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoindex = i;
            break;
        }
    if (videoindex == -1) {
        printf("Couldn't find a video stream.（没有找到视频流）\n");
        return -1;
    }
    if (avcodec_open2(ifmt_ctx->streams[videoindex]->codec, avcodec_find_decoder(ifmt_ctx->streams[videoindex]->codec->codec_id), NULL) < 0) {
        printf("Could not open video codec.（无法打开解码器）\n");
        return -1;
    }
    //input audio initialize
    if (avformat_find_stream_info(ifmt_ctx_a, NULL) < 0) {
        printf("Couldn't find audio stream information.（无法获取流信息）\n");
        return -1;
    }
    audioindex = -1;

    AVCodecContext *codec_ctx=NULL;
    for (i = 0; i < ifmt_ctx_a->nb_streams; i++)
        if (ifmt_ctx_a->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioindex = i;
            //avcodec_find_decoder()

            //AVCodec *dec = avcodec_find_decoder(ifmt_ctx_a->streams[i]->codecpar->codec_id);

            codec_ctx = ifmt_ctx_a->streams[i]->codec;
            break;
        }
    if (audioindex == -1) {
        printf("Couldn't find a audio stream.（没有找到视频流）\n");
        return -1;
    }
    if (avcodec_open2(ifmt_ctx_a->streams[audioindex]->codec, avcodec_find_decoder(ifmt_ctx_a->streams[audioindex]->codec->codec_id), NULL) < 0) {
        printf("Could not open audio codec.（无法打开解码器）\n");
        return -1;
    }

    //output initialize
    sprintf(out_path, "output_th%s_ra%s.flv", threshold, ratio);
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_path);
    //output video encoder initialize
    pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!pCodec) {
        printf("Can not find output video encoder! (没有找到合适的编码器！)\n");
        return -1;
    }
    pCodecCtx = avcodec_alloc_context3(pCodec);
    pCodecCtx->pix_fmt = YUV_FORMAT;
    pCodecCtx->width = ifmt_ctx->streams[videoindex]->codec->width;
    pCodecCtx->height = ifmt_ctx->streams[videoindex]->codec->height;
    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = FRAME_RATE;
    pCodecCtx->bit_rate = 300000;
    //pCodecCtx->bit_rate = 3000;
    pCodecCtx->gop_size = 300;

    /* Some formats want stream headers to be separate. */
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        pCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

    //H264 codec param
    //pCodecCtx->me_range = 16;
    //pCodecCtx->max_qdiff = 4;
    //pCodecCtx->qcompress = 0.6;
    pCodecCtx->qmin = 10;
    pCodecCtx->qmax = 51;
    //Optional Param
    pCodecCtx->max_b_frames = 0;
    // Set H264 preset and tune
    AVDictionary *param = 0;
    av_dict_set(&param, "preset", "fast", 0);
    av_dict_set(&param, "tune", "zerolatency", 0);



    if (avcodec_open2(pCodecCtx, pCodec, &param) < 0) {
        printf("Failed to open output video encoder! (编码器打开失败！)\n");
        return -1;
    }



    //Add a new stream to output,should be called by the user before avformat_write_header() for muxing
    video_st = avformat_new_stream(ofmt_ctx, pCodec);
    if (video_st == NULL) {
        return -1;
    }
    video_st->time_base.num = 1;
    video_st->time_base.den = FRAME_RATE;
    video_st->codec = pCodecCtx;


    //output audio encoder initialize
    pCodec_a = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!pCodec_a) {
        printf("Can not find output audio encoder! (没有找到合适的编码器！)\n");
        return -1;
    }
    pCodecCtx_a = avcodec_alloc_context3(pCodec_a);
    pCodecCtx_a->channels = 2;
    pCodecCtx_a->channel_layout = av_get_default_channel_layout(2);
    pCodecCtx_a->sample_rate = ifmt_ctx_a->streams[audioindex]->codec->sample_rate;
    pCodecCtx_a->sample_fmt = pCodec_a->sample_fmts[0];
    pCodecCtx_a->bit_rate = 32000;
    pCodecCtx_a->time_base.num = 1;
    pCodecCtx_a->time_base.den = pCodecCtx_a->sample_rate;
    pCodecCtx_a->noise_reduction = 1;
    /** Allow the use of the experimental AAC encoder */
    pCodecCtx_a->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    /* Some formats want stream headers to be separate. */
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        pCodecCtx_a->flags |= CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(pCodecCtx_a, pCodec_a, NULL) < 0) {
        printf("Failed to open ouput audio encoder! (编码器打开失败！)\n");
        return -1;
    }

    //Add a new stream to output,should be called by the user before avformat_write_header() for muxing
    audio_st = avformat_new_stream(ofmt_ctx, pCodec_a);
    if (audio_st == NULL) {
        return -1;
    }
    audio_st->time_base.num = 1;
    audio_st->time_base.den = pCodecCtx_a->sample_rate;
    audio_st->codec = pCodecCtx_a;

    //Open output URL,set before avformat_write_header() for muxing
    if (avio_open(&ofmt_ctx->pb, out_path, AVIO_FLAG_READ_WRITE) < 0) {
        printf("Failed to open output file! (输出文件打开失败！)\n");
        return -1;
    }

    //Show some Information
    // av_dump_format(ofmt_ctx, 0, out_path, 1);

    //Write File Header
    avformat_write_header(ofmt_ctx, NULL);

    //prepare before decode and encode
    dec_pkt = (AVPacket *)av_malloc(sizeof(AVPacket));


    //camera data may has a pix fmt of RGB or sth else,convert it to YUV420
    img_convert_ctx = sws_getContext(ifmt_ctx->streams[videoindex]->codec->width,
                                     ifmt_ctx->streams[videoindex]->codec->height,
                                     ifmt_ctx->streams[videoindex]->codec->pix_fmt,
                                     pCodecCtx->width,
                                     pCodecCtx->height,
                                     pCodecCtx->pix_fmt,
                                     SWS_BICUBIC, NULL, NULL, NULL);

    // Initialize the resampler to be able to convert audio sample formats
    aud_convert_ctx = swr_alloc_set_opts(NULL,
                                         av_get_default_channel_layout(pCodecCtx_a->channels),
                                         pCodecCtx_a->sample_fmt,
                                         pCodecCtx_a->sample_rate,
                                         av_get_default_channel_layout(ifmt_ctx_a->streams[audioindex]->codec->channels),
                                         ifmt_ctx_a->streams[audioindex]->codec->sample_fmt,
                                         ifmt_ctx_a->streams[audioindex]->codec->sample_rate,
                                         0, NULL);

    /**
    * Perform a sanity check so that the number of converted samples is
    * not greater than the number of samples to be converted.
    * If the sample rates differ, this case has to be handled differently
    */
    //av_assert0(pCodecCtx_a->sample_rate == ifmt_ctx_a->streams[audioindex]->codec->sample_rate);

    swr_init(aud_convert_ctx);



    //@lw
    //Initialize the buffer to store YUV frames to be encoded.
    pFrameYUV = av_frame_alloc();
    /// pframe = av_frame_alloc();

    uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height));

    //avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
    //@lw
    avpicture_fill((AVPicture *)pFrameYUV, out_buffer, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

    //Initialize the FIFO buffer to store audio samples to be encoded.
    AVAudioFifo *fifo = NULL;
    fifo = av_audio_fifo_alloc(pCodecCtx_a->sample_fmt, pCodecCtx_a->channels, 1);

    //Initialize the buffer to store converted samples to be encoded.
    uint8_t **converted_input_samples =NULL;
    /**
    * Allocate as many pointers as there are audio channels.
    * Each pointer will later point to the audio samples of the corresponding
    * channels (although it may be NULL for interleaved formats).
    */
    if (!(converted_input_samples = (uint8_t**)calloc(pCodecCtx_a->channels,
                                    sizeof(**converted_input_samples)))) {
        printf("Could not allocate converted input sample pointers\n");
        return AVERROR(ENOMEM);
    }


    printf("\n --------call started----------\n");

    init_filters(ifmt_ctx_a, codec_ctx, pCodecCtx_a);

    //printf("\nPress enter to stop...\n");
    //hThread =

    // goto cleanup;

    CreateThread(
        NULL,                   // default security attributes
        0,                      // use default stack size
        MyThreadFunction,       // thread function name
        NULL,          // argument to thread function
        0,                      // use default creation flags
        NULL);   // returns the thread identifier

    //start decode and encode
    int64_t start_time = av_gettime();
    //int64_t index_v = 0,index_a=0;
    while (encode_video || encode_audio) {

        if (exit_thread == TYPE_THREAD::FINISH)
            break;

        if (encode_video &&
            (!encode_audio || av_compare_ts(vid_next_pts, time_base_q,
                                            aud_next_pts, time_base_q) <= 0)) {

            #pragma region[VIDEO]


            if ((ret = av_read_frame(ifmt_ctx, dec_pkt)) >= 0) {



                // av_log(NULL, AV_LOG_DEBUG, "Going to reencode the frame\n");
                pframe = av_frame_alloc();
                /*            if (!pframe) {
                                ret = AVERROR(ENOMEM);
                                return ret;
                            }*/



                ret = avcodec_decode_video2(ifmt_ctx->streams[dec_pkt->stream_index]->codec, pframe,
                                            &dec_got_frame, dec_pkt);
                if (ret < 0) {
                    //av_frame_free(&pframe);
                    //av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                    break;
                }
                if (dec_got_frame) {


                    //pFrameYUV = av_frame_alloc();

                    sws_scale(img_convert_ctx, (const uint8_t* const*)pframe->data, pframe->linesize,
                              0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);


                    //av_frame_unref(pframe);


                    //av_free(pframe->data);

                    //pFrameYUV->width = pframe->width;
                    //pFrameYUV->height = pframe->height;
                    //pFrameYUV->format = PIX_FMT_YUVJ420P;
                    ////pFrameYUV->format = PIX_FMT_YUV420P;
                    ////@lw
                    //pFrameYUV->format = pCodecCtx->pix_fmt;

                    enc_pkt.data = NULL;
                    enc_pkt.size = 0;
                    av_init_packet(&enc_pkt);
                    ret = avcodec_encode_video2(pCodecCtx, &enc_pkt, pFrameYUV, &enc_got_frame);
                    //av_frame_free(&pframe);

                    if (enc_got_frame == 1) {
                        //printf("Succeed to encode frame: %5d\tsize:%5d\n", framecnt, enc_pkt.size);
                        framecnt++;
                        enc_pkt.stream_index = video_st->index;

                        //Write PTS
                        AVRational time_base = ofmt_ctx->streams[0]->time_base;//{ 1, 1000 };
                        //AVRational r_framerate1 = ifmt_ctx->streams[videoindex]->r_frame_rate;//{ 50, 2 };
                        AVRational r_framerate1 = {FRAME_RATE,1};//{ 50, 2 };
                        //Duration between 2 frames (us)
                        int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳
                        //Parameters
                        //enc_pkt.pts = (double)(framecnt*calc_duration)*(double)(av_q2d(time_base_q)) / (double)(av_q2d(time_base));
                        enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
                        enc_pkt.dts = enc_pkt.pts;
                        enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base); //(double)(calc_duration)*(double)(av_q2d(time_base_q)) / (double)(av_q2d(time_base));
                        enc_pkt.pos = -1;
                        //printf("video pts : %d\n", enc_pkt.pts);

                        vid_next_pts = framecnt*calc_duration; //general timebase

                        //Delay
                        int64_t pts_time = av_rescale_q(enc_pkt.pts, time_base, time_base_q);
                        int64_t now_time = av_gettime() - start_time;
                        if ((pts_time > now_time) && ((vid_next_pts + pts_time - now_time)<aud_next_pts))
                            av_usleep(pts_time - now_time);

                        ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);

                        //av_write_frame()
                        // av_free_packet(&enc_pkt);
                    }

                    //av_frame_unref(pFrameYUV);
                    av_packet_unref(&enc_pkt);
                    // av_free(pFrameYUV->data);
                    //av_frame_alloc

                    // av_frame_free(&pFrameYUV);

                    //index_v++;
                }


                av_packet_unref(dec_pkt);
                av_frame_unref(pframe);
                // av_frame_free(&pframe);


            } else if (ret == AVERROR_EOF)
                encode_video = 0;
            else {
                printf("Could not read video frame\n");
                return ret;
            }

            #pragma endregion[VIDEO]


        } else {


            #pragma region[AUDIO]

            //audio trancoding here
            const int output_frame_size = pCodecCtx_a->frame_size;


            /**
            * Make sure that there is one frame worth of samples in the FIFO
            * buffer so that the encoder can do its work.
            * Since the decoder's and the encoder's frame size may differ, we
            * need to FIFO buffer to store as many frames worth of input samples
            * that they make up at least one frame worth of output samples.
            */
            while (av_audio_fifo_size(fifo) < output_frame_size) {
                /**
                * Decode one frame worth of audio samples, convert it to the
                * output sample format and put it into the FIFO buffer.
                */
                AVFrame *input_frame = av_frame_alloc();
                if (!input_frame) {
                    ret = AVERROR(ENOMEM);
                    return ret;
                }

                /** Decode one frame worth of audio samples. */
                /** Packet used for temporary storage. */
                AVPacket input_packet;
                av_init_packet(&input_packet);
                input_packet.data = NULL;
                input_packet.size = 0;

                /** Read one audio frame from the input file into a temporary packet. */
                if ((ret = av_read_frame(ifmt_ctx_a, &input_packet)) < 0) {
                    /** If we are at the end of the file, flush the decoder below. */
                    if (ret == AVERROR_EOF) {
                        encode_audio = 0;
                    } else {
                        printf("Could not read audio frame\n");
                        return ret;
                    }
                }

                /**
                * Decode the audio frame stored in the temporary packet.
                * The input audio stream decoder is used to do this.
                * If we are at the end of the file, pass an empty packet to the decoder
                * to flush it.
                */
                if ((ret = avcodec_decode_audio4(ifmt_ctx_a->streams[audioindex]->codec, input_frame,
                                                 &dec_got_frame_a, &input_packet)) < 0) {
                    printf("Could not decode audio frame\n");
                    return ret;
                }
                av_packet_unref(&input_packet);
                /** If there is decoded data, convert and store it */
                if (dec_got_frame_a) {
                    /**
                    * Allocate memory for the samples of all channels in one consecutive
                    * block for convenience.
                    */
                    if ((ret = av_samples_alloc(converted_input_samples, NULL,
                                                pCodecCtx_a->channels,
                                                input_frame->nb_samples,
                                                pCodecCtx_a->sample_fmt, 0)) < 0) {
                        printf("Could not allocate converted input samples\n");
                        /*                   av_freep(&(*converted_input_samples)[0]);
                                           free(converted_input_samples);*/
                        return ret;
                    }

                    /**
                    * Convert the input samples to the desired output sample format.
                    * This requires a temporary storage provided by converted_input_samples.
                    */
                    /** Convert the samples using the resampler. */
                    if ((ret = swr_convert(aud_convert_ctx,
                                           converted_input_samples, input_frame->nb_samples,
                                           (const uint8_t**)input_frame->extended_data, input_frame->nb_samples)) < 0) {
                        printf("Could not convert input samples\n");
                        return ret;
                    }

                    /** Add the converted input samples to the FIFO buffer for later processing. */
                    /**
                    * Make the FIFO as large as it needs to be to hold both,
                    * the old and the new samples.
                    */
                    if ((ret = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + input_frame->nb_samples)) < 0) {
                        printf("Could not reallocate FIFO\n");
                        return ret;
                    }

                    /** Store the new samples in the FIFO buffer. */
                    if (av_audio_fifo_write(fifo, (void **)converted_input_samples,
                                            input_frame->nb_samples) < input_frame->nb_samples) {
                        printf("Could not write data to FIFO\n");
                        return AVERROR_EXIT;
                    }
                }
            }

            /**
            * If we have enough samples for the encoder, we encode them.
            * At the end of the file, we pass the remaining samples to
            * the encoder.
            */
            if (av_audio_fifo_size(fifo) >= output_frame_size)
                /**
                * Take one frame worth of audio samples from the FIFO buffer,
                * encode it and write it to the output file.
                */
            {
                /** Temporary storage of the output samples of the frame written to the file. */
                AVFrame *output_frame = av_frame_alloc();
                if (!output_frame) {
                    ret = AVERROR(ENOMEM);
                    return ret;
                }
                /**
                * Use the maximum number of possible samples per frame.
                * If there is less than the maximum possible frame size in the FIFO
                * buffer use this number. Otherwise, use the maximum possible frame size
                */
                const int frame_size = FFMIN(av_audio_fifo_size(fifo),
                                             pCodecCtx_a->frame_size);

                /** Initialize temporary storage for one output frame. */
                /**
                * Set the frame's parameters, especially its size and format.
                * av_frame_get_buffer needs this to allocate memory for the
                * audio samples of the frame.
                * Default channel layouts based on the number of channels
                * are assumed for simplicity.
                */
                output_frame->nb_samples = frame_size;
                output_frame->channel_layout = pCodecCtx_a->channel_layout;
                output_frame->format = pCodecCtx_a->sample_fmt;
                output_frame->sample_rate = pCodecCtx_a->sample_rate;
                //output_frame->noise

                /**
                * Allocate the samples of the created frame. This call will make
                * sure that the audio frame can hold as many samples as specified.
                */
                if ((ret = av_frame_get_buffer(output_frame, 0)) < 0) {
                    printf("Could not allocate output frame samples\n");
                    av_frame_free(&output_frame);
                    return ret;
                }

                /**
                * Read as many samples from the FIFO buffer as required to fill the frame.
                * The samples are stored in the frame temporarily.
                */
                if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size) {
                    printf("Could not read data from FIFO\n");
                    return AVERROR_EXIT;
                }

                /** Encode one frame worth of audio samples. */
                /** Packet used for temporary storage. */
                AVPacket output_packet;
                av_init_packet(&output_packet);
                output_packet.data = NULL;
                output_packet.size = 0;

                /** Set a timestamp based on the sample rate for the container. */
                if (output_frame) {
                    nb_samples += output_frame->nb_samples;
                }
                /**
                * Encode the audio frame and store it in the temporary packet.
                * The output audio stream encoder is used to do this.
                */
                if ((ret = avcodec_encode_audio2(pCodecCtx_a, &output_packet,
                                                 output_frame, &enc_got_frame_a)) < 0) {
                    printf("Could not encode frame\n");
                    //av_packet_unref(&output_packet);
                    av_frame_free(&output_frame);
                    return ret;
                }



                /** Write one audio frame from the temporary packet to the output file. */
                if (enc_got_frame_a) {

                    if (0) {

                        AVRational time_base = ofmt_ctx->streams[1]->time_base;
                        AVRational r_framerate1 = { ifmt_ctx_a->streams[audioindex]->codec->sample_rate, 1 };// { 44100, 1};
                        int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));  //内部时间戳

                        output_frame->pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
                        // output_frame->pts = output_frame->best_effort_timestamp;
                        ret = filter_encode_write_frame(output_frame, 0, pCodecCtx_a, ifmt_ctx_a,
                                                        ofmt_ctx, audioindex, nb_samples, aud_next_pts);
                        if (ret < 0) {
                            return ret;
                        }
                    } else {
                        output_packet.stream_index = 1;

                        AVRational time_base = ofmt_ctx->streams[1]->time_base;
                        AVRational r_framerate1 = { ifmt_ctx_a->streams[audioindex]->codec->sample_rate, 1 };// { 44100, 1};
                        int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));  //内部时间戳

                        output_packet.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
                        output_packet.dts = output_packet.pts;
                        output_packet.duration = output_frame->nb_samples;

                        //printf("audio pts : %d\n", output_packet.pts);
                        aud_next_pts = nb_samples*calc_duration;

                        int64_t pts_time = av_rescale_q(output_packet.pts, time_base, time_base_q);
                        int64_t now_time = av_gettime() - start_time;
                        if ((pts_time > now_time) && ((aud_next_pts + pts_time - now_time)<vid_next_pts))
                            av_usleep(pts_time - now_time);

                        if ((ret = av_interleaved_write_frame(ofmt_ctx, &output_packet)) < 0) {
                            printf("Could not write frame\n");
                            //av_packet_unref(&output_packet);
                            return ret;
                        }
                    }



                    //filter_encode_write_frame(output_frame, 0, pCodecCtx_a, ifmt_ctx_a, ofmt_ctx);
                    av_packet_unref(&output_packet);
                }



                av_frame_free(&output_frame);
            } else {
                printf("dd");
            }

            #pragma endregion[AUDIO]
        }


        //Sleep(1);



        ///while
    }


    ////Flush Encoder
    ret = flush_encoder(ifmt_ctx, ofmt_ctx, 0, framecnt);
    if (ret < 0) {
        printf("Flushing encoder failed\n");
        //return -1;
    }
    ret = flush_encoder_a(ifmt_ctx_a, ofmt_ctx, 1, nb_samples);
    if (ret < 0) {
        printf("Flushing encoder failed\n");
        //return -1;
    }



    //Write file trailer
    av_write_trailer(ofmt_ctx);
    // return 0;
cleanup:
    //Clean

    //pCodecCtx
    //dec_pkt = (AVPacket *)av_malloc(sizeof(AVPacket));
    av_free(dec_pkt);
    //avcodec_close(pCodecCtx);
    //av_free(pCodecCtx);
    //avcodec_close(pCodecCtx_a);
    //av_free(pCodecCtx_a);

    av_dict_free(&param);
    av_dict_free(&device_param);
    av_dict_free(&device_param_a);
    av_frame_free(&pFrameYUV);
    av_frame_free(&pframe);






    av_free(out_buffer);

    if (encode_audio) {
        int size = sizeof(converted_input_samples) / sizeof(converted_input_samples[0]);

        for (int i = 0; i < size; i++) {
            if (converted_input_samples[i]) {
                av_freep(&converted_input_samples[i]);

            }

        }
    }



    if (fifo)
        av_audio_fifo_free(fifo);



    avformat_close_input(&ifmt_ctx);

    avformat_close_input(&ifmt_ctx_a);


    sws_freeContext(img_convert_ctx);
    swr_free(&aud_convert_ctx);
    //sws_freeContext(aud_convert_ctx);
    //swr_free(aud_convert_ctx);


    ///free_stream

    if (video_st) {
        avcodec_close(video_st->codec);
        //av_free(video_st->codec);

    }

    if (audio_st) {
        avcodec_close(audio_st->codec);
        //av_free(audio_st->codec);

    }


    if (0) {
        free(threshold);
        threshold = NULL;

        free(ratio);
        ratio = NULL;
    }

    int size = sizeof(filter_ctx) / sizeof(filter_ctx[0]);


    for (i = 0; i < 1; i++) {
        // avcodec_free_context(&stream_ctx[i].dec_ctx);
        /*    if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] )
                avcodec_free_context(&stream_ctx[i].enc_ctx);*/
        if (filter_ctx && filter_ctx[i].filter_graph)
            avfilter_graph_free(&filter_ctx[i].filter_graph);
    }

    // avio_close(ofmt_ctx->pb);
    avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    //@lw avcodec_free_context

    exit_thread = TYPE_THREAD::INIT;

    //avformat_free_context(ifmt_ctx_a);

    // return 0;


    // CloseHandle(hThread);
    return 0;
}

int flush_encoder(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, unsigned int stream_index, int framecnt)
{
    int ret;
    int got_frame;
    AVPacket enc_pkt;

    if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities &
          CODEC_CAP_DELAY))
        return 0;
    while (1) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_video2(ofmt_ctx->streams[stream_index]->codec, &enc_pkt,
                                    NULL, &got_frame);
        //av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame) {
            ret = 0;
            break;
        }
        printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
        framecnt++;
        //Write PTS
        AVRational time_base = ofmt_ctx->streams[stream_index]->time_base;//{ 1, 1000 };
        AVRational r_framerate1 = ifmt_ctx->streams[0]->r_frame_rate;// { 50, 2 };
        AVRational time_base_q = { 1, AV_TIME_BASE };
        //Duration between 2 frames (us)
        int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳
        //Parameters
        enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
        enc_pkt.dts = enc_pkt.pts;
        enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);

        /* copy packet*/
        //转换PTS/DTS（Convert PTS/DTS）
        enc_pkt.pos = -1;

        //ofmt_ctx->duration = enc_pkt.duration * framecnt;

        /* mux encoded frame */
        ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);

        // av_free_packet(&enc_pkt);
        av_packet_unref(&enc_pkt);
        /*   if (ret < 0)
               break;*/
    }

    av_free_packet(&enc_pkt);
    return ret;
}

int flush_encoder_a(AVFormatContext *ifmt_ctx_a, AVFormatContext *ofmt_ctx, unsigned int stream_index, int nb_samples)
{
    int ret;
    int got_frame;
    AVPacket enc_pkt;
    if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities &
          CODEC_CAP_DELAY))
        return 0;
    while (1) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_audio2(ofmt_ctx->streams[stream_index]->codec, &enc_pkt,
                                    NULL, &got_frame);
        //av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame) {
            ret = 0;
            break;
        }
        printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
        nb_samples += 1024;
        //Write PTS
        AVRational time_base = ofmt_ctx->streams[stream_index]->time_base;//{ 1, 1000 };
        AVRational r_framerate1 = { ifmt_ctx_a->streams[0]->codec->sample_rate, 1 };
        AVRational time_base_q = { 1, AV_TIME_BASE };
        //Duration between 2 frames (us)
        int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳
        //Parameters


        enc_pkt.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
        enc_pkt.dts = enc_pkt.pts;
        enc_pkt.duration = 1024;

        /* copy packet*/
        //转换PTS/DTS（Convert PTS/DTS）
        enc_pkt.pos = -1;

        //ofmt_ctx->duration = enc_pkt.duration * nb_samples;

        /* mux encoded frame */
        ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);

        // av_free_packet(&enc_pkt);
        av_packet_unref(&enc_pkt);
        //if (ret < 0)
        //    break;
    }

    av_free_packet(&enc_pkt);
    return ret;
}