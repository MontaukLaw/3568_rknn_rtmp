#ifndef _SIMPLE_RTMP_PUSHER_H_
#define _SIMPLE_RTMP_PUSHER_H_

#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>

#ifdef __cplusplus
};
#endif

#include <string>

using namespace std;
// void init_rtmp_connection(void);
// void init_rtmp_connection(string h264_file_name, string rtmp_url);
void init_rtmp_connection(string h264_file_name, string rtmp_url, int width, int height, int fps);
// void send_to_rtmp_server(uint8_t *h264_data, int data_len);
//  void send_to_rtmp_server(uint8_t *h264_data, int data_len, int time_gap_ms);
void send_to_rtmp_server(uint8_t *h264_data, int data_len, long long time_stamp);

void set_start_time(void);

void get_time_gap(void);

#endif