//
// Created by z on 2020/7/17.
//
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iterator>
// #include <opencv2/core/core.hpp>
// #include <opencv2/videoio.hpp>
// #include <opencv2/highgui.hpp>
// #include <opencv2/imgcodecs.hpp>
// #include <opencv2/imgproc.hpp>

#include "MyEncoder.h"
#include "sys/time.h"
using namespace std;

char dst[1024 * 1024 * 4];
char img[1024 * 1024 * 4];

int main(void)
{

    int count = 0;
    int length = 0;
    std::cout << "1" << std::endl;
    MyEncoder myEncoder = MyEncoder();
    myEncoder.MppEncoderInit(1280, 960, 30);
    char *pdst = dst;
    FILE *fp = fopen("1280x960.h264", "wb+");
    // std::vector<char> file =
    // ReadFile("/demo/atk_rknn_yolo_v5_demo/1280x960.rgb");
    // printf("file size:%d\n", file.size());

    // ReadImageParts("/demo/atk_rknn_yolo_v5_demo/1280x960.rgb");
    const size_t imageSize = 1280 * 960 * 3; // 每个图像部分的大小
    const int numParts = 50;                 // 读取部分的数量
    const char *filename = "/demo/atk_rknn_yolo_v5_demo/1280x960.rgb";
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    std::vector<char> buffer(imageSize); // 单个部分的缓冲区

    for (int i = 0; i < numParts; ++i)
    {
        if (file.eof())
        {
            std::cerr << "Reached end of file before reading all parts." << std::endl;
            return false;
        }

        file.read(buffer.data(), imageSize);
        if (file.gcount() != imageSize)
        {
            std::cerr << "Error: Only read " << file.gcount() << " bytes, expected " << imageSize << " bytes in part " << i + 1 << std::endl;
            return false;
        }
        // 打个时间戳
        timeval tv;
        gettimeofday(&tv, NULL);
        myEncoder.encode(buffer.data(), buffer.size(), pdst, &length);
        // 计算编码时间
        timeval tv1;
        gettimeofday(&tv1, NULL);
        printf("encode time:%d ms\n", (tv1.tv_sec - tv.tv_sec) * 1000 + (tv1.tv_usec - tv.tv_usec) / 1000);
        fwrite(dst, length, 1, fp);
        // 处理缓冲区数据或进行后续处理
        // 示例：打印每部分的第一个字节，只为演示
        std::cout << "Part " << i + 1 << ", first byte: " << static_cast<int>(buffer[0]) << std::endl;
    }

    cout << "main test 6" << endl;
    fclose(fp);

    return 0;
}