#include "simple_rtmp_pusher.h"

static AVFormatContext *ifmt_ctx = NULL;
static AVFormatContext *ofmt_ctx = NULL;
static AVOutputFormat *ofmt = NULL;
static int64_t start_time = 0;
static long frame_index = 0;

static AVPacket pkt;
static int first_pack = 0;

void destropy_ctx(void)
{
    avformat_close_input(&ifmt_ctx);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
    {
        avio_close(ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);
}

void init_rtmp_connection(string h264_file_name, string rtmp_url, int width, int height, int fps)
{

    // AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;
    int videoindex = -1;

    in_filename = h264_file_name.c_str();
    // in_filename = "test10.h264";
    // out_filename = "rtmp://43.139.145.196/live/livestream?secret=e8c13b9687ec47f989d80f08b763fdbf";
    out_filename = rtmp_url.c_str();
    // out_filename = "rtmp://159.75.182.56/live/livestream?secret=334a72b548e8443fa51531391bfe2a2f";
    // printf("%s\n", in_filename);

    av_register_all();

    // printf("%s\n", out_filename);
    // Network
    avformat_network_init();

    // Input
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0)
    {
        printf("Could not open input file.");
        destropy_ctx();
        return;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0)
    {
        printf("Failed to retrieve input stream information");
        destropy_ctx();
        return;
    }

    for (i = 0; i < ifmt_ctx->nb_streams; i++)
        if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoindex = i;
            break;
        }

    // 帮助初始化
    ret = av_read_frame(ifmt_ctx, &pkt);
    first_pack = 1;
    if (ret < 0)
    {
        printf("av_read_frame failed\n");
        return;
    }
    ifmt_ctx->streams[0]->time_base.den = 1000000;
    ifmt_ctx->streams[0]->time_base.num = 1;
    // 设置帧率
    ifmt_ctx->streams[0]->r_frame_rate.num = fps;
    ifmt_ctx->streams[0]->r_frame_rate.den = 1;

    ifmt_ctx->streams[0]->avg_frame_rate.num = fps;
    ifmt_ctx->streams[0]->avg_frame_rate.den = 1;

    // Stream #0:0: Video: h264 (High), yuv420p(progressive), 1920x1080, 30 fps, 30 tbr, 1200k tbn, 60 tbc
    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    printf("dump data\n");

    // Output
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); // RTMP
    // avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", out_filename);//UDP

    if (!ofmt_ctx)
    {
        printf("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        destropy_ctx();
        return;
    }

    printf("ofmt_ctx created\n");

    ofmt = ofmt_ctx->oformat;
    for (i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        // Create output AVStream according to input AVStream
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream)
        {
            printf("Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            destropy_ctx();
            return;
        }
        // Copy the settings of AVCodecContext
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0)
        {
            printf("Failed to copy context from input to output stream codec context\n");
            destropy_ctx();
            return;
        }
        out_stream->codec->codec_tag = 0;
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // CODEC_FLAG_GLOBAL_HEADER;
    }
    // Dump Format------------------
    av_dump_format(ofmt_ctx, 0, out_filename, 1);
    // Open output URL
    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            printf("Could not open output URL '%s'", out_filename);
            destropy_ctx();
            return;
        }
    }

    printf("send head\n");

    // Write file header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        printf("Error occurred when opening output URL\n");
        destropy_ctx();
        return;
    }
    printf("head sent \n");
    // printf("start_time %ld\n", start_time);
}

void set_start_time(void)
{
    start_time = av_gettime();
}

void get_time_gap(void)
{
    static int64_t last_time = 0;
    static int64_t frame_counter = 1;
    // static RK_S32 packet_cnt = 0;
    static int64_t gime_gap_total = 0;
    // printf("# Get packet-%d, size %zu\n", packet_cnt, RK_MPI_MB_GetSize(mb));
    if (frame_counter == 1)
    {
        last_time = av_gettime();
    }
    int64_t time_gap_ms = (av_gettime() - last_time) / 1000;
    printf("time gap:%lld\n", time_gap_ms);
    gime_gap_total = time_gap_ms + gime_gap_total;
    // printf("gap total:%lld\n", gime_gap_total);
    // printf("gap average:%lld\n", gime_gap_total / frame_counter);

    last_time = av_gettime();
    // RK_MPI_MB_ReleaseBuffer(mb);
    frame_counter++;
}

void send_to_rtmp_server_testing(uint8_t *h264_data, int data_len)
{
    get_time_gap();
}

void send_to_rtmp_server(uint8_t *h264_data, int data_len, long long time_stamp) //  int time_gap_ms)
{
    int ret = 0;
    AVStream *in_stream, *out_stream;
    if (first_pack == 0)
    {
        // ret = av_read_frame(ifmt_ctx, &pkt);
        // first_pack = 1;
        // if (ret < 0)
        // {
        //     printf("av_read_frame failed\n");
        //     return;
        // }
        // ifmt_ctx->streams[0]->time_base.den = 1000000;
    }

    // Get an AVPacket
    pkt.data = h264_data;
    pkt.size = data_len;
    pkt.pts = time_stamp; // frame_index * time_gap_ms;
    pkt.dts = pkt.pts;
    pkt.duration = 20;
    // printf("Sending %8d \n", frame_index);
    ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
    if (ret < 0)
    {
        printf("Error muxing packet\n");
        return;
    }

    frame_index++;
    av_free_packet(&pkt);
}

void send_to_rtmp_server_tested(uint8_t *h264_data, int data_len)
{
    int ret = 0;
    AVStream *in_stream, *out_stream;
    if (first_pack == 0)
    {
        ret = av_read_frame(ifmt_ctx, &pkt);
        first_pack = 1;
        if (ret < 0)
        {
            printf("av_read_frame failed\n");
            return;
        }
        ifmt_ctx->streams[0]->time_base.den = 1000000;
    }

    // Get an AVPacket
    pkt.data = h264_data;
    pkt.size = data_len;
    // FIX No PTS (Example: Raw H.264)
    // Simple Write PTS
    // Write PTS
    AVRational time_base1 = ifmt_ctx->streams[0]->time_base;
    printf("time_base: num: %d len: %d\n", time_base1.num, time_base1.den);
    printf("frame :num:%d den:%d\n", ifmt_ctx->streams[0]->r_frame_rate.num, ifmt_ctx->streams[0]->r_frame_rate.den);

    // Duration between 2 frames (us)
    int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmt_ctx->streams[0]->r_frame_rate);
    printf("calc_duration:%ld\n", calc_duration);
    // Parameters
    pkt.pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
    pkt.dts = pkt.pts;
    pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);

    // Important:Delay
    AVRational time_base = ifmt_ctx->streams[0]->time_base;
    AVRational time_base_q = {1, AV_TIME_BASE};
    // printf("time_base num :%d den:%d time_base_q num :%d den:%d \n",
    // time_base.num, time_base.den, time_base_q.num, time_base_q.den);
    int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
    int64_t now_time = av_gettime() - start_time;
    printf("pts_time:%d , now_time :%d \n", pts_time, now_time);
    if (pts_time > now_time)
    {
        av_usleep(pts_time - now_time);
        printf("-----------------\n");
    }

    in_stream = ifmt_ctx->streams[pkt.stream_index];
    out_stream = ofmt_ctx->streams[pkt.stream_index];
    /* copy packet */
    // Convert PTS/DTS
    pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
    pkt.pos = -1;

    // Print to Screen
    printf("Send %8d video frames to output URL\n", frame_index);
    frame_index++;

    printf("pts:%d dts:%d duration:%d\n", pkt.pts, pkt.dts, pkt.duration);

    // for (int j = 0; j < 10; j++)
    // {
    //     printf("%02x ", pkt.data[j]);
    // }
    // printf("\n");
    ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
    if (ret < 0)
    {
        printf("Error muxing packet\n");
        return;
    }

    av_free_packet(&pkt);
}
