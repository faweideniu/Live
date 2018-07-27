#include "stdafx.h"
#include "FFmpeg.h"
#include "DS_AudioVideoDevices.h"


#define YUV_FORMAT  AVPixelFormat::AV_PIX_FMT_YUVJ420P
#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER
#define FRAME_RATE 3
#define CODEC_CAP_DELAY AV_CODEC_CAP_DELAY
#define TIME_BASE_Q  { 1, AV_TIME_BASE }

char *dup_wchar_to_utf8(const wchar_t *w)
{
    char *s = NULL;
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    s = (char *)av_malloc(l);
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
    return s;
}

CFFmpeg::CFFmpeg()
{
    av_register_all();
    //Register Device
    avdevice_register_all();
    avformat_network_init();

    m_video = {0};
    m_audio = {0};
    m_output = {0};
    m_audioTime = { 0 };
    m_videoTime = { 0 };

}



CFFmpeg::~CFFmpeg()
{
    //avdevice_register_all();
    avformat_network_deinit();
}

int CFFmpeg::initDevice()
{
    int ret=0;

    ret = this->openAudio();

    if (ret < 0)
        return ret;

    ret = this->openVideo();
    if (ret < 0)
        return ret;


    return 0;
}

int CFFmpeg::openAudio()
{
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


    AVInputFormat*& ifmt_a = m_audio.ifmt;
    AVFormatContext *&ifmt_ctx_a = m_audio.ctx;

    ifmt_a = av_find_input_format("dshow");

    if (avformat_open_input(&ifmt_ctx_a, psDevName, ifmt_a, NULL) != 0) {

        printf("Couldn't open input audio stream.（无法打开输入流）\n");
        return -1;
    }
    if (avformat_find_stream_info(ifmt_ctx_a, NULL) < 0) {
        printf("Couldn't find audio stream information.（无法获取流信息）\n");
        return -1;
    }

    for (int i = 0; i < ifmt_ctx_a->nb_streams; i++)
        if (ifmt_ctx_a->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            //AVCodec *dec = avcodec_find_decoder(ifmt_ctx_a->streams[i]->codecpar->codec_id);

            m_audio.stream = ifmt_ctx_a->streams[i];
            break;
        }

    if (m_audio.stream) {
        printf("Couldn't find a audio stream.（没有找到视频流）\n");
        return -1;
    }

    if (avcodec_open2(m_video.stream->codec,
                      avcodec_find_decoder(m_video.stream->codec->codec_id), NULL) < 0) {
        printf("Could not open video codec.（无法打开解码器）\n");
        return -1;
    }



    av_free(psDevName);


    return 0;
}

int CFFmpeg::openVideo()
{
    AVInputFormat*& ifmt = m_video.ifmt;
    AVFormatContext *&ifmt_ctx = m_video.ctx;

    ifmt = av_find_input_format("gdigrab");

    if (avformat_open_input(&ifmt_ctx, "desktop", ifmt, NULL) != 0) {

        printf("Couldn't open input video stream.（无法打开输入流）\n");

        return -1;
    }
    // int  videoindex = -1;
    for (int i = 0; i < ifmt_ctx->nb_streams; i++)
        if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            //videoindex = i;
            m_video.stream = ifmt_ctx->streams[i];
            break;
        }

    if (m_video.stream) {
        printf("Couldn't find a video stream.（没有找到视频流）\n");
        return -1;
    }
    if (avcodec_open2(m_video.stream->codec,
                      avcodec_find_decoder(m_video.stream->codec->codec_id), NULL) < 0) {
        printf("Could not open video codec.（无法打开解码器）\n");
        return -1;
    }

    return 0;

}
int CFFmpeg::openOutput()
{

    char* out_path = "output.flv";

    AVFormatContext *&ofmt_ctx=m_output.ctx;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv",out_path);

    AVCodec* pCodec;
    AVCodecContext*& pCodecCtx=m_output.video_ctx;

    pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);

    if (!pCodec) {
        printf("Can not find output video encoder! (没有找到合适的编码器！)\n");
        return -1;
    }
    pCodecCtx = avcodec_alloc_context3(pCodec);
    pCodecCtx->pix_fmt = YUV_FORMAT;
    pCodecCtx->width = m_video.stream->codec->width;
    pCodecCtx->height = m_video.stream->codec->height;
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

    AVStream*& video_st=m_output.video_stream;
    video_st = avformat_new_stream(ofmt_ctx, pCodec);
    if (video_st == NULL) {
        return -1;
    }
    video_st->time_base.num = 1;
    video_st->time_base.den = FRAME_RATE;
    video_st->codec = pCodecCtx;

    AVCodec* pCodec_a ;

    AVCodecContext*& pCodecCtx_a = m_output.audio_ctx;
    //output audio encoder initialize
    pCodec_a = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!pCodec_a) {
        printf("Can not find output audio encoder! (没有找到合适的编码器！)\n");
        return -1;
    }
    pCodecCtx_a = avcodec_alloc_context3(pCodec_a);
    pCodecCtx_a->channels = 2;
    pCodecCtx_a->channel_layout = av_get_default_channel_layout(2);
    pCodecCtx_a->sample_rate = m_audio.stream->codec->sample_rate;
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

    AVStream*& audio_st=m_output.audio_stream;
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
    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        printf("Fialed to write header!");
        return -1;
    }



    return 0;
}

int CFFmpeg::initChange()
{


    //camera data may has a pix fmt of RGB or sth else,convert it to YUV420
    //img_convert_ctx = sws_getContext(ifmt_ctx->streams[videoindex]->codec->width,
    //                                 ifmt_ctx->streams[videoindex]->codec->height,
    //                                 ifmt_ctx->streams[videoindex]->codec->pix_fmt,
    //                                 pCodecCtx->width,
    //                                 pCodecCtx->height,
    //                                 pCodecCtx->pix_fmt,
    //                                 SWS_BICUBIC, NULL, NULL, NULL);
    m_convertCtx = sws_getContext(m_video.stream->codec->width,
                                  m_video.stream->codec->height,
                                  m_video.stream->codec->pix_fmt,
                                  m_output.video_ctx->width,
                                  m_output.video_ctx->height,
                                  m_output.video_ctx->pix_fmt,
                                  SWS_BICUBIC, NULL, NULL, NULL);

    return 0;

    //// Initialize the resampler to be able to convert audio sample formats
    //aud_convert_ctx = swr_alloc_set_opts(NULL,
    //                                     av_get_default_channel_layout(pCodecCtx_a->channels),
    //                                     pCodecCtx_a->sample_fmt,
    //                                     pCodecCtx_a->sample_rate,
    //                                     av_get_default_channel_layout(ifmt_ctx_a->streams[audioindex]->codec->channels),
    //                                     ifmt_ctx_a->streams[audioindex]->codec->sample_fmt,
    //                                     ifmt_ctx_a->streams[audioindex]->codec->sample_rate,
    //                                     0, NULL);
}

int CFFmpeg::initTime()
{
    //m_audio.stream->codec->sample_rate
    //int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));
    m_audioTime.dela = (double)(AV_TIME_BASE)*(1 / av_q2d({ m_audio.stream->codec->sample_rate, 1 }));
    //(double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1))
    m_videoTime.dela = (double)(AV_TIME_BASE)*(1 / av_q2d({ FRAME_RATE, 1 }));

    m_audioTime.duration = m_videoTime.duration = 0;

}

int CFFmpeg::process()
{
    //int ret = 0;



    if (this->initDevice() < 0)
        return -1;

    if (this->openOutput() < 0)
        return -1;

    this->initChange();
    this->initTime();


    ////Initialize the buffer to store YUV frames to be encoded.
    //AVFrame* pFrameYUV = av_frame_alloc();
    ///// pframe = av_frame_alloc();

    //AVCodecContext*& pCodecCtx=m_video.stream->codec;

    //uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height));

    ////avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
    ////@lw
    //avpicture_fill((AVPicture *)pFrameYUV, out_buffer, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);


    ////Initialize the buffer to store converted samples to be encoded.
    //uint8_t **converted_input_samples = NULL;
    ///**
    //* Allocate as many pointers as there are audio channels.
    //* Each pointer will later point to the audio samples of the corresponding
    //* channels (although it may be NULL for interleaved formats).
    //*/
    //if (!(converted_input_samples = (uint8_t**)calloc(pCodecCtx_a->channels,
    //                                sizeof(**converted_input_samples)))) {
    //    printf("Could not allocate converted input sample pointers\n");
    //    return AVERROR(ENOMEM);
    //}


    int encode_video = 1, encode_audio = 1;

    while (encode_video | encode_audio) {
        if (encode_video &&
            (!encode_audio || av_compare_ts(m_videoTime.duration, TIME_BASE_Q,
                                            m_videoTime.duration, TIME_BASE_Q) <= 0)) {
            //process audio
            if (this->processVideo() < 0) {
                return -1;
            }


        } else {
            //progress
            if (this->processAudio() < 0) {
                return -1;
            }
        }

    }
}
int CFFmpeg::processVideo(int &encode_video)
{
    int ret = 0, dec_got_frame=0;
    AVCodecContext*& pCodecCtx = m_output.video_ctx;
    AVPacket* dec_pkt = av_packet_alloc();
    AVFrame *pframe = NULL;

    AVFrame * pFrameYUV = av_frame_alloc();
    /// pframe = av_frame_alloc();

    uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(m_output.video_ctx->pix_fmt,
                          m_output.video_ctx->width, m_output.video_ctx->height));

    //avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
    //@lw
    avpicture_fill((AVPicture *)pFrameYUV, out_buffer, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

    //av_image_fill_arrays()

    ret = av_read_frame(m_video.ctx, dec_pkt);
    if (ret == AVERROR_EOF) {
        encode_video = 0;
        return 0;
    } else if (ret < 0) {
        goto cleanup;
    }


    pframe=av_frame_alloc();
    ret = avcodec_decode_video2(m_video.ctx->streams[dec_pkt->stream_index]->codec, pframe,
                                &dec_got_frame, dec_pkt);

    if (ret < 0) {
        goto cleanup;
    }

    if (!dec_got_frame) {

        goto cleanup;
    }

    sws_scale(m_convertCtx, (const uint8_t* const*)pframe->data, pframe->linesize,
              0, m_output.video_ctx->height, pFrameYUV->data, pFrameYUV->linesize);




cleanup:

    av_frame_free(&pframe);
    av_frame_free(&pFrameYUV);
    av_packet_free(&dec_pkt);
    av_free(out_buffer);
    return ret;
}