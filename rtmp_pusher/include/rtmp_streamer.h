#ifndef _RTMP_STREAMMER_H_
#define _RTMP_STREAMMER_H_

void clean_rtmp(void);

void rtmp_init(void);

void rtmp_push_raw_data(int taglen, char *packet);

#endif