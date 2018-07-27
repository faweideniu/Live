#pragma once
#include "stdafx.h"
extern "C"
{

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include "libavdevice/avdevice.h"

    typedef struct FilteringContext {
        AVFilterContext *buffersink_ctx;
        AVFilterContext *buffersrc_ctx;
        AVFilterGraph *filter_graph;
    } FilteringContext;

    typedef struct StreamContext {
        AVCodecContext *dec_ctx;
        AVCodecContext *enc_ctx;
    } StreamContext;

    DWORD WINAPI mainT(LPVOID lpParam);
    static int open_output_file(const char *filename);

}