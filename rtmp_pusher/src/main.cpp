#define MODULE_TAG "mpi_enc_test"

#include <string.h>
#include "rk_mpi.h"

#include "mpp_env.h"
#include "mpp_mem.h"
#include "mpp_time.h"
#include "mpp_debug.h"
#include "mpp_common.h"

#include "utils.h"
#include "mpi_enc_utils.h"
#include "camera_source.h"
#include "mpp_enc_roi_utils.h"
#include "mpp_rc_api.h"
// #include "rtmp_streamer.h"
// #include "h264_to_rtmp.h"
#include "simple_rtmp_pusher.h"

// #include <sys/socket.h>
//  #include <netinet/in.h>
#include <time.h>
#include <arpa/inet.h>

#define REMOTE_SERVER "43.139.145.196"
#define REMOTE_PORT 9999
#define REMOTE_TCP_PORT 9998

#define UDP_PACK_SIZE 1024

typedef struct
{
    // base flow context
    MppCtx ctx;
    MppApi *mpi;
    RK_S32 chn;

    // global flow control flag
    RK_U32 frm_eos;
    RK_U32 pkt_eos;
    RK_U32 frm_pkt_cnt;
    RK_S32 frame_num;
    RK_S32 frame_count;
    RK_U64 stream_size;
    /* end of encoding flag when set quit the loop */
    volatile RK_U32 loop_end;

    // src and dst
    FILE *fp_input;
    FILE *fp_output;
    FILE *fp_verify;

    /* encoder config set */
    MppEncCfg cfg;
    MppEncPrepCfg prep_cfg;
    MppEncRcCfg rc_cfg;
    MppEncCodecCfg codec_cfg;
    MppEncSliceSplit split_cfg;
    MppEncOSDPltCfg osd_plt_cfg;
    MppEncOSDPlt osd_plt;
    MppEncOSDData osd_data;
    RoiRegionCfg roi_region;
    MppEncROICfg roi_cfg;

    // input / output
    MppBufferGroup buf_grp;
    MppBuffer frm_buf;
    MppBuffer pkt_buf;
    MppBuffer md_info;
    MppEncSeiMode sei_mode;
    MppEncHeaderMode header_mode;

    // paramter for resource malloc
    RK_U32 width;
    RK_U32 height;
    RK_U32 hor_stride;
    RK_U32 ver_stride;
    MppFrameFormat fmt;
    MppCodingType type;
    RK_S32 loop_times;
    CamSource *cam_ctx;
    MppEncRoiCtx roi_ctx;

    // resources
    size_t header_size;
    size_t frame_size;
    size_t mdinfo_size;
    /* NOTE: packet buffer may overflow */
    size_t packet_size;

    RK_U32 osd_enable;
    RK_U32 osd_mode;
    RK_U32 split_mode;
    RK_U32 split_arg;
    RK_U32 split_out;

    RK_U32 user_data_enable;
    RK_U32 roi_enable;

    // rate control runtime parameter
    RK_S32 fps_in_flex;
    RK_S32 fps_in_den;
    RK_S32 fps_in_num;
    RK_S32 fps_out_flex;
    RK_S32 fps_out_den;
    RK_S32 fps_out_num;
    RK_S32 bps;
    RK_S32 bps_max;
    RK_S32 bps_min;
    RK_S32 rc_mode;
    RK_S32 gop_mode;
    RK_S32 gop_len;
    RK_S32 vi_len;
    RK_S32 scene_mode;

    RK_S64 first_frm;
    RK_S64 first_pkt;
} MpiEncTestData;

/* For each instance thread return value */
typedef struct
{
    float frame_rate;
    RK_U64 bit_rate;
    RK_S64 elapsed_time;
    RK_S32 frame_count;
    RK_S64 stream_size;
    RK_S64 delay;
} MpiEncMultiCtxRet;

typedef struct
{
    MpiEncTestArgs *cmd; // pointer to global command line info
    const char *name;
    RK_S32 chn;

    pthread_t thd;         // thread for for each instance
    MpiEncTestData ctx;    // context of encoder
    MpiEncMultiCtxRet ret; // return of encoder
} MpiEncMultiCtxInfo;

MPP_RET test_ctx_init(MpiEncMultiCtxInfo *info)
{
    MpiEncTestArgs *cmd = info->cmd;
    MpiEncTestData *p = &info->ctx;
    MPP_RET ret = MPP_OK;

    // get paramter from cmd
    p->width = cmd->width;
    p->height = cmd->height;
    p->hor_stride = (cmd->hor_stride) ? (cmd->hor_stride) : (MPP_ALIGN(cmd->width, 16));
    p->ver_stride = (cmd->ver_stride) ? (cmd->ver_stride) : (MPP_ALIGN(cmd->height, 16));
    p->fmt = cmd->format;
    p->type = cmd->type;
    p->bps = 20000000;           // cmd->bps_target;
    p->bps_min = p->bps * 5 / 4; // cmd->bps_min;
    p->bps_max = p->bps * 3 / 4; // cmd->bps_max;
    p->rc_mode = cmd->rc_mode;
    p->frame_num = cmd->frame_num;
    if (cmd->type == MPP_VIDEO_CodingMJPEG && p->frame_num == 0)
    {
        printf("jpege default encode only one frame. Use -n [num] for rc case\n");
        p->frame_num = 1;
    }
    p->gop_mode = cmd->gop_mode;
    p->gop_len = cmd->gop_len;
    p->vi_len = cmd->vi_len;

    printf("gop_mode:%d gop_len :%d vi_len:%d \n", cmd->gop_mode, cmd->gop_len, cmd->vi_len);

    p->fps_in_flex = cmd->fps_in_flex;
    p->fps_in_den = cmd->fps_in_den;
    p->fps_in_num = cmd->fps_in_num;
    p->fps_out_flex = cmd->fps_out_flex;
    p->fps_out_den = cmd->fps_out_den;
    p->fps_out_num = cmd->fps_out_num;
    p->scene_mode = cmd->scene_mode;
    p->mdinfo_size = (MPP_VIDEO_CodingHEVC == cmd->type) ? (MPP_ALIGN(p->hor_stride, 32) >> 5) *
                                                               (MPP_ALIGN(p->ver_stride, 32) >> 5) * 16
                                                         : (MPP_ALIGN(p->hor_stride, 64) >> 6) *
                                                               (MPP_ALIGN(p->ver_stride, 16) >> 4) * 16;

    printf("open camera device\n");
    p->cam_ctx = camera_source_init(cmd->file_input, 4, p->width, p->height, p->fmt);
    printf("new framecap ok");
    if (p->cam_ctx == NULL)
        mpp_err("open %s fail", cmd->file_input);

    if (cmd->file_output)
    {
        p->fp_output = fopen(cmd->file_output, "w+b");
        if (NULL == p->fp_output)
        {
            mpp_err("failed to open output file %s\n", cmd->file_output);
            ret = MPP_ERR_OPEN_FILE;
        }
    }
    p->frame_size = MPP_ALIGN(p->hor_stride, 64) * MPP_ALIGN(p->ver_stride, 64) * 3 / 2;
    printf("p->frame_size :%d\n", p->frame_size);

    p->header_size = 0;
    printf("p->header_size :%d\n", p->header_size);

    return ret;
}

MPP_RET test_ctx_deinit(MpiEncTestData *p)
{
    if (p)
    {
        if (p->cam_ctx)
        {
            camera_source_deinit(p->cam_ctx);
            p->cam_ctx = NULL;
        }
        if (p->fp_input)
        {
            fclose(p->fp_input);
            p->fp_input = NULL;
        }
        if (p->fp_output)
        {
            fclose(p->fp_output);
            p->fp_output = NULL;
        }
        if (p->fp_verify)
        {
            fclose(p->fp_verify);
            p->fp_verify = NULL;
        }
    }
    return MPP_OK;
}

MPP_RET test_mpp_enc_cfg_setup(MpiEncMultiCtxInfo *info)
{
    MpiEncTestArgs *cmd = info->cmd;
    MpiEncTestData *p = &info->ctx;
    MppApi *mpi = p->mpi;
    MppCtx ctx = p->ctx;
    MppEncCfg cfg = p->cfg;
    RK_U32 quiet = cmd->quiet;
    MPP_RET ret;
    RK_U32 rotation;
    RK_U32 mirroring;
    RK_U32 flip;
    RK_U32 gop_mode = p->gop_mode;
    MppEncRefCfg ref = NULL;

    /* setup default parameter */
    if (p->fps_in_den == 0)
        p->fps_in_den = 1;
    if (p->fps_in_num == 0)
        p->fps_in_num = 30;
    if (p->fps_out_den == 0)
        p->fps_out_den = 1;
    if (p->fps_out_num == 0)
        p->fps_out_num = 30;

    printf("p->fps_in_den : %d p->fps_in_num: %d \n", p->fps_in_den, p->fps_in_num);
    printf("p->fps_out_den : %d p->fps_out_num: %d \n", p->fps_out_den, p->fps_out_num);

    if (!p->bps)
        p->bps = p->width * p->height / 8 * (p->fps_out_num / p->fps_out_den);

    mpp_enc_cfg_set_s32(cfg, "tune:scene_mode", p->scene_mode);
    mpp_enc_cfg_set_s32(cfg, "prep:width", p->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", p->height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", p->hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", p->ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", p->fmt);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", p->rc_mode);

    /* fix input / output frame rate */
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", p->fps_in_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", p->fps_in_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", p->fps_in_den);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", p->fps_out_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", p->fps_out_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", p->fps_out_den);

    /* drop frame or not when bitrate overflow */
    mpp_enc_cfg_set_u32(cfg, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    mpp_enc_cfg_set_u32(cfg, "rc:drop_thd", 20); /* 20% of max bps */
    mpp_enc_cfg_set_u32(cfg, "rc:drop_gap", 1);  /* Do not continuous drop frame */

    /* setup bitrate for different rc_mode */
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", p->bps);
    switch (p->rc_mode)
    {
    case MPP_ENC_RC_MODE_FIXQP:
    {
        /* do not setup bitrate on FIXQP mode */
    }
    break;
    case MPP_ENC_RC_MODE_CBR:
    {
        /* CBR mode has narrow bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps_max ? p->bps_max : p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps_min ? p->bps_min : p->bps * 15 / 16);
    }
    break;
    case MPP_ENC_RC_MODE_VBR:
    case MPP_ENC_RC_MODE_AVBR:
    {
        /* VBR mode has wide bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps_max ? p->bps_max : p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps_min ? p->bps_min : p->bps * 1 / 16);
    }
    break;
    default:
    {
        /* default use CBR mode */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps_max ? p->bps_max : p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps_min ? p->bps_min : p->bps * 15 / 16);
    }
    break;
    }

    /* setup qp for different codec and rc_mode */
    switch (p->type)
    {
    case MPP_VIDEO_CodingAVC:
    case MPP_VIDEO_CodingHEVC:
    {
        switch (p->rc_mode)
        {
        case MPP_ENC_RC_MODE_FIXQP:
        {
            RK_S32 fix_qp = cmd->qp_init;

            mpp_enc_cfg_set_s32(cfg, "rc:qp_init", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 0);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_i", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_i", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_p", fix_qp);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_p", fix_qp);
        }
        break;
        case MPP_ENC_RC_MODE_CBR:
        case MPP_ENC_RC_MODE_VBR:
        case MPP_ENC_RC_MODE_AVBR:
        {
            mpp_enc_cfg_set_s32(cfg, "rc:qp_init", cmd->qp_init ? cmd->qp_init : -1);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max", cmd->qp_max ? cmd->qp_max : 51);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min", cmd->qp_min ? cmd->qp_min : 10);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", cmd->qp_max_i ? cmd->qp_max_i : 51);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", cmd->qp_min_i ? cmd->qp_min_i : 10);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 2);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_i", cmd->fqp_min_i ? cmd->fqp_min_i : 10);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_i", cmd->fqp_max_i ? cmd->fqp_max_i : 51);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_min_p", cmd->fqp_min_p ? cmd->fqp_min_p : 10);
            mpp_enc_cfg_set_s32(cfg, "rc:fqp_max_p", cmd->fqp_max_p ? cmd->fqp_max_p : 51);
        }
        break;
        default:
        {
            mpp_err_f("unsupport encoder rc mode %d\n", p->rc_mode);
        }
        break;
        }
    }
    break;
    case MPP_VIDEO_CodingVP8:
    {
        /* vp8 only setup base qp range */
        mpp_enc_cfg_set_s32(cfg, "rc:qp_init", cmd->qp_init ? cmd->qp_init : 40);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_max", cmd->qp_max ? cmd->qp_max : 127);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_min", cmd->qp_min ? cmd->qp_min : 0);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", cmd->qp_max_i ? cmd->qp_max_i : 127);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", cmd->qp_min_i ? cmd->qp_min_i : 0);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 6);
    }
    break;
    case MPP_VIDEO_CodingMJPEG:
    {
        /* jpeg use special codec config to control qtable */
        mpp_enc_cfg_set_s32(cfg, "jpeg:q_factor", cmd->qp_init ? cmd->qp_init : 80);
        mpp_enc_cfg_set_s32(cfg, "jpeg:qf_max", cmd->qp_max ? cmd->qp_max : 99);
        mpp_enc_cfg_set_s32(cfg, "jpeg:qf_min", cmd->qp_min ? cmd->qp_min : 1);
    }
    break;
    default:
    {
    }
    break;
    }

    /* setup codec  */
    mpp_enc_cfg_set_s32(cfg, "codec:type", p->type);
    switch (p->type)
    {
    case MPP_VIDEO_CodingAVC:
    {
        RK_U32 constraint_set;

        /*
         * H.264 profile_idc parameter
         * 66  - Baseline profile
         * 77  - Main profile
         * 100 - High profile
         */
        mpp_enc_cfg_set_s32(cfg, "h264:profile", 100);
        /*
         * H.264 level_idc parameter
         * 10 / 11 / 12 / 13    - qcif@15fps / cif@7.5fps / cif@15fps / cif@30fps
         * 20 / 21 / 22         - cif@30fps / half-D1@@25fps / D1@12.5fps
         * 30 / 31 / 32         - D1@25fps / 720p@30fps / 720p@60fps
         * 40 / 41 / 42         - 1080p@30fps / 1080p@30fps / 1080p@60fps
         * 50 / 51 / 52         - 4K@30fps
         */
        mpp_enc_cfg_set_s32(cfg, "h264:level", 40);
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
        mpp_enc_cfg_set_s32(cfg, "h264:trans8x8", 1);

        mpp_env_get_u32("constraint_set", &constraint_set, 0);
        if (constraint_set & 0x3f0000)
            mpp_enc_cfg_set_s32(cfg, "h264:constraint_set", constraint_set);
    }
    break;
    case MPP_VIDEO_CodingHEVC:
    case MPP_VIDEO_CodingMJPEG:
    case MPP_VIDEO_CodingVP8:
    {
    }
    break;
    default:
    {
        mpp_err_f("unsupport encoder coding type %d\n", p->type);
    }
    break;
    }

    p->split_mode = 0;
    p->split_arg = 0;
    p->split_out = 0;

    mpp_env_get_u32("split_mode", &p->split_mode, MPP_ENC_SPLIT_NONE);
    mpp_env_get_u32("split_arg", &p->split_arg, 0);
    mpp_env_get_u32("split_out", &p->split_out, 0);

    if (p->split_mode)
    {
        printf("%p split mode %d arg %d out %d\n", ctx, p->split_mode, p->split_arg, p->split_out);
        mpp_enc_cfg_set_s32(cfg, "split:mode", p->split_mode);
        mpp_enc_cfg_set_s32(cfg, "split:arg", p->split_arg);
        mpp_enc_cfg_set_s32(cfg, "split:out", p->split_out);
    }

    mpp_env_get_u32("mirroring", &mirroring, 0);
    mpp_env_get_u32("rotation", &rotation, 0);
    mpp_env_get_u32("flip", &flip, 0);

    mpp_enc_cfg_set_s32(cfg, "prep:mirroring", mirroring);
    mpp_enc_cfg_set_s32(cfg, "prep:rotation", rotation);
    mpp_enc_cfg_set_s32(cfg, "prep:flip", flip);

    int gop_ = p->gop_len ? p->gop_len : p->fps_out_num * 2;
    printf("p->gop_len :%d p->fps_out_num : %d gop_len: %d\n", p->gop_len, p->fps_out_num, gop_);

    mpp_enc_cfg_set_s32(cfg, "rc:gop", p->gop_len ? p->gop_len : p->fps_out_num);
    mpp_env_get_u32("gop_mode", &gop_mode, gop_mode);

    if (gop_mode)
    {
        mpp_enc_ref_cfg_init(&ref);

        if (p->gop_mode < 4)
            mpi_enc_gen_ref_cfg(ref, gop_mode);
        else
            mpi_enc_gen_smart_gop_ref_cfg(ref, p->gop_len, p->vi_len);

        mpp_enc_cfg_set_ptr(cfg, "rc:ref_cfg", ref);
    }

    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret)
    {
        mpp_err("mpi control enc set cfg failed ret %d\n", ret);
        goto RET;
    }

    if (ref)
        mpp_enc_ref_cfg_deinit(&ref);

    /* optional */
    {
        RK_U32 sei_mode;

        mpp_env_get_u32("sei_mode", &sei_mode, MPP_ENC_SEI_MODE_ONE_FRAME);
        p->sei_mode = (MppEncSeiMode)sei_mode;
        ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &p->sei_mode);
        if (ret)
        {
            mpp_err("mpi control enc set sei cfg failed ret %d\n", ret);
            goto RET;
        }
    }

    if (p->type == MPP_VIDEO_CodingAVC || p->type == MPP_VIDEO_CodingHEVC)
    {
        p->header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &p->header_mode);
        if (ret)
        {
            mpp_err("mpi control enc set header mode failed ret %d\n", ret);
            goto RET;
        }
    }

    /* setup test mode by env */
    mpp_env_get_u32("osd_enable", &p->osd_enable, 0);
    mpp_env_get_u32("osd_mode", &p->osd_mode, MPP_ENC_OSD_PLT_TYPE_DEFAULT);
    mpp_env_get_u32("roi_enable", &p->roi_enable, 0);
    mpp_env_get_u32("user_data_enable", &p->user_data_enable, 0);

    if (p->roi_enable)
    {
        mpp_enc_roi_init(&p->roi_ctx, p->width, p->height, p->type, 4);
        mpp_assert(p->roi_ctx);
    }

RET:
    return ret;
}

static int sockfd;
static struct sockaddr_in server_addr;
static struct timeval tv_now;
static struct timeval last_sec_tv;

int init_tcp_connection(void)
{

    return 0;

    // 创建TCP套接字
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    // 设置服务器地址结构
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("43.139.145.196");
    // server_addr.sin_addr.s_addr = inet_addr("192.168.1.88");
    // server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(REMOTE_TCP_PORT);

    // 连接服务器
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        return -1;
    }

    return 0;
}

void send_data_by_tcp(int buf_size, char *buf_data)
{
    // 使用udp发送数据到remote server
    // char sendline[buf_size];
    char *sendline = NULL;
    int n;
    int i = 0;
    // int pack_size = UDP_PACK_SIZE;
    int pack_num = buf_size / UDP_PACK_SIZE;
    int last_pack_size = buf_size % UDP_PACK_SIZE;
    int total_bytes = pack_num * UDP_PACK_SIZE + last_pack_size;

    // 分包发送所有数据
    for (i = 0; i < pack_num; i++)
    {
        sendline = buf_data + i * UDP_PACK_SIZE;
        send(sockfd, sendline, UDP_PACK_SIZE, 0);
        // send_data_to_tcp(REMOTE_SERVER, REMOTE_PORT, sendline, UDP_PACK_SIZE);
        //  usleep(1000);
    }

    if (last_pack_size > 0)
    {
        sendline = buf_data + pack_num * UDP_PACK_SIZE;
        send(sockfd, sendline, last_pack_size, 0);
        //  send_data_to_tcp(REMOTE_SERVER, REMOTE_PORT, sendline, last_pack_size);
    }

    // 统计帧率
    static uint32_t bytes_sent = 0;
    bytes_sent = bytes_sent + buf_size;

    if (tv_now.tv_sec - last_sec_tv.tv_sec >= 1)
    {
        printf("bytes total: %d\n", bytes_sent);
        bytes_sent = 0;
        last_sec_tv = tv_now;
    }
}

// 发送数据给udp服务器
void send_data_to_udp(char *server_ip, int port, const char *data, int data_len)
{

    // 创建UDP套接字
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    // 设置服务器地址结构
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(port);

    // 发送数据
    sendto(sockfd, data, data_len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

void send_data_to_self_by_udp(int buf_size, char *buf_data)
{
    gettimeofday(&tv_now, NULL);
    // 创建UDP套接字
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    // 设置服务器地址结构
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(9999);

    // 使用udp发送数据到remote server
    // char sendline[buf_size];
    char *sendline = NULL;
    int n;
    int i = 0;
    // int pack_size = UDP_PACK_SIZE;
    int pack_num = buf_size / UDP_PACK_SIZE;
    int last_pack_size = buf_size % UDP_PACK_SIZE;
    int total_bytes = pack_num * UDP_PACK_SIZE + last_pack_size;

    // 分包发送所有数据
    for (i = 0; i < pack_num; i++)
    {
        sendline = buf_data + i * UDP_PACK_SIZE;
        sendto(sockfd, sendline, UDP_PACK_SIZE, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        // send(sockfd, sendline, UDP_PACK_SIZE, 0);
        // send_data_to_tcp(REMOTE_SERVER, REMOTE_PORT, sendline, UDP_PACK_SIZE);
        usleep(1000);
    }

    if (last_pack_size > 0)
    {
        sendline = buf_data + pack_num * UDP_PACK_SIZE;
        // send(sockfd, sendline, last_pack_size, 0);
        sendto(sockfd, sendline, last_pack_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        //  send_data_to_tcp(REMOTE_SERVER, REMOTE_PORT, sendline, last_pack_size);
    }

    // 统计帧率
    static uint32_t bytes_sent = 0;
    bytes_sent = bytes_sent + buf_size;

    if (tv_now.tv_sec - last_sec_tv.tv_sec >= 1)
    {
        printf("bytes total: %d\n", bytes_sent);
        bytes_sent = 0;
        last_sec_tv = tv_now;
    }

    close(sockfd);
}

// 编码部分
MPP_RET test_mpp_run(MpiEncMultiCtxInfo *info)
{
    MpiEncTestArgs *cmd = info->cmd;
    MpiEncTestData *p = &info->ctx;
    MppApi *mpi = p->mpi;
    MppCtx ctx = p->ctx;
    RK_U32 quiet = cmd->quiet;
    RK_S32 chn = info->chn;
    RK_U32 cap_num = 0;
    DataCrc checkcrc;
    MPP_RET ret = MPP_OK;

    char *pps_data = (char *)malloc(1024);

    int pps_data_len = 0;
    int pps_counter = 0;

    memset(&checkcrc, 0, sizeof(checkcrc));
    checkcrc.sum = mpp_malloc(RK_ULONG, 512);

    // 使用h264编码
    if (p->type == MPP_VIDEO_CodingAVC || p->type == MPP_VIDEO_CodingHEVC)
    {
        MppPacket packet = NULL;

        /*
         * Can use packet with normal malloc buffer as input not pkt_buf.
         * Please refer to vpu_api_legacy.cpp for normal buffer case.
         * Using pkt_buf buffer here is just for simplifing demo.
         * 可使用普通malloc缓冲区的数据包作为输入，而不是pkt_buf。
         * 参考vpu_api_legacy.cpp作为普通缓冲区的情况。
         * 使用pkt_buf缓冲区仅是为了简化演示。
         */
        mpp_packet_init_with_buffer(&packet, p->pkt_buf);
        /* NOTE: It is important to clear output packet length!! */
        // 重要：清除输出包长度！
        mpp_packet_set_length(packet, 0);

        ret = mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet);
        if (ret)
        {
            mpp_err("mpi control enc get extra info failed\n");
            goto RET;
        }
        else
        {
            // 往h264当中写入sps和pps帧
            /* get and write sps/pps for H.264 */
            // void *ptr = mpp_packet_get_pos(packet);
            // size_t len = mpp_packet_get_length(packet);

            // pps_data = (char *)mpp_packet_get_pos(packet);
            void *ptr = mpp_packet_get_pos(packet);
            pps_data_len = mpp_packet_get_length(packet);
            memcpy(pps_data, ptr, pps_data_len);
            // printf("sending data len:%d\n", len);

            // 发送PPS数据
            // send_data_by_tcp(pps_data_len, pps_data);
            // send_data_by_tcp(len, (char *)ptr);
            // send_data_to_self_by_udp(len, (char *)ptr);
            // rtmp_push_raw_data(len, (char *)ptr);

            if (p->fp_output)
            {
                // fwrite
            }
        }

        mpp_packet_deinit(&packet);
    }

    // 线程的主体
    while (!p->pkt_eos)
    {
        MppMeta meta = NULL;
        MppFrame frame = NULL;
        MppPacket packet = NULL;
        void *buf = mpp_buffer_get_ptr(p->frm_buf);
        RK_S32 cam_frm_idx = -1;
        MppBuffer cam_buf = NULL;
        RK_U32 eoi = 1;

        // printf("Running \n");

        // 针对原始yuv/rgb文件的编码
        if (p->fp_input)
        {
            // printf("Input is file\n");
            mpp_buffer_sync_begin(p->frm_buf);
            ret = read_image((RK_U8 *)buf, p->fp_input, p->width, p->height, p->hor_stride, p->ver_stride, p->fmt);
            if (ret == MPP_NOK || feof(p->fp_input))
            {
                p->frm_eos = 1;

                if (p->frame_num < 0 || p->frame_count < p->frame_num)
                {
                    clearerr(p->fp_input);
                    rewind(p->fp_input);
                    p->frm_eos = 0;
                    printf("chn %d loop times %d\n", chn, ++p->loop_times);
                    continue;
                }
                printf("chn %d found last frame. feof %d\n", chn, feof(p->fp_input));
            }
            else if (ret == MPP_ERR_VALUE)
                goto RET;
            mpp_buffer_sync_end(p->frm_buf);
        }
        else
        {
            // 摄像头模式
            // printf("Input is camera\n");
            if (p->cam_ctx == NULL)
            {
                // 读取摄像头帧
                mpp_buffer_sync_begin(p->frm_buf);

                // printf("frame count:%d \n", p->frame_count);

                // 将读取的数据放入缓存
                ret = fill_image((RK_U8 *)buf, p->width, p->height, p->hor_stride, p->ver_stride, p->fmt, p->frame_count);
                if (ret)
                    goto RET;
                mpp_buffer_sync_end(p->frm_buf);
            }
            else
            {

                cam_frm_idx = camera_source_get_frame(p->cam_ctx);
                mpp_assert(cam_frm_idx >= 0);

                /* skip unstable frames */
                if (cap_num++ < 50)
                {
                    camera_source_put_frame(p->cam_ctx, cam_frm_idx);
                    // 直接丢掉
                    continue;
                }

                // printf("cam_frm_idx:%d \n", cam_frm_idx);

                // cam_frm_idx:0
                // cam_frm_idx:1
                // cam_frm_idx:2
                // cam_frm_idx:3

                cam_buf = camera_frame_to_buf(p->cam_ctx, cam_frm_idx);
                mpp_assert(cam_buf);
            }
        }

        // 帧初始化
        ret = mpp_frame_init(&frame);
        if (ret)
        {
            mpp_err_f("mpp_frame_init failed\n");
            goto RET;
        }

        // 根据预设, 分辨数据宽高
        mpp_frame_set_width(frame, p->width);
        mpp_frame_set_height(frame, p->height);
        mpp_frame_set_hor_stride(frame, p->hor_stride);
        mpp_frame_set_ver_stride(frame, p->ver_stride);
        mpp_frame_set_fmt(frame, p->fmt);
        mpp_frame_set_eos(frame, p->frm_eos);

        // 根据来源填充buff
        if (p->fp_input && feof(p->fp_input))
            mpp_frame_set_buffer(frame, NULL);
        else if (cam_buf)
            mpp_frame_set_buffer(frame, cam_buf);
        else
            mpp_frame_set_buffer(frame, p->frm_buf);

        // 取出frame中的meta数据
        meta = mpp_frame_get_meta(frame);
        mpp_packet_init_with_buffer(&packet, p->pkt_buf);
        /* NOTE: It is important to clear output packet length!!
        先清理输出的output packet长度 */
        mpp_packet_set_length(packet, 0);
        mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet);
        mpp_meta_set_buffer(meta, KEY_MOTION_INFO, p->md_info);

// 暂时无用
#if 0
        if (p->osd_enable || p->user_data_enable || p->roi_enable)
        {
            printf("osd_enable or user_data_enable or roi_enable\n");
            if (p->user_data_enable)
            {
                MppEncUserData user_data;
                char *str = "this is user data\n";

                if ((p->frame_count & 10) == 0)
                {
                    user_data.pdata = str;
                    user_data.len = strlen(str) + 1;
                    mpp_meta_set_ptr(meta, KEY_USER_DATA, &user_data);
                }
                static RK_U8 uuid_debug_info[16] = {
                    0x57, 0x68, 0x97, 0x80, 0xe7, 0x0c, 0x4b, 0x65,
                    0xa9, 0x06, 0xae, 0x29, 0x94, 0x11, 0xcd, 0x9a};

                MppEncUserDataSet data_group;
                MppEncUserDataFull datas[2];
                char *str1 = "this is user data 1\n";
                char *str2 = "this is user data 2\n";
                data_group.count = 2;
                datas[0].len = strlen(str1) + 1;
                datas[0].pdata = str1;
                datas[0].uuid = uuid_debug_info;

                datas[1].len = strlen(str2) + 1;
                datas[1].pdata = str2;
                datas[1].uuid = uuid_debug_info;

                data_group.datas = datas;

                mpp_meta_set_ptr(meta, KEY_USER_DATAS, &data_group);
            }

            if (p->osd_enable)
            {
                /* gen and cfg osd plt */
                mpi_enc_gen_osd_plt(&p->osd_plt, p->frame_count);

                p->osd_plt_cfg.change = MPP_ENC_OSD_PLT_CFG_CHANGE_ALL;
                p->osd_plt_cfg.type = MPP_ENC_OSD_PLT_TYPE_USERDEF;
                p->osd_plt_cfg.plt = &p->osd_plt;

                ret = mpi->control(ctx, MPP_ENC_SET_OSD_PLT_CFG, &p->osd_plt_cfg);
                if (ret)
                {
                    mpp_err("mpi control enc set osd plt failed ret %d\n", ret);
                    goto RET;
                }

                /* gen and cfg osd plt */
                mpi_enc_gen_osd_data(&p->osd_data, p->buf_grp, p->width,
                                     p->height, p->frame_count);
                mpp_meta_set_ptr(meta, KEY_OSD_DATA, (void *)&p->osd_data);
            }

            if (p->roi_enable)
            {
                RoiRegionCfg *region = &p->roi_region;

                /* calculated in pixels */
                region->x = MPP_ALIGN(p->width / 8, 16);
                region->y = MPP_ALIGN(p->height / 8, 16);
                region->w = 128;
                region->h = 256;
                region->force_intra = 0;
                region->qp_mode = 1;
                region->qp_val = 24;

                mpp_enc_roi_add_region(p->roi_ctx, region);

                region->x = MPP_ALIGN(p->width / 2, 16);
                region->y = MPP_ALIGN(p->height / 4, 16);
                region->w = 256;
                region->h = 128;
                region->force_intra = 1;
                region->qp_mode = 1;
                region->qp_val = 10;

                mpp_enc_roi_add_region(p->roi_ctx, region);

                /* send roi info by metadata */
                mpp_enc_roi_setup_meta(p->roi_ctx, meta);
            }
        }
#endif

        if (!p->first_frm)
            p->first_frm = mpp_time();
        /*
         * NOTE: in non-block mode the frame can be resent.
         * The default input timeout mode is block.
         *
         * User should release the input frame to meet the requirements of
         * resource creator must be the resource destroyer.
         *
         * 这个编码过程个ffmpeg的编码过程差不多, 异步形式将frame放入编码器硬件
         */
        ret = mpi->encode_put_frame(ctx, frame);
        if (ret)
        {
            mpp_err("chn %d encode put frame failed\n", chn);
            mpp_frame_deinit(&frame);
            goto RET;
        }

        // 销毁frame
        mpp_frame_deinit(&frame);

        // 等待直到获取输出packet
        do
        {
            ret = mpi->encode_get_packet(ctx, &packet);
            if (ret)
            {
                mpp_err("chn %d encode get packet failed\n", chn);
                goto RET;
            }

            mpp_assert(packet);

            if (packet)
            {
                // write packet to file here
                void *ptr = mpp_packet_get_pos(packet);
                size_t len = mpp_packet_get_length(packet);
                char log_buf[256];
                RK_S32 log_size = sizeof(log_buf) - 1;
                RK_S32 log_len = 0;

                if (!p->first_pkt)
                    p->first_pkt = mpp_time();

                p->pkt_eos = mpp_packet_get_eos(packet);

                // printf("Packet data len:%d\n", len);

                // 数据发送点
                // rtmp_init((char *)packet, len);
                // push_rtmp((char *)packet, len);
                // send_data_to_self_by_udp(len, (char *)ptr);
                // rtmp_push_raw_data(len, (char *)ptr);
                static int write_counter = 0;
                pps_counter++;
                if (pps_counter == 30)
                {
                    pps_counter = 0;
                    // send_data_by_tcp(pps_data_len, pps_data);
                }

                if (pps_counter == 0)
                {
                    // send_data_by_tcp(pps_data_len, pps_data);
                }

                // send_data_by_tcp(len, (char *)ptr);

                if (p->fp_output)
                {
                    if (write_counter == 0)
                    {
                        // printf("Write SPS/PPS\n");
                        // fwrite(pps_data, 1, pps_data_len, p->fp_output);
                        // fwrite(ptr, 1, len, p->fp_output);
                        init_rtmp_connection("sample.h264", "rtmp://159.75.182.56/live/livestream?secret=334a72b548e8443fa51531391bfe2a2f", 1920, 1080, 30);

                        set_start_time();
                        // fwrite(pps_data, 1, pps_data_len, p->fp_output);
                        send_to_rtmp_server((uint8_t *)pps_data, pps_data_len, 33333);

                        write_counter = 1;
                    }

                    char *h264_data = (char *)ptr;
                    // if (h264_data[0] != 0 || h264_data[1] != 0 || h264_data[2] != 0 || h264_data[3] != 1)
                    // {
                    //     printf("----------\n");
                    // }
                    // printf("Type: 0x%02x\n", h264_data[4]);

                    for (int i = 0; i < 10; i++)
                    {
                        // printf("%02x ", h264_data[i]);
                    }

                    // printf("\n");

                    // 写入文件
                    // fwrite(ptr, 1, len, p->fp_output);
                    // get_time_gap();
                    send_to_rtmp_server((uint8_t *)ptr, len, 33333);
                }

                if (p->fp_verify && !p->pkt_eos)
                {
                    calc_data_crc((RK_U8 *)ptr, (RK_U32)len, &checkcrc);
                    printf("p->frame_count=%d, len=%d\n", p->frame_count, len);
                    write_data_crc(p->fp_verify, &checkcrc);
                }

                log_len += snprintf(log_buf + log_len, log_size - log_len, "encoded frame %-4d", p->frame_count);

                /* for low delay partition encoding */
                if (mpp_packet_is_partition(packet))
                {

                    eoi = mpp_packet_is_eoi(packet);

                    log_len += snprintf(log_buf + log_len, log_size - log_len, " pkt %d", p->frm_pkt_cnt);
                    p->frm_pkt_cnt = (eoi) ? (0) : (p->frm_pkt_cnt + 1);
                }

                log_len += snprintf(log_buf + log_len, log_size - log_len, " size %-7zu", len);

                if (mpp_packet_has_meta(packet))
                {
                    meta = mpp_packet_get_meta(packet);
                    RK_S32 temporal_id = 0;
                    RK_S32 lt_idx = -1;
                    RK_S32 avg_qp = -1;

                    if (MPP_OK == mpp_meta_get_s32(meta, KEY_TEMPORAL_ID, &temporal_id))
                        log_len += snprintf(log_buf + log_len, log_size - log_len, " tid %d", temporal_id);

                    if (MPP_OK == mpp_meta_get_s32(meta, KEY_LONG_REF_IDX, &lt_idx))
                        log_len += snprintf(log_buf + log_len, log_size - log_len, " lt %d", lt_idx);

                    if (MPP_OK == mpp_meta_get_s32(meta, KEY_ENC_AVERAGE_QP, &avg_qp))
                        log_len += snprintf(log_buf + log_len, log_size - log_len, " qp %d", avg_qp);
                }

                // printf("chn %d %s\n", chn, log_buf);

                mpp_packet_deinit(&packet);
                fps_calc_inc(cmd->fps);

                p->stream_size += len;
                p->frame_count += eoi;

                if (p->pkt_eos)
                {
                    printf("chn %d found last packet\n", chn);
                    mpp_assert(p->frm_eos);
                }
            }
        } while (!eoi);

        if (cam_frm_idx >= 0)
        {
            camera_source_put_frame(p->cam_ctx, cam_frm_idx);
        }

        if (p->frame_num > 0 && p->frame_count >= p->frame_num)
            break;

        if (p->loop_end)
            break;

        if (p->frm_eos && p->pkt_eos)
            break;
    }
RET:
    MPP_FREE(checkcrc.sum);

    return ret;
}

// 正式编码
void *enc_test(void *arg)
{
    MpiEncMultiCtxInfo *info = (MpiEncMultiCtxInfo *)arg;
    MpiEncTestArgs *cmd = info->cmd;
    MpiEncTestData *p = &info->ctx;
    MpiEncMultiCtxRet *enc_ret = &info->ret;
    MppPollType timeout = MPP_POLL_BLOCK;
    RK_U32 quiet = cmd->quiet;
    MPP_RET ret = MPP_OK;
    RK_S64 t_s = 0;
    RK_S64 t_e = 0;

    printf("%s start\n", info->name);

    // 会话创建
    ret = test_ctx_init(info);
    if (ret)
    {
        mpp_err_f("test data init failed ret %d\n", ret);
        goto MPP_TEST_OUT;
    }
    printf("ctx init\n");

    // 获取缓存组
    ret = mpp_buffer_group_get_internal(&p->buf_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
    if (ret)
    {
        printf("failed to get mpp buffer group ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    printf("Get internal buffer group\n");

    // 分配帧缓存
    ret = mpp_buffer_get(p->buf_grp, &p->frm_buf, p->frame_size + p->header_size);
    if (ret)
    {
        mpp_err_f("failed to get buffer for input frame ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    // 分配包缓存
    ret = mpp_buffer_get(p->buf_grp, &p->pkt_buf, p->frame_size);
    if (ret)
    {
        mpp_err_f("failed to get buffer for output packet ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    // 分配运动信息缓存
    ret = mpp_buffer_get(p->buf_grp, &p->md_info, p->mdinfo_size);
    if (ret)
    {
        mpp_err_f("failed to get buffer for motion info output packet ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    // 类似创建对象, 这里就是会话, 因为c没有对象
    ret = mpp_create(&p->ctx, &p->mpi);
    if (ret)
    {
        mpp_err("mpp_create failed ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    printf("encoder test start w %d h %d type %d\n", p->width, p->height, p->type);

    // 设置输出超时时间
    ret = p->mpi->control(p->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    if (MPP_OK != ret)
    {
        mpp_err("mpi control set output timeout %d ret %d\n", timeout, ret);
        goto MPP_TEST_OUT;
    }

    // 利用参数对mpp初始化
    ret = mpp_init(p->ctx, MPP_CTX_ENC, p->type);
    if (ret)
    {
        mpp_err("mpp_init failed ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    // 初始化编码配置
    ret = mpp_enc_cfg_init(&p->cfg);
    if (ret)
    {
        mpp_err_f("mpp_enc_cfg_init failed ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    // 获取编码配置
    ret = p->mpi->control(p->ctx, MPP_ENC_GET_CFG, p->cfg);
    if (ret)
    {
        mpp_err_f("get enc cfg failed ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    // 编码器配置setup
    ret = test_mpp_enc_cfg_setup(info);
    if (ret)
    {
        mpp_err_f("test mpp setup failed ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    // 拿一个时间戳, 用于统计
    t_s = mpp_time();

    // 运行编码过程(核心)
    ret = test_mpp_run(info);

    // 获取结束时间
    t_e = mpp_time();
    if (ret)
    {
        mpp_err_f("test mpp run failed ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    ret = p->mpi->reset(p->ctx);
    if (ret)
    {
        mpp_err("mpi->reset failed\n");
        goto MPP_TEST_OUT;
    }

    // 统计用
    enc_ret->elapsed_time = t_e - t_s;
    enc_ret->frame_count = p->frame_count;
    enc_ret->stream_size = p->stream_size;
    enc_ret->frame_rate = (float)p->frame_count * 1000000 / enc_ret->elapsed_time;
    enc_ret->bit_rate = (p->stream_size * 8 * (p->fps_out_num / p->fps_out_den)) / p->frame_count;
    enc_ret->delay = p->first_pkt - p->first_frm;

MPP_TEST_OUT:
    if (p->ctx)
    {
        mpp_destroy(p->ctx);
        p->ctx = NULL;
    }

    if (p->cfg)
    {
        mpp_enc_cfg_deinit(p->cfg);
        p->cfg = NULL;
    }

    if (p->frm_buf)
    {
        mpp_buffer_put(p->frm_buf);
        p->frm_buf = NULL;
    }

    if (p->pkt_buf)
    {
        mpp_buffer_put(p->pkt_buf);
        p->pkt_buf = NULL;
    }

    if (p->md_info)
    {
        mpp_buffer_put(p->md_info);
        p->md_info = NULL;
    }

    if (p->osd_data.buf)
    {
        mpp_buffer_put(p->osd_data.buf);
        p->osd_data.buf = NULL;
    }

    if (p->buf_grp)
    {
        mpp_buffer_group_put(p->buf_grp);
        p->buf_grp = NULL;
    }

    if (p->roi_ctx)
    {
        mpp_enc_roi_deinit(p->roi_ctx);
        p->roi_ctx = NULL;
    }

    test_ctx_deinit(p);

    return NULL;
}

// 使用多线程进行编码
int enc_test_multi(MpiEncTestArgs *cmd, const char *name)
{
    MpiEncMultiCtxInfo *ctxs = NULL;
    float total_rate = 0.0;
    RK_S32 ret = MPP_NOK;
    RK_S32 i = 0;

    ctxs = mpp_calloc(MpiEncMultiCtxInfo, cmd->nthreads);
    if (NULL == ctxs)
    {
        mpp_err("failed to alloc context for instances\n");
        return -1;
    }

    ctxs[0].cmd = cmd;
    ctxs[0].name = name;
    ctxs[0].chn = 0;
    printf("ctxs[%d].chn = %d\n", 0, ctxs[0].chn);
    ret = pthread_create(&ctxs[0].thd, NULL, enc_test, &ctxs[0]);
    if (ret)
    {
        mpp_err("failed to create thread %d\n", 0);
        return ret;
    }

    printf("cmd->frame_num :%d\n", cmd->frame_num);

    if (cmd->frame_num < 0)
    {
        // wait for input then quit encoding
        printf("*******************************************\n");
        printf("**** Press Enter to stop loop encoding ****\n");
        printf("*******************************************\n");

        getc(stdin);
        for (i = 0; i < cmd->nthreads; i++)
            ctxs[i].ctx.loop_end = 1;
    }

    for (i = 0; i < cmd->nthreads; i++)
        pthread_join(ctxs[i].thd, NULL);

    for (i = 0; i < cmd->nthreads; i++)
    {
        MpiEncMultiCtxRet *enc_ret = &ctxs[i].ret;

        printf("chn %d encode %d frames time %lld ms delay %3d ms fps %3.2f bps %lld\n",
               i, enc_ret->frame_count, (RK_S64)(enc_ret->elapsed_time / 1000),
               (RK_S32)(enc_ret->delay / 1000), enc_ret->frame_rate, enc_ret->bit_rate);

        total_rate += enc_ret->frame_rate;
    }

    MPP_FREE(ctxs);

    total_rate /= cmd->nthreads;
    printf("%s average frame rate %.2f\n", name, total_rate);

    return ret;
}

int main(void)
{

    int argc = 21;
    char *argv[21];

    argv[0] = "vi_3568_rtmp";
    argv[1] = "-i";
    argv[2] = "/dev/video0";
    argv[3] = "-w";
    argv[4] = "800";
    argv[5] = "-h";
    argv[6] = "600";
    argv[7] = "-o";
    argv[8] = "sample_720.h264";
    argv[9] = "-t";
    argv[10] = "7";
    argv[11] = "-n";
    argv[12] = "-1";
    argv[13] = "-fps";
    argv[14] = "30";
    argv[15] = "-v";
    argv[16] = "f";
    argv[17] = "-g";
    argv[18] = "30";
    argv[19] = "-rc";
    argv[20] = "1";
    // argv[19] = "-bps";
    // argv[20] = "2048";

    int i = 0;
    for (i = 0; i < argc; i++)
    {
        printf("[%d]: %s\n", i, argv[i]);
    }

    // rtmp_init();

    // if (init_tcp_connection() < 0)
    // {
    //     printf("Please turn on the server first\n");
    //     return -1;
    // }
    // init_rtmp_connection();

    RK_S32 ret = MPP_NOK;
    MpiEncTestArgs *cmd = mpi_enc_test_cmd_get();

    // parse the cmd option
    ret = mpi_enc_test_cmd_update_by_args(cmd, argc, argv);
    if (ret)
        goto DONE;

    mpi_enc_test_cmd_show_opt(cmd);

    ret = enc_test_multi(cmd, argv[0]);

DONE:
    mpi_enc_test_cmd_put(cmd);
    //  clean_rtmp();
    // clean_rtmp();
    return ret;
}
