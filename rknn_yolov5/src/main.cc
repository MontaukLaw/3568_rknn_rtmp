// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
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

#include "encoder_api.h"
#include <csignal>
#include "simple_rtmp_pusher.h"
#include "tcp_cmd.h"
/*-------------------------------------------
                  Functions
-------------------------------------------*/
#define VIDEO_WIDTH 1280
#define VIDEO_HEIGHT 960
#define MODEL_WIDTH 640
#define MODEL_HEIGHT 640
#define RKNN_RESULT_DELAY_FRAMES 10

#define CMD_SERVER_IP "159.75.182.56"
#define TCP_PORT 9999

static void printRKNNTensor(rknn_tensor_attr *attr)
{
    printf("index=%d name=%s n_dims=%d dims=[%d %d %d %d] n_elems=%d size=%d "
           "fmt=%d type=%d qnt_type=%d fl=%d zp=%d scale=%f\n",
           attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1],
           attr->dims[2], attr->dims[3], attr->n_elems, attr->size, 0, attr->type,
           attr->qnt_type, attr->fl, attr->zp, attr->scale);
}
double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }
long long __get_ms(struct timeval t) { return (t.tv_sec * 1000 + t.tv_usec / 1000); }

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz)
{
    unsigned char *data;
    int ret;

    data = NULL;

    if (NULL == fp)
    {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0)
    {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char *)malloc(sz);
    if (data == NULL)
    {
        printf("buffer malloc failure.\n");
        return NULL;
    }
    ret = fread(data, 1, sz, fp);
    return data;
}

static unsigned char *load_model(const char *filename, int *model_size)
{

    FILE *fp;
    unsigned char *data;

    fp = fopen(filename, "rb");
    if (NULL == fp)
    {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);

    data = load_data(fp, 0, size);

    fclose(fp);

    *model_size = size;
    return data;
}

int load_hyperm_param(MODEL_INFO *m, int argc, char **argv)
{
    if (argc != 5)
    {
        printf("Usage: %s [yolov5/yolov7/yolox] [fp/q8] <rknn model path> <anchor file path> <input_path>\n", argv[0]);
        printf("  -- [1] Model type, select from yolov5, yolov7, yolox\n");
        printf("  -- [2] Post process type, select from fp, q8. Only quantize-8bit model could use q8\n");
        printf("  -- [3] RKNN model path\n");
        printf("  -- [4] anchor file path. If using yolox model, any character is ok.\n");
        return -1;
    }
    int ret = 0;
    m->m_path = (char *)argv[3];
    char *anchor_path = argv[4];

    if (strcmp(argv[1], "yolov5") == 0)
    {
        m->m_type = YOLOV5;
        m->color_expect = RK_FORMAT_RGB_888;
        m->anchor_per_branch = 3;
        printf("Runing with yolov5 model\n");
    }
    else if (strcmp(argv[1], "yolov7") == 0)
    {
        m->m_type = YOLOV7;
        m->color_expect = RK_FORMAT_RGB_888;
        m->anchor_per_branch = 3;
        printf("Runing with yolov7 model\n");
    }
    else if (strcmp(argv[1], "yolox") == 0)
    {
        m->m_type = YOLOX;
        m->color_expect = RK_FORMAT_RGB_888;
        m->anchor_per_branch = 1;
        printf("Runing with yolox model\n");
        printf("Ignore anchors file %s\n", anchor_path);
    }
    else
    {
        printf("Only support yolov5/yolov7/yolox model, but got %s\n", argv[1]);
        return -1;
    }

    // load anchors
    int n = 2 * m->out_nodes * m->anchor_per_branch;
    if (m->m_type == YOLOX)
    {
        for (int i = 0; i < n; i++)
        {
            m->anchors[i] = 1;
        }
    }
    else
    {
        printf("anchors: ");
        float result[n];
        int valid_number;
        ret = readFloats(anchor_path, &result[0], n, &valid_number);
        for (int i = 0; i < valid_number; i++)
        {
            m->anchors[i] = (int)result[i];
            printf("%d ", m->anchors[i]);
        }
        printf("\n");
    }

    if (strcmp(argv[2], "fp") == 0)
    {
        m->post_type = FP;
        printf("Post process with fp\n");
    }
    else if (strcmp(argv[2], "q8") == 0)
    {
        m->post_type = Q8;
        printf("Post process with q8\n");
    }
    else
    {
        printf("Post process type not support: %s\nPlease select from [fp/q8]\n", argv[2]);
        return -1;
    }
    return 0;
}

int query_model_info(MODEL_INFO *m, rknn_context ctx)
{
    int ret;
    /* Query sdk version */
    rknn_sdk_version version;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version,
                     sizeof(rknn_sdk_version));
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("sdk version: %s driver version: %s\n", version.api_version,
           version.drv_version);

    /* Get input,output attr */
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("model input num: %d, output num: %d\n", io_num.n_input,
           io_num.n_output);
    m->in_nodes = io_num.n_input;
    m->out_nodes = io_num.n_output;
    m->in_attr = (rknn_tensor_attr *)malloc(sizeof(rknn_tensor_attr) * io_num.n_input);
    m->out_attr = (rknn_tensor_attr *)malloc(sizeof(rknn_tensor_attr) * io_num.n_output);
    if (m->in_attr == NULL || m->out_attr == NULL)
    {
        printf("alloc memery failed\n");
        return -1;
    }

    for (int i = 0; i < io_num.n_input; i++)
    {
        m->in_attr[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &m->in_attr[i],
                         sizeof(rknn_tensor_attr));
        if (ret < 0)
        {
            printf("rknn_init error ret=%d\n", ret);
            return -1;
        }
        printRKNNTensor(&m->in_attr[i]);
    }

    for (int i = 0; i < io_num.n_output; i++)
    {
        m->out_attr[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(m->out_attr[i]),
                         sizeof(rknn_tensor_attr));
        printRKNNTensor(&(m->out_attr[i]));
    }

    /* get input shape */
    if (io_num.n_input > 1)
    {
        printf("expect model have 1 input, but got %d\n", io_num.n_input);
        return -1;
    }

    if (m->in_attr[0].fmt == RKNN_TENSOR_NCHW)
    {
        printf("model is NCHW input fmt\n");
        m->width = m->in_attr[0].dims[0];
        m->height = m->in_attr[0].dims[1];
        m->channel = m->in_attr[0].dims[2];
    }
    else
    {
        printf("model is NHWC input fmt\n");
        m->width = m->in_attr[0].dims[2];
        m->height = m->in_attr[0].dims[1];
        m->channel = m->in_attr[0].dims[3];
    }
    printf("model input height=%d, width=%d, channel=%d\n", m->height, m->width,
           m->channel);

    return 0;
}

static int if_rknn_finished = 0;
static bool if_data_ready = false;
static int if_rknn_ready = true;
static bool if_quit = false;
static MODEL_INFO m_info;
static LETTER_BOX letter_box;
static rknn_input inputs[1];
static detect_result_group_t detect_result_group;
static cv::Mat buf_img;
static rknn_context ctx;
static rknn_output outputs[3];
static long long time_gap_total = 0;
bool start_process = false;

int resize_by_rga(char *resize_buf)
{

    int ret = 0;
    im_rect src_rect;
    im_rect dst_rect;
    rga_buffer_t src;
    rga_buffer_t dst;

    memset(&src_rect, 0, sizeof(src_rect));
    memset(&dst_rect, 0, sizeof(dst_rect));
    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));

    // printf("resize with RGA!\n");
    src = wrapbuffer_virtualaddr((void *)buf_img.data, VIDEO_WIDTH, VIDEO_HEIGHT, RK_FORMAT_RGB_888);
    dst = wrapbuffer_virtualaddr((void *)resize_buf, m_info.width, m_info.height, RK_FORMAT_RGB_888);
    ret = imcheck(src, dst, src_rect, dst_rect);
    if (IM_STATUS_NOERROR != ret)
    {
        printf("%d, check error! %s", __LINE__, imStrError((IM_STATUS)ret));
        return -1;
    }
    IM_STATUS STATUS = imresize(src, dst);

    // for debug
    cv::Mat resize_img(cv::Size(m_info.width, m_info.height), CV_8UC3, resize_buf);
    inputs[0].buf = resize_buf;

    return 0;
}
rga_context rga_ctx;

// 捕捉系统信号
void signal_handler(int signo)
{
    if (signo == SIGINT)
    {
        printf("receive SIGINT, quit now\n");
        if_quit = true;
    }
}

void *rknn_process(void *args)
{
    int ret;
    struct timeval start_time, stop_time;
    unsigned char *input_data = NULL;
    void *rk_outputs_buf[m_info.out_nodes];

    memset(&rga_ctx, 0, sizeof(rga_context));
    /* Create the neural network */
    printf("Loading model...\n");
    int model_data_size = 0;
    unsigned char *model_data = load_model(m_info.m_path, &model_data_size);
    ret = rknn_init(&ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return nullptr;
    }

    printf("query info\n");
    ret = query_model_info(&m_info, ctx);
    if (ret < 0)
    {
        return nullptr;
    }

    /* Init input tensor */
    // rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8; /* SAME AS INPUT IMAGE */
    inputs[0].size = m_info.width * m_info.height * m_info.channel;
    inputs[0].fmt = RKNN_TENSOR_NHWC; /* SAME AS INPUT IMAGE */
    inputs[0].pass_through = 0;

    /* Init output tensor */

    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < m_info.out_nodes; i++)
    {
        // printf("The info type: %d\n", m_info.post_type);
        outputs[i].want_float = m_info.post_type;
    }

    char *resize_buf = (char *)malloc(inputs[0].size);
    if (resize_buf == NULL)
    {
        printf("resize buf alloc failed\n");
        return nullptr;
    }

    while (if_quit == false)
    {

        // 当数据准备完毕的时候开始推理
        if (if_data_ready)
        {

            // printf("rknn process running\n");

            // if_rknn_finished = false;

            resize_by_rga(resize_buf);

            gettimeofday(&start_time, NULL);

            rknn_inputs_set(ctx, m_info.in_nodes, inputs);
            ret = rknn_run(ctx, NULL);
            ret = rknn_outputs_get(ctx, m_info.out_nodes, outputs, NULL);
            /* Post process */

            for (auto i = 0; i < m_info.out_nodes; i++)
            {
                rk_outputs_buf[i] = outputs[i].buf;
            }

            post_process(rk_outputs_buf, &m_info, &letter_box, &detect_result_group);

            gettimeofday(&stop_time, NULL);
            // printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);
            rknn_outputs_release(ctx, m_info.out_nodes, outputs);

            if (detect_result_group.count)
            {
                if_rknn_finished = RKNN_RESULT_DELAY_FRAMES;
            }

            if_rknn_ready = true;

            if_data_ready = false;
        }
        usleep(1);
    }

    RGA_deinit(&rga_ctx);
    if (model_data)
    {
        free(model_data);
    }
    if (resize_buf)
    {
        free(resize_buf);
    }
    return nullptr;
}

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int main(int argc, char **argv)
{

    int status = 0;

    static int frame_count = 0;

    // drm_context drm_ctx;
    void *drm_buf = NULL;
    // int drm_fd = -1;
    int buf_fd = -1; // converted from buffer handle
    unsigned int handle;

    // memset(&drm_ctx, 0, sizeof(drm_context));
    // drm_fd = drm_init(&drm_ctx);

    init_encoder(VIDEO_WIDTH, VIDEO_HEIGHT, 20);

    init_cmd_client(CMD_SERVER_IP, TCP_PORT);

    size_t actual_size = 0;
    int img_width = 0;
    int img_height = 0;
    int img_channel = 0;

    // init rga context

    int ret;

    ret = load_hyperm_param(&m_info, argc, argv);
    if (ret < 0)
        return -1;

    cv::VideoCapture cap(0);
    if (!cap.isOpened())
    {
        std::cout << "无法打开摄像头" << std::endl;
        return -1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, VIDEO_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, VIDEO_HEIGHT);
    // using opencv
    using namespace cv;
    using namespace std;
    cv::Mat orig_img;
    // cv::namedWindow("Video", cv::WINDOW_NORMAL);
    // cv::setWindowProperty("Video", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);

    FILE *fp = fopen("1280x960_cv.h264", "wb+");
    if (fp == NULL)
    {
        printf("open file failed\n");
        return -1;
    }

    // FILE *rgb_fp = fopen("1280x960_cv.rgb", "wb");
    char h264_data[1024 * 1024];
    int h264_len = 0;

    // Letter box resize
    letter_box.target_height = MODEL_HEIGHT;
    letter_box.target_width = MODEL_WIDTH;
    letter_box.in_height = VIDEO_HEIGHT;
    letter_box.in_width = VIDEO_WIDTH;

    printf("letter box: %d %d %d %d\n", letter_box.target_height, letter_box.target_width, letter_box.in_height, letter_box.in_width);
    compute_letter_box(&letter_box);

    // 启动新线程,用于推理
    pthread_t rknn_thread;
    pthread_create(&rknn_thread, NULL, rknn_process, NULL);

    // 初始化rtmp
    init_rtmp_connection("rtmp_sample.h264", "rtmp://159.75.182.56/live/livestream?secret=334a72b548e8443fa51531391bfe2a2f",
                         VIDEO_WIDTH, VIDEO_HEIGHT, 20);
    timeval last_frame_time;
    timeval this_frame_time;

    while (if_quit == false)
    {
        timeval start_time, stop_time;
        gettimeofday(&start_time, NULL);
        // 图送到服务器
        long long time_stamp = __get_ms(start_time);
        cap.read(orig_img);
        // cv::cvtColor(orig_img, orig_img, cv::COLOR_BGR2RGB);
        // cv::rotate(orig_img, orig_img, ROTATE_90_COUNTERCLOCKWISE);

        // 当rknn准备好, 就尝试复制数据
        if (if_rknn_ready)
        {
            // 复制数据
            // orig_img.copyTo(buf_img);
            cv::cvtColor(orig_img, buf_img, cv::COLOR_BGR2RGB);
            // 复制完, 标志位置1
            if_data_ready = true;
        }
        if (if_rknn_finished)
        {
            for (int i = 0; i < detect_result_group.count; i++)
            {
                detect_result_t *det_result = &(detect_result_group.results[i]);

                int left = det_result->box.left;
                int top = det_result->box.top;
                int right = det_result->box.right;
                int bottom = det_result->box.bottom;
                int w = (det_result->box.right - det_result->box.left);
                int h = (det_result->box.bottom - det_result->box.top);

                if (left < 0)
                {
                    left = 0;
                }
                if (top < 0)
                {
                    top = 0;
                }

                while ((uint32_t)(left + w) >= VIDEO_HEIGHT)
                {
                    w -= 16;
                }
                while ((uint32_t)(top + h) >= VIDEO_WIDTH)
                {
                    h -= 16;
                }

                // printf("border=(%d %d %d %d)\n", left, top, w, h);
                // printf("%s @ (%d %d %d %d) %f\n",
                //        det_result->name,
                //        det_result->box.left, det_result->box.top, det_result->box.right, det_result->box.bottom,
                //        det_result->prop);
                // 采用opencv来绘制矩形框,颜色格式是B、G、R
                using namespace cv;

                cv::rectangle(orig_img, cv::Point(left, top), cv::Point(right, bottom), cv::Scalar(0, 255, 255), 5, 8, 0);
                putText(orig_img, detect_result_group.results[i].name, Point(left, top - 16), FONT_HERSHEY_TRIPLEX, 3, Scalar(0, 0, 255), 4, 8, 0);
            }

            if_rknn_finished--;
        }

        if (start_process == false)
        {
            usleep(1);
            continue;
        }
        // img_width = img.cols;
        // img_height = img.rows;
        // printf("img width = %d, img height = %d\n", img_width, img_height);

        // gettimeofday(&start_time, NULL);
        // 进行h264编码
        encode_rgb_image_to_h264((char *)orig_img.data, VIDEO_HEIGHT * VIDEO_WIDTH * 3, h264_data, &h264_len);

        // gettimeofday(&stop_time, NULL);
        // printf("encode use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);

        // 统计一下时间
        // get_time_gap();

        send_to_rtmp_server((uint8_t *)h264_data, h264_len, time_stamp); // 20000);
        gettimeofday(&stop_time, NULL);
        // printf("send to rtmp use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);

        // fwrite(h264_data, h264_len, 1, fp);
        // cv::imshow("Video", orig_img);
        // if (cv::waitKey(1) == 'q')
        // {
        //     // release
        //     ret = rknn_destroy(ctx);
        // }

        if (frame_count == 0)
        {
            // 写入原始文件
            // fwrite((char *)orig_img.data, VIDEO_HEIGHT * VIDEO_WIDTH * 3, 1, rgb_fp);
        }

        gettimeofday(&this_frame_time, NULL);
        // printf("this:%lld\n", __get_ms(this_frame_time));
        // printf("last:%lld\n", __get_ms(last_frame_time));
        // printf("%lld\n", this_frame_time.tv_sec * 1000 + this_frame_time.tv_usec / 1000);
        long long time_gap_us = __get_us(this_frame_time) - __get_us(last_frame_time);
        if (time_gap_us < 0 || time_gap_us > 1000000)
        {
            time_gap_us = 0;
        }
        // printf("time gap: %lld\n", time_gap_us);
        time_gap_total = time_gap_total + (int)time_gap_us;
        // printf("time_gap_total: %d frame_count:%d\n", time_gap_total, frame_count);
        int avrage_time_gap_us = time_gap_total / frame_count;
        // printf("time between frames average: %d\n", avrage_time_gap_us);
        int delay_us = 50000 - avrage_time_gap_us;
        if (avrage_time_gap_us < 50000)
        {
            // printf("sleep %d ms\n", delay_us);
            // usleep(delay_us);
            // usleep((50 - avrage_time_gap) * 1000);
        }
        gettimeofday(&last_frame_time, NULL);
        usleep(1);
        frame_count++;
    }
    // release
    ret = rknn_destroy(ctx);

    if (m_info.in_attr)
    {
        free(m_info.in_attr);
    }

    if (m_info.out_attr)
    {
        free(m_info.out_attr);
    }

    return 0;
}
