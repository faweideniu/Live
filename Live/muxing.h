#pragma once
#include "stdafx.h"
extern "C"
{

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
//
//#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
//#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>s
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavdevice/avdevice.h>


    //typedef struct FilteringContext {
    //    AVFilterContext *buffersink_ctx;
    //    AVFilterContext *buffersrc_ctx;
    //    AVFilterGraph *filter_graph;
    //} FilteringContext;

    //typedef struct StreamContext {
    //    AVCodecContext *dec_ctx;
    //    AVCodecContext *enc_ctx;
    //} StreamContext;

    DWORD WINAPI main(LPVOID lpParam);
    //static int open_output_file(const char *filename);

}