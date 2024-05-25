
#include "MyEncoder.h"

static char dst[1024 * 1024 * 4];
static char img[1024 * 1024 * 4];
static std::vector<char> buffer;
static MyEncoder myEncoder;

// 初始化编码器
void init_encoder(int w, int h, int fps)
{
    myEncoder.MppEncoderInit(w, h, 30);
    char *pdst = dst;
    // *3 因为是RGB格式
    const size_t imageSize = w * h * 3;
    buffer.resize(imageSize);
}

// 将raw格式编码为h264
void encode_rgb_image_to_h264(char *data, int data_len, char *endoced_data, int *encoded_data_len)
{
    myEncoder.encode(data, data_len, endoced_data, encoded_data_len);
}