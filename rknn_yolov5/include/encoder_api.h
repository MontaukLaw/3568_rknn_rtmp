#ifndef _ENCODER_API_H_
#define _ENCODER_API_H_

void init_encoder(int w, int h, int fps);

void encode_rgb_image_to_h264(char *data, int data_len, char *endoced_data, int *encoded_data_len);

#endif