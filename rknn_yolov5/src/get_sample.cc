#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <vector>
#include <string>

#include "drm_func.h"
#include "rga_func.h"
#include "rknn_api.h"
#include "yolo.h"

#include "RgaUtils.h"
#include "im2d.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include <opencv2/opencv.hpp>
#include "rga.h"

/*-------------------------------------------
                  Functions
-------------------------------------------*/
double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int main(int argc, char **argv)
{

    unsigned char *input_data = NULL;
    cv::VideoCapture cap(0);
    if (!cap.isOpened())
    {
        std::cout << "无法打开摄像头" << std::endl;
        return -1;
    }
    // cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    // cap.set(cv::CAP_PROP_FRAME_HEIGHT, 960);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    // using opencv
    using namespace cv;
    using namespace std;
    cv::Mat orig_img;
    cv::Mat img;

    timeval last_frame_time;
    timeval this_frame_time;
    while (true)
    {
        cap.read(orig_img);
        // cv::cvtColor(orig_img, img, cv::COLOR_BGR2RGB);

        // img_width = orig_img.cols;
        // img_height = orig_img.rows;
        // printf("img width = %d, img height = %d\n", img_width, img_height);

        // 统计一下帧率
        // static int frame_count = 0;
        // static double last_time = 0;
        static struct timeval stop_time;
        gettimeofday(&stop_time, NULL);
        // frame_count++;
        // double current_time = __get_us(stop_time) / 1000000;
        // // printf("current_time = %f\n", current_time);
        // if (current_time - last_time >= 1.0)
        // {
        //     printf("FPS: %d\n", frame_count);
        //     frame_count = 0;
        //     last_time = current_time;
        // }

        // 写入RGB文件
        // FILE *fp = fopen("1280x960.rgb", "ab");
        // if (fp == NULL)
        // {
        //     printf("open file failed\n");
        //     return -1;
        // }
        // fwrite(img.data, 1, img.cols * img.rows * 3, fp);
        printf("time between frames: %f\n", (__get_us(stop_time) - __get_us(last_frame_time)) / 1000);
        gettimeofday(&last_frame_time, NULL);

        usleep(1);
        // printf("Writing\n");
    }

    return 0;
}
