#ifndef _H264_TO_RTMP_H_
#define _H264_TO_RTMP_H_

extern "C"
{
#define __STDC_CONSTANT_MACROS
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
};

void rtmp_init(char *h264buffer, int iPsLength);

void push_rtmp(char *h264buffer, int iPsLength);

void clean_rtmp(void);

#endif