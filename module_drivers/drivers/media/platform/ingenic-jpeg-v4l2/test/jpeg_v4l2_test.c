/*
 * JPEG V4L2 Driver Test Application
 *
 * This application demonstrates how to use the Ingenic JPEG V4L2 driver
 * for encoding and decoding JPEG images.
 */

#define _POSIX_C_SOURCE 199309L

#include <time.h>  // 用于调试时间戳
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <stdint.h>

// 调试宏
#define DEBUG_ENABLE 1
#if DEBUG_ENABLE
#define DEBUG(fmt, ...) printf("[%s:%d] " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)
#endif

#define MAX_BUFFER_SIZE (10 * 1024 * 1024) // 10MB

struct jpeg_test_ctx {
    int fd;
    enum v4l2_buf_type src_type;
    enum v4l2_buf_type dst_type;
    void *src_bufs[4];
    void *dst_bufs[4];
    int src_buf_len[4];
    int dst_buf_len[4];
    int src_buf_count;
    int dst_buf_count;
    int src_buf_index;
    int dst_buf_index;
};

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    
    // 记录ioctl操作结果
    if (r < 0) {
        DEBUG("ioctl 0x%lx failed: %s", request, strerror(errno));
    } else {
        DEBUG("ioctl 0x%lx succeeded", request);
    }
    
    return r;
}

static int init_device(struct jpeg_test_ctx *ctx, const char *dev_name)
{
    struct v4l2_capability cap;
    int ret;

    ctx->fd = open(dev_name, O_RDWR);
    if (ctx->fd < 0) {
        perror("Failed to open device");
        return -1;
    }

    ret = xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        perror("Failed to query capabilities");
        close(ctx->fd);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_M2M)) {
        fprintf(stderr, "Device does not support memory-to-memory operations\n");
        close(ctx->fd);
        return -1;
    }

    printf("Device: %s\n", dev_name);
    printf("Driver: %s\n", cap.driver);
    printf("Card: %s\n", cap.card);

    return 0;
}

static int set_format(struct jpeg_test_ctx *ctx, enum v4l2_buf_type type, 
                      uint32_t pixelformat, int width, int height)
{
    struct v4l2_format fmt = {0};
    int ret;

    fmt.type = type;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    ret = xioctl(ctx->fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        perror("Failed to set format");
        return -1;
    }

    printf("Set format for type %d: %c%c%c%c %dx%d\n", type,
           pixelformat & 0xFF, (pixelformat >> 8) & 0xFF,
           (pixelformat >> 16) & 0xFF, (pixelformat >> 24) & 0xFF,
           fmt.fmt.pix.width, fmt.fmt.pix.height);

    return 0;
}

static int init_mmap(struct jpeg_test_ctx *ctx, enum v4l2_buf_type type, void **buffers, 
                     int *buf_lens, int *buf_count)
{
    struct v4l2_requestbuffers req = {0};
    struct v4l2_buffer buf = {0};
    int i, ret;

    req.count = 4;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;

    ret = xioctl(ctx->fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        perror("Failed to request buffers");
        return -1;
    }

    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on device\n");
        return -1;
    }

    *buf_count = req.count;

    for (i = 0; i < (int)req.count; i++) {
        buf.type = type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        ret = xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            perror("Failed to query buffer");
            return -1;
        }

        buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                          ctx->fd, buf.m.offset);
        if (buffers[i] == MAP_FAILED) {
            perror("Failed to mmap buffer");
            return -1;
        }

        buf_lens[i] = buf.length;
        printf("Mapped buffer %d with size %d bytes\n", i, buf.length);
    }

    return 0;
}

static int init_buffers(struct jpeg_test_ctx *ctx)
{
    // Initialize source buffers (for encoding: raw image; for decoding: JPEG data)
    ctx->src_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (init_mmap(ctx, ctx->src_type, ctx->src_bufs, ctx->src_buf_len, &ctx->src_buf_count) < 0) {
        return -1;
    }

    // Initialize destination buffers (for encoding: JPEG data; for decoding: raw image)
    ctx->dst_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (init_mmap(ctx, ctx->dst_type, ctx->dst_bufs, ctx->dst_buf_len, &ctx->dst_buf_count) < 0) {
        return -1;
    }

    return 0;
}

static int start_streaming(struct jpeg_test_ctx *ctx)
{
    int ret;

    ret = xioctl(ctx->fd, VIDIOC_STREAMON, &ctx->src_type);
    if (ret < 0) {
        perror("Failed to start source streaming");
        return -1;
    }

    ret = xioctl(ctx->fd, VIDIOC_STREAMON, &ctx->dst_type);
    if (ret < 0) {
        perror("Failed to start destination streaming");
        return -1;
    }

    printf("Streaming started\n");
    return 0;
}

static int stop_streaming(struct jpeg_test_ctx *ctx)
{
    int ret;

    ret = xioctl(ctx->fd, VIDIOC_STREAMOFF, &ctx->src_type);
    if (ret < 0) {
        perror("Failed to stop source streaming");
        return -1;
    }

    ret = xioctl(ctx->fd, VIDIOC_STREAMOFF, &ctx->dst_type);
    if (ret < 0) {
        perror("Failed to stop destination streaming");
        return -1;
    }

    printf("Streaming stopped\n");
    return 0;
}

static int process_frame(struct jpeg_test_ctx *ctx, const char *input_file, const char *output_file)
{
    struct v4l2_buffer src_buf = {0};
    struct v4l2_buffer dst_buf = {0};
    FILE *infile, *outfile;
    size_t bytes_read;
    int ret;
    clock_t start_time, end_time;

    DEBUG("Starting frame processing: input=%s, output=%s", input_file, output_file);
    start_time = clock();

    // Open input file
    infile = fopen(input_file, "rb");
    if (!infile) {
        perror("Failed to open input file");
        return -1;
    }

    // Read input data to source buffer
    bytes_read = fread(ctx->src_bufs[0], 1, ctx->src_buf_len[0], infile);
    fclose(infile);

    if (bytes_read <= 0) {
        fprintf(stderr, "Failed to read input file");
        return -1;
    }

    DEBUG("Read %zu bytes from input file, buffer size: %d", bytes_read, ctx->src_buf_len[0]);

    // 正确的V4L2 M2M处理流程：
    // 1. 队列化源缓冲区
    src_buf.type = ctx->src_type;
    src_buf.memory = V4L2_MEMORY_MMAP;
    src_buf.index = 0;
    src_buf.bytesused = bytes_read;

    DEBUG("Queuing source buffer: index=%d, bytesused=%d", src_buf.index, src_buf.bytesused);
    ret = xioctl(ctx->fd, VIDIOC_QBUF, &src_buf);
    if (ret < 0) {
        perror("Failed to queue source buffer");
        return -1;
    }
    DEBUG("Source buffer queued successfully");

    // 2. 队列化目标缓冲区
    dst_buf.type = ctx->dst_type;
    dst_buf.memory = V4L2_MEMORY_MMAP;
    dst_buf.index = 0;
    dst_buf.bytesused = 0;

    DEBUG("Queuing destination buffer: index=%d, bytesused=%d", dst_buf.index, dst_buf.bytesused);
    ret = xioctl(ctx->fd, VIDIOC_QBUF, &dst_buf);
    if (ret < 0) {
        perror("Failed to queue destination buffer");
        return -1;
    }
    DEBUG("Destination buffer queued successfully");

    // 3. Dequeue目标缓冲区（包含处理结果）
    dst_buf.type = ctx->dst_type;
    dst_buf.memory = V4L2_MEMORY_MMAP;

    DEBUG("Attempting to dequeue destination buffer");
    ret = xioctl(ctx->fd, VIDIOC_DQBUF, &dst_buf);
    if (ret < 0) {
        perror("Failed to dequeue destination buffer");
        return -1;
    }
    DEBUG("Destination buffer dequeued: index=%d, bytesused=%d, flags=0x%x, timestamp=%llu", 
          dst_buf.index, dst_buf.bytesused, dst_buf.flags, (unsigned long long)dst_buf.timestamp.tv_sec);

    // 4. Dequeue源缓冲区
    src_buf.type = ctx->src_type;
    src_buf.memory = V4L2_MEMORY_MMAP;

    DEBUG("Attempting to dequeue source buffer");
    ret = xioctl(ctx->fd, VIDIOC_DQBUF, &src_buf);
    if (ret < 0) {
        perror("Failed to dequeue source buffer");
        return -1;
    }
    DEBUG("Source buffer dequeued: index=%d, bytesused=%d, flags=0x%x", 
          src_buf.index, src_buf.bytesused, src_buf.flags);

    printf("Processed frame, output size: %d bytes\n", dst_buf.bytesused);
    end_time = clock();
    DEBUG("Frame processing completed in %.2f ms", 
          ((double)(end_time - start_time)) * 1000 / CLOCKS_PER_SEC);

    // Write output data
    outfile = fopen(output_file, "wb");
    if (!outfile) {
        perror("Failed to open output file");
        return -1;
    }

    bytes_read = fwrite(ctx->dst_bufs[0], 1, dst_buf.bytesused, outfile);
    fclose(outfile);

    if (bytes_read != dst_buf.bytesused) {
        fprintf(stderr, "Failed to write complete output file\n");
        return -1;
    }

    printf("Wrote %zu bytes to output file\n", bytes_read);

    return 0;
}

static void uninit_buffers(struct jpeg_test_ctx *ctx)
{
    int i;

    // Unmap source buffers
    for (i = 0; i < ctx->src_buf_count; i++) {
        if (ctx->src_bufs[i]) {
            munmap(ctx->src_bufs[i], ctx->src_buf_len[i]);
        }
    }

    // Unmap destination buffers
    for (i = 0; i < ctx->dst_buf_count; i++) {
        if (ctx->dst_bufs[i]) {
            munmap(ctx->dst_bufs[i], ctx->dst_buf_len[i]);
        }
    }
}

static void close_device(struct jpeg_test_ctx *ctx)
{
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

static void print_usage(const char *program_name)
{
    printf("Usage: %s <device> <input_file> <output_file> <operation> [width] [height]\n", program_name);
    printf("  device      - V4L2 device node (e.g., /dev/video0)\n");
    printf("  input_file  - Input file path\n");
    printf("  output_file - Output file path\n");
    printf("  operation   - Operation type: 'encode' or 'decode'\n");
    printf("  width       - Image width (required for encode)\n");
    printf("  height      - Image height (required for encode)\n");
    printf("\nExamples:\n");
    printf("  %s /dev/video0 input.raw output.jpg encode 640 480\n", program_name);
    printf("  %s /dev/video1 input.jpg output.nv12 decode 640 480\n", program_name);
}

int main(int argc, char **argv)
{
    struct jpeg_test_ctx ctx = {0};
    const char *dev_name, *input_file, *output_file, *operation;
    uint32_t src_fmt, dst_fmt;
    int width = 640, height = 480;
    int ret;

    if (argc < 5) {
        print_usage(argv[0]);
        return -1;
    }

    dev_name = argv[1];
    input_file = argv[2];
    output_file = argv[3];
    operation = argv[4];

    if (strcmp(operation, "encode") == 0) {
        if (argc < 7) {
            print_usage(argv[0]);
            return -1;
        }
        width = atoi(argv[5]);
        height = atoi(argv[6]);
        
        // Source format (raw image) - using NV12 as an example
        src_fmt = V4L2_PIX_FMT_NV12;
        // Destination format (JPEG)
        dst_fmt = V4L2_PIX_FMT_JPEG;
    } else if (strcmp(operation, "decode") == 0) {
        if (argc < 7) {
            print_usage(argv[0]);
            return -1;
        }

        width = atoi(argv[5]);
        height = atoi(argv[6]);

        // Source format (JPEG)
        src_fmt = V4L2_PIX_FMT_JPEG;
        // Destination format (raw image) - using NV12 as an example
        dst_fmt = V4L2_PIX_FMT_NV12;
    } else {
        fprintf(stderr, "Invalid operation: %s\n", operation);
        print_usage(argv[0]);
        return -1;
    }

    printf("JPEG V4L2 Test Application\n");
    printf("Operation: %s\n", operation);
    printf("Device: %s\n", dev_name);
    printf("Input: %s\n", input_file);
    printf("Output: %s\n", output_file);

    // Initialize device
    ret = init_device(&ctx, dev_name);
    if (ret < 0) {
        return -1;
    }

    // Set source format
    ret = set_format(&ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT, src_fmt, width, height);
    if (ret < 0) {
        close_device(&ctx);
        return -1;
    }

    // Set destination format
    ret = set_format(&ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE, dst_fmt, width, height);
    if (ret < 0) {
        close_device(&ctx);
        return -1;
    }

    // Initialize buffers
    ret = init_buffers(&ctx);
    if (ret < 0) {
        close_device(&ctx);
        return -1;
    }

    // Start streaming
    ret = start_streaming(&ctx);
    if (ret < 0) {
        uninit_buffers(&ctx);
        close_device(&ctx);
        return -1;
    }

    // Process frame
    ret = process_frame(&ctx, input_file, output_file);
    if (ret < 0) {
        stop_streaming(&ctx);
        uninit_buffers(&ctx);
        close_device(&ctx);
        return -1;
    }

    // Stop streaming
    stop_streaming(&ctx);

    // Cleanup
    uninit_buffers(&ctx);
    close_device(&ctx);

    printf("Test completed successfully\n");
    return 0;
}
