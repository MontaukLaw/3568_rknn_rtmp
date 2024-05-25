extern "C"
{
#include "rk_mpi.h"
#include "rk_venc_ref.h"
#include "mpp_meta.h"
#include "rk_venc_cmd.h"
#include "mpp_packet.h"
};

#include "MyEncoder.h"
#include <stdio.h>
#include <fstream>

void MyEncoder::init()
{
    MPP_RET ret = MPP_OK;

    MppPollType timeout = MPP_POLL_BLOCK;

    printf("mpi_enc_test start\n");

    ret = enc_ctx_init(&p, &args_);
    if (ret)
    {
        printf("test data init failed ret %d\n", ret);
        deinit();
    }
    ret = mpp_buffer_group_get_internal(&p->buf_grp, MPP_BUFFER_TYPE_DRM);
    if (ret)
    {
        printf("failed to get mpp buffer group ret %d\n", ret);
        deinit();
    }

    ret = mpp_buffer_get(p->buf_grp, &p->frm_buf, p->frame_size + p->header_size);
    if (ret)
    {
        printf("failed to get buffer for input frame ret %d\n", ret);
        deinit();
    }

    ret = mpp_buffer_get(p->buf_grp, &p->pkt_buf, p->frame_size);
    if (ret)
    {
        printf("failed to get buffer for input osd index ret %d\n", ret);
        deinit();
    }

    printf("mpi_enc_test encoder test start w %d h %d type %d\n", p->width,
           p->height, p->type);

    // encoder demo
    ret = mpp_create(&p->ctx, &p->mpi);
    if (ret)
    {
        printf("mpp_create failed ret %d\n", ret);
        deinit();
    }

    ret = p->mpi->control(p->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    if (MPP_OK != ret)
    {
        printf("mpi control set output timeout %d ret %d\n", timeout, ret);
        deinit();
    }

    ret = mpp_init(p->ctx, MPP_CTX_ENC, p->type);
    if (ret)
    {
        printf("mpp_init failed ret %d\n", ret);
        deinit();
    }

    ret = mpp_enc_cfg_init(&p->cfg);
    if (ret)
    {
        printf("mpp_enc_cfg_init failed ret %d\n", ret);
        deinit();
    }

    ret = test_mpp_enc_cfg_setup(p);
    if (ret)
    {
        printf("test mpp setup failed ret %d\n", ret);
        deinit();
    }
}

// void MyEncoder::deinit()
// {
//     printf("deinit\n");
// }

void MyEncoder::deinit()
{
    p->mpi->reset(p->ctx);
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

    enc_ctx_deinit(&p);
}

MPP_RET MyEncoder::enc_ctx_deinit(MpiEncData **data)
{
    MpiEncData *p = NULL;

    if (!data)
    {
        printf("invalid input data %p\n", data);
        return MPP_ERR_NULL_PTR;
    }

    p = *data;
    if (p)
    {
        //        if (p->fp_input) {
        //            fclose(p->fp_input);
        //            p->fp_input = NULL;
        //        }
        //        if (p->fp_output) {
        //            fclose(p->fp_output);
        //            p->fp_output = NULL;
        //        }
        MPP_FREE(p);
        *data = NULL;
    }

    return MPP_OK;
}

MPP_RET MyEncoder::enc_ctx_init(MpiEncData **data, MpiEncArgs *cmd)
{
    MpiEncData *p = NULL;
    MPP_RET ret = MPP_OK;

    if (!data || !cmd)
    {
        printf("invalid input data %p cmd %p\n", data, cmd);
        return MPP_ERR_NULL_PTR;
    }

    p = mpp_calloc(MpiEncData, 1);
    if (!p)
    {
        printf("create MpiEncTestData failed\n");
        ret = MPP_ERR_MALLOC;
        *data = p;
        return ret;
    }

    // get paramter from cmd
    p->width = cmd->width;
    p->height = cmd->height;
    p->hor_stride = (cmd->hor_stride) ? (cmd->hor_stride) : (MPP_ALIGN(cmd->width, 16));
    p->ver_stride = (cmd->ver_stride) ? (cmd->ver_stride) : (MPP_ALIGN(cmd->height, 16));
    p->fmt = cmd->format;
    p->type = cmd->type;
    p->bps = cmd->bps_target;
    p->bps_min = cmd->bps_min;
    p->bps_max = cmd->bps_max;
    p->rc_mode = cmd->rc_mode;
    p->num_frames = cmd->num_frames;
    if (cmd->type == MPP_VIDEO_CodingMJPEG && p->num_frames == 0)
    {
        printf("jpege default encode only one frame. Use -n [num] for rc case\n");
        p->num_frames = 1;
    }
    p->gop_mode = cmd->gop_mode;
    p->gop_len = cmd->gop_len;
    p->vi_len = cmd->vi_len;

    p->fps_in_flex = cmd->fps_in_flex;
    p->fps_in_den = cmd->fps_in_den;
    p->fps_in_num = cmd->fps_in_num;
    p->fps_out_flex = cmd->fps_out_flex;
    p->fps_out_den = cmd->fps_out_den;
    p->fps_out_num = cmd->fps_out_num;

    // update resource parameter
    switch (p->fmt & MPP_FRAME_FMT_MASK)
    {
    case MPP_FMT_YUV420SP:
    case MPP_FMT_YUV420P:
    {
        p->frame_size = MPP_ALIGN(p->hor_stride, 64) * MPP_ALIGN(p->ver_stride, 64) * 3 / 2;
    }
    break;

    case MPP_FMT_YUV422_YUYV:
    case MPP_FMT_YUV422_YVYU:
    case MPP_FMT_YUV422_UYVY:
    case MPP_FMT_YUV422_VYUY:
    case MPP_FMT_YUV422P:
    case MPP_FMT_YUV422SP:
    case MPP_FMT_RGB444:
    case MPP_FMT_BGR444:
    case MPP_FMT_RGB555:
    case MPP_FMT_BGR555:
    case MPP_FMT_RGB565:
    case MPP_FMT_BGR565:
    {
        p->frame_size = MPP_ALIGN(p->hor_stride, 64) * MPP_ALIGN(p->ver_stride, 64) * 2;
    }
    break;

    default:
    {
        p->frame_size = MPP_ALIGN(p->hor_stride, 64) * MPP_ALIGN(p->ver_stride, 64) * 4;
    }
    break;
    }

    if (MPP_FRAME_FMT_IS_FBC(p->fmt))
        p->header_size = MPP_ALIGN(MPP_ALIGN(p->width, 16) * MPP_ALIGN(p->height, 16) / 16, SZ_4K);
    else
        p->header_size = 0;

    // /*
    //  * osd idx size range from 16x16 bytes(pixels) to
    //  * hor_stride*ver_stride(bytes). for general use, 1/8 Y buffer is enough.
    //  */
    // p->osd_idx_size = p->hor_stride * p->ver_stride / 8;
    // p->plt_table[0] = MPP_ENC_OSD_PLT_RED;
    // p->plt_table[1] = MPP_ENC_OSD_PLT_YELLOW;
    // p->plt_table[2] = MPP_ENC_OSD_PLT_BLUE;
    // p->plt_table[3] = MPP_ENC_OSD_PLT_GREEN;
    // p->plt_table[4] = MPP_ENC_OSD_PLT_CYAN;
    // p->plt_table[5] = MPP_ENC_OSD_PLT_TRANS;
    // p->plt_table[6] = MPP_ENC_OSD_PLT_BLACK;
    // p->plt_table[7] = MPP_ENC_OSD_PLT_WHITE;

    *data = p;
    return ret;
}

MPP_RET MyEncoder::test_mpp_enc_cfg_setup(MpiEncData *p)
{
    MPP_RET ret;
    MppApi *mpi;
    MppCtx ctx;
    MppEncCfg cfg;
    MppEncRcMode rc_mode = MPP_ENC_RC_MODE_AVBR;

    if (NULL == p)
        return MPP_ERR_NULL_PTR;

    mpi = p->mpi;
    ctx = p->ctx;
    cfg = p->cfg;

    /* setup default parameter */
    if (p->fps_in_den == 0)
        p->fps_in_den = 1;
    if (p->fps_in_num == 0)
        p->fps_in_num = 30;
    if (p->fps_out_den == 0)
        p->fps_out_den = 1;
    if (p->fps_out_num == 0)
        p->fps_out_num = 30;
    // p->gop = 60;

    if (!p->bps)
        p->bps = p->width * p->height / 8 * (p->fps_out_num / p->fps_out_den);

    mpp_enc_cfg_set_s32(cfg, "prep:width", p->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", p->height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", p->hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", p->ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", p->fmt);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", rc_mode);

    /* fix input / output frame rate */
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", p->fps_in_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", p->fps_in_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", p->fps_in_den);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", p->fps_out_flex);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", p->fps_out_num);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", p->fps_out_den);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", p->gop_len ? p->gop_len : p->fps_out_num * 2);

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
        printf("FIXQp\n");
        /* do not setup bitrate on FIXQP mode */
    }
    break;
    case MPP_ENC_RC_MODE_CBR:
    {
        printf("CBR\n");
        /* CBR mode has narrow bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps_max ? p->bps_max : p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps_min ? p->bps_min : p->bps * 15 / 16);
    }
    break;
    case MPP_ENC_RC_MODE_VBR:
    case MPP_ENC_RC_MODE_AVBR:
    {
        printf("AVBR \n");
        /* VBR mode has wide bound */
        mpp_enc_cfg_set_s32(cfg, "rc:bps_max", p->bps_max ? p->bps_max : p->bps * 17 / 16);
        mpp_enc_cfg_set_s32(cfg, "rc:bps_min", p->bps_min ? p->bps_min : p->bps * 1 / 16);
    }
    break;
    default:
    {
        /* default use CBR mode */
        printf("default");
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
            mpp_enc_cfg_set_s32(cfg, "rc:qp_init", 20);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max", 20);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min", 20);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", 20);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", 20);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 2);
        }
        break;
        case MPP_ENC_RC_MODE_CBR:
        case MPP_ENC_RC_MODE_VBR:
        case MPP_ENC_RC_MODE_AVBR:
        {
            mpp_enc_cfg_set_s32(cfg, "rc:qp_init", 26);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max", 51);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min", 10);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", 51);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", 10);
            mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 2);
        }
        break;
        default:
        {
            printf("unsupport encoder rc mode %d\n", p->rc_mode);
        }
        break;
        }
    }
    break;
    case MPP_VIDEO_CodingVP8:
    {
        /* vp8 only setup base qp range */
        mpp_enc_cfg_set_s32(cfg, "rc:qp_init", 40);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_max", 127);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_min", 0);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_max_i", 127);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_min_i", 0);
        mpp_enc_cfg_set_s32(cfg, "rc:qp_ip", 6);
    }
    break;
    case MPP_VIDEO_CodingMJPEG:
    {
        /* jpeg use special codec config to control qtable */
        mpp_enc_cfg_set_s32(cfg, "jpeg:q_factor", 80);
        mpp_enc_cfg_set_s32(cfg, "jpeg:qf_max", 99);
        mpp_enc_cfg_set_s32(cfg, "jpeg:qf_min", 1);
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
        printf("unsupport encoder coding type %d\n", p->type);
    }
    break;
    }

    p->split_mode = 0;
    p->split_arg = 0;

    mpp_env_get_u32("split_mode", &p->split_mode, MPP_ENC_SPLIT_NONE);
    mpp_env_get_u32("split_arg", &p->split_arg, 0);

    if (p->split_mode)
    {
        printf("%p split_mode %d split_arg %d\n", ctx, p->split_mode, p->split_arg);
        mpp_enc_cfg_set_s32(cfg, "split:mode", p->split_mode);
        mpp_enc_cfg_set_s32(cfg, "split:arg", p->split_arg);
    }

    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret)
    {
        printf("mpi control enc set cfg failed ret %d\n", ret);
        return ret;
    }

    /* optional */
    p->sei_mode = MPP_ENC_SEI_MODE_ONE_FRAME;
    ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &p->sei_mode);
    if (ret)
    {
        printf("mpi control enc set sei cfg failed ret %d\n", ret);
        return ret;
    }

    if (p->type == MPP_VIDEO_CodingAVC || p->type == MPP_VIDEO_CodingHEVC)
    {
        p->header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        ret = mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &p->header_mode);
        if (ret)
        {
            printf("mpi control enc set header mode failed ret %d\n", ret);
            return ret;
        }
    }

    RK_U32 gop_mode = p->gop_mode;

    mpp_env_get_u32("gop_mode", &gop_mode, gop_mode);
    if (gop_mode)
    {
        MppEncRefCfg ref;

        mpp_enc_ref_cfg_init(&ref);

        if (p->gop_mode < 4)
            mpi_enc_gen_ref_cfg(ref, gop_mode);
        else
            mpi_enc_gen_smart_gop_ref_cfg(ref, p->gop_len, p->vi_len);

        ret = mpi->control(ctx, MPP_ENC_SET_REF_CFG, ref);
        if (ret)
        {
            printf("mpi control enc set ref cfg failed ret %d\n", ret);
            return ret;
        }
        mpp_enc_ref_cfg_deinit(&ref);
    }

    /* setup test mode by env */
    mpp_env_get_u32("osd_enable", &p->osd_enable, 0);
    mpp_env_get_u32("osd_mode", &p->osd_mode, MPP_ENC_OSD_PLT_TYPE_DEFAULT);
    mpp_env_get_u32("roi_enable", &p->roi_enable, 0);
    mpp_env_get_u32("user_data_enable", &p->user_data_enable, 0);

    // RET:
    return ret;
}

void MyEncoder::setUp(int width, int height, int fps)
{
    memset(&args_, 0, sizeof(MpiEncArgs));

    // 计算idx是否到了gop数量，如果到了则添加一个关键帧头信息
    countIdx_ = 0;

    left = 0;
    right = 0;
    bottom = 0;
    top = 0;

    // 设置的输入帧的格式信息 yuv 420p  即 I420  yyyyyyyyyyyyuuuvvv planer 结构
    args_.format = MPP_FMT_BGR888; // MPP_FMT_RGB888; // MPP_FMT_YUV420P;

    // 设置264编码格式， AVC
    args_.type = MPP_VIDEO_CodingAVC;

    args_.fps_in_num = fps;
    args_.fps_out_num = fps;
    args_.gop_len = fps;
    args_.width = width;
    args_.height = height;
    args_.hor_stride = mpi_enc_width_default_stride(args_.width, args_.format);
    args_.ver_stride = args_.height;

    packet = NULL;
    std::cout << "in setUp" << std::endl;

    init();
}


MPP_RET MyEncoder::WriteHeadInfo(char *dst, int *length)
{
	MPP_RET ret = MPP_OK;
	MppApi *mpi;
	MppCtx ctx;

	if (NULL == p)
		return MPP_ERR_NULL_PTR;

	mpi = p->mpi;
	ctx = p->ctx;

	//
	if (p->type == MPP_VIDEO_CodingAVC || p->type == MPP_VIDEO_CodingHEVC)
	{
		packet = NULL;
		ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &packet);
		if (ret)
		{
			printf("mpi control enc get extra info failed\n");
			return ret;
		}
		std::cout << "in write head 1" << std::endl;

		/* get and write sps/pps for H.264 */
		if (packet)
		{
			void *ptr = mpp_packet_get_pos(packet);
			*length = mpp_packet_get_length(packet);

			memcpy(dst, ptr, *length);

			packet = NULL;
			std::cout << "in write head 2" << std::endl;
		}
	}
	return ret;
}

MPP_RET MyEncoder::WriteHeadInfo(FILE *fp)
{
	MPP_RET ret = MPP_OK;
	MppApi *mpi;
	MppCtx ctx;

	if (NULL == p)
		return MPP_ERR_NULL_PTR;

	mpi = p->mpi;
	ctx = p->ctx;

	//
	if (p->type == MPP_VIDEO_CodingAVC || p->type == MPP_VIDEO_CodingHEVC)
	{
		MppPacket m_packet = NULL;

		/*
		 * Can use packet with normal malloc buffer as input not pkt_buf.
		 * Please refer to vpu_api_legacy.cpp for normal buffer case.
		 * Using pkt_buf buffer here is just for simplifing demo.
		 */
		mpp_packet_init_with_buffer(&m_packet, p->pkt_buf);
		/* NOTE: It is important to clear output packet length!! */
		mpp_packet_set_length(m_packet, 0);

		ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &m_packet);
		if (ret)
		{
			printf("mpi control enc get extra info failed\n");
			return ret;
		}
		else
		{
			/* get and write sps/pps for H.264 */
			void *ptr = mpp_packet_get_pos(m_packet);
			int len = mpp_packet_get_length(m_packet);

			fwrite(ptr, len, 1, fp);

			m_packet = NULL;
		}
		mpp_packet_deinit(&m_packet);
	}
	return ret;
}


MPP_RET MyEncoder::encode(const void *img, int img_len, char *dst, int *length)
{
    MPP_RET ret = MPP_OK;
    MppApi *mpi;
    MppCtx ctx;

    char *tmpP = dst;
    *length = 0;
    // for test group
    // mpp默认设置为IPPPPP帧的模式， p->gop 为60帧
    // 即每隔60帧写一次头信息
    // std::cout << " p->gop is:" << p->gop << std::endl;
    if (0 == (countIdx_ % 30))
    {
        countIdx_ = 0;

        int len = 0;

        WriteHeadInfo(tmpP, &len);
        tmpP += len;
        *length += len;
    }

    countIdx_++;

    if (NULL == p)
        return MPP_ERR_NULL_PTR;

    mpi = p->mpi;
    ctx = p->ctx;

    MppFrame frame = NULL;
    packet = NULL;
    MppMeta meta = NULL;
    RK_U32 eoi = 1;

    void *buf = mpp_buffer_get_ptr(p->frm_buf);
    memcpy(buf, img, img_len);
    ret = mpp_frame_init(&frame);
    if (ret)
    {
        printf("mpp_frame_init failed\n");
        return ret;
    }
    mpp_frame_set_width(frame, p->width);
    mpp_frame_set_height(frame, p->height);
    mpp_frame_set_hor_stride(frame, p->hor_stride);
    mpp_frame_set_ver_stride(frame, p->ver_stride);
    mpp_frame_set_fmt(frame, p->fmt);
    mpp_frame_set_eos(frame, p->frm_eos);

    mpp_frame_set_buffer(frame, p->frm_buf);
    meta = mpp_frame_get_meta(frame);
    mpp_packet_init_with_buffer(&packet, p->pkt_buf);
    /* NOTE: It is important to clear output packet length!! */
    mpp_packet_set_length(packet, 0);
    mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet);

    /*
     * NOTE: in non-block mode the frame can be resent.
     * The default input timeout mode is block.
     *
     * User should release the input frame to meet the requirements of
     * resource creator must be the resource destroyer.
     */
    ret = mpi->encode_put_frame(ctx, frame);
    if (ret)
    {
        printf("mpp encode put frame failed\n");
        mpp_frame_deinit(&frame);
        return ret;
    }
    mpp_frame_deinit(&frame);
    do
    {
        ret = mpi->encode_get_packet(ctx, &packet);
        if (ret)
        {
            printf("mpp encode get packet failed\n");
            return ret;
        }

        //    mpp_assert(packet);

        if (packet)
        {
            // write packet to file here
            void *ptr = mpp_packet_get_pos(packet);
            size_t len = mpp_packet_get_length(packet);

            p->pkt_eos = mpp_packet_get_eos(packet);
            memcpy(tmpP, ptr, len);
            *length += len;

            /* for low delay partition encoding */
            if (mpp_packet_is_partition(packet))
            {
                eoi = mpp_packet_is_eoi(packet);
                printf(" pkt %d", p->frm_pkt_cnt);
                p->frm_pkt_cnt = (eoi) ? (0) : (p->frm_pkt_cnt + 1);
            }

            if (mpp_packet_has_meta(packet))
            {
                meta = mpp_packet_get_meta(packet);
                RK_S32 temporal_id = 0;
                RK_S32 lt_idx = -1;
                RK_S32 avg_qp = -1;
                if (MPP_OK == mpp_meta_get_s32(meta, KEY_TEMPORAL_ID, &temporal_id))
                    printf(" tid %d", temporal_id);
                if (MPP_OK == mpp_meta_get_s32(meta, KEY_LONG_REF_IDX, &lt_idx))
                    printf(" lt %d", lt_idx);
                if (MPP_OK == mpp_meta_get_s32(meta, KEY_ENC_AVERAGE_QP, &avg_qp)){
                    // printf(" qp %d", avg_qp);
                }
            }
            mpp_packet_deinit(&packet);
            p->stream_size += len;
            p->frame_count += eoi;
            if (p->pkt_eos)
            {
                printf("%p found last packet\n", ctx);
            }
        }
    } while (!eoi);

    return ret;
}
