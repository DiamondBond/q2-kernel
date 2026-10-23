#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define SC_SIGNATURE_SIZE 2048
#define SC_MAGIC_SIZE 512
#define SC_MAGIC_TAG 0x54424353U

#define CMD_SC_SETUP _IOWR('s', 0, void *)
#define CMD_SC_DECRYPT _IOWR('s', 1, void *)
#define CMD_SC_SETUP_32 _IOC(_IOC_READ | _IOC_WRITE, 's', 0, 4)
#define CMD_SC_SETUP_64 _IOC(_IOC_READ | _IOC_WRITE, 's', 0, 8)
#define CMD_SC_DEC_32 _IOC(_IOC_READ | _IOC_WRITE, 's', 1, 4)
#define CMD_SC_DEC_64 _IOC(_IOC_READ | _IOC_WRITE, 's', 1, 8)

struct options {
    const char *dev_path;
    const char *in_path;
    size_t chunk_size;
    unsigned long loops;
    unsigned long long input_offset;
    size_t input_size;
};

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s -i <input.bin|/dev/mmcblkXpY> [options]\n"
            "\n"
            "Options:\n"
            "  -d, --device <path>    SC device path (default: /dev/sc)\n"
            "  -i, --input <path>     Input with [2048-byte sckeys][encrypted payload]\n"
            "  -c, --chunk <bytes>    Bytes per CMD_sc_decrypt call (default: 2048)\n"
            "  -n, --loops <count>    Loop count; 0 means run until fail (default: 1)\n"
            "  -p, --offset <bytes>   Input offset (default: 0)\n"
            "  -s, --size <bytes>     Bytes to read from input (required for block dev)\n"
            "  -h, --help             Show this help\n",
            prog);
}

static int parse_u64(const char *s, unsigned long long *out)
{
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);

    if (end == s || *end != '\0')
        return -1;

    *out = v;
    return 0;
}

static int parse_args(int argc, char **argv, struct options *opt)
{
    int c;
    static const struct option long_opts[] = {
        {"device", required_argument, NULL, 'd'},
        {"input", required_argument, NULL, 'i'},
        {"chunk", required_argument, NULL, 'c'},
        {"loops", required_argument, NULL, 'n'},
        {"offset", required_argument, NULL, 'p'},
        {"size", required_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    opt->dev_path = "/dev/sc";
    opt->in_path = NULL;
    opt->chunk_size = 2048;
    opt->loops = 1;
    opt->input_offset = 0;
    opt->input_size = 0;

    while ((c = getopt_long(argc, argv, "d:i:c:n:p:s:h", long_opts, NULL)) != -1) {
        unsigned long long v;

        switch (c) {
        case 'd':
            opt->dev_path = optarg;
            break;
        case 'i':
            opt->in_path = optarg;
            break;
        case 'c':
            if (parse_u64(optarg, &v) < 0 || v == 0) {
                fprintf(stderr, "Invalid chunk size: %s\n", optarg);
                return -1;
            }
            opt->chunk_size = (size_t)v;
            break;
        case 'n':
            if (parse_u64(optarg, &v) < 0) {
                fprintf(stderr, "Invalid loops: %s\n", optarg);
                return -1;
            }
            opt->loops = (unsigned long)v;
            break;
        case 'p':
            if (parse_u64(optarg, &v) < 0) {
                fprintf(stderr, "Invalid offset: %s\n", optarg);
                return -1;
            }
            opt->input_offset = v;
            break;
        case 's':
            if (parse_u64(optarg, &v) < 0 || v == 0) {
                fprintf(stderr, "Invalid size: %s\n", optarg);
                return -1;
            }
            opt->input_size = (size_t)v;
            break;
        case 'h':
            usage(argv[0]);
            return 1;
        default:
            usage(argv[0]);
            return -1;
        }
    }

    if (!opt->in_path) {
        usage(argv[0]);
        return -1;
    }

    return 0;
}

static int read_exact(int fd, uint8_t *buf, size_t len)
{
    size_t done = 0;

    while (done < len) {
        ssize_t rd = read(fd, buf + done, len - done);
        if (rd < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (rd == 0)
            return -1;
        done += (size_t)rd;
    }

    return 0;
}

static int read_input(const struct options *opt, uint8_t **out_buf, size_t *out_len)
{
    int fd = -1;
    struct stat st;
    size_t to_read = 0;
    int is_reg = 0;
    uint8_t *buf = NULL;

    fd = open(opt->in_path, O_RDONLY);
    if (fd < 0) {
        perror("open input");
        return -1;
    }

    if (fstat(fd, &st) < 0) {
        perror("fstat input");
        close(fd);
        return -1;
    }

    is_reg = S_ISREG(st.st_mode);

    if (lseek(fd, (off_t)opt->input_offset, SEEK_SET) < 0) {
        perror("lseek input");
        close(fd);
        return -1;
    }

    if (opt->input_size > 0) {
        to_read = opt->input_size;
    } else if (is_reg) {
        unsigned long long fs = (unsigned long long)st.st_size;
        if (fs <= opt->input_offset) {
            fprintf(stderr, "Offset out of range: file=%llu offset=%llu\n", fs, opt->input_offset);
            close(fd);
            return -1;
        }
        to_read = (size_t)(fs - opt->input_offset);
    } else {
        fprintf(stderr, "Input is block/char device, please set --size <bytes>.\n");
        close(fd);
        return -1;
    }

    if (to_read <= SC_SIGNATURE_SIZE) {
        fprintf(stderr, "Input too small: need > %u bytes\n", SC_SIGNATURE_SIZE);
        close(fd);
        return -1;
    }

    buf = (uint8_t *)malloc(to_read);
    if (!buf) {
        perror("malloc input");
        close(fd);
        return -1;
    }

    if (read_exact(fd, buf, to_read) < 0) {
        perror("read input");
        free(buf);
        close(fd);
        return -1;
    }

    close(fd);
    *out_buf = buf;
    *out_len = to_read;
    return 0;
}

static uint32_t load_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int validate_sig_header(const uint8_t *sig, size_t payload_len)
{
    uint32_t magic = load_u32_le(sig);
    uint32_t total = load_u32_le(sig + SC_MAGIC_SIZE);

    if (magic != SC_MAGIC_TAG) {
        fprintf(stderr, "Invalid signature magic: 0x%x\n", magic);
        return -1;
    }

    if (total == 0 || (total % 16) != 0) {
        fprintf(stderr, "Invalid signature total: %u\n", total);
        return -1;
    }

    if ((size_t)total > payload_len) {
        fprintf(stderr, "Signature total=%u exceeds payload=%zu\n", total, payload_len);
        return -1;
    }

    return 0;
}

static int ioctl_with_fallback(int fd,
                               unsigned long *args,
                               unsigned long def_cmd,
                               unsigned long cmd32,
                               unsigned long cmd64,
                               unsigned long *cache)
{
    unsigned long cmd = *cache ? *cache : def_cmd;
    unsigned long alt = (cmd == cmd32) ? cmd64 : cmd32;

    if (ioctl(fd, cmd, args) == 0) {
        *cache = cmd;
        return 0;
    }

    if (errno == EINVAL && cmd != alt) {
        if (ioctl(fd, alt, args) == 0) {
            *cache = alt;
            return 0;
        }
    }

    return -1;
}

int main(int argc, char **argv)
{
    struct options opt;
    uint8_t *input = NULL;
    size_t input_len = 0;
    const uint8_t *sig;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t *dec_out = NULL;
    int devfd = -1;
    unsigned long cmd_setup_cache = 0;
    unsigned long cmd_dec_cache = 0;
    unsigned long loop = 0;
    int ret = EXIT_FAILURE;

    int parse = parse_args(argc, argv, &opt);
    if (parse > 0)
        return EXIT_SUCCESS;
    if (parse < 0)
        return EXIT_FAILURE;

    if (read_input(&opt, &input, &input_len) < 0)
        return EXIT_FAILURE;

    sig = input;
    payload = input + SC_SIGNATURE_SIZE;
    payload_len = input_len - SC_SIGNATURE_SIZE;

    if (validate_sig_header(sig, payload_len) < 0)
        goto out;

    dec_out = (uint8_t *)malloc(opt.chunk_size);
    if (!dec_out) {
        perror("malloc dec_out");
        goto out;
    }

    devfd = open(opt.dev_path, O_RDWR);
    if (devfd < 0) {
        perror("open device");
        goto out;
    }

    printf("Start V2 test: input=%s read=%zu payload=%zu chunk=%zu loops=%s\n",
           opt.in_path, input_len, payload_len, opt.chunk_size,
           opt.loops ? "limited" : "until-fail");

    while (opt.loops == 0 || loop < opt.loops) {
        unsigned long setup_args[2];
        unsigned int setup_total = 0;
        size_t done = 0;

        printf("TEST loop=%lu\n", loop + 1);

        setup_args[0] = (unsigned long)sig;
        setup_args[1] = (unsigned long)&setup_total;

        if (ioctl_with_fallback(devfd, setup_args,
                                CMD_SC_SETUP, CMD_SC_SETUP_32, CMD_SC_SETUP_64,
                                &cmd_setup_cache) < 0) {
            printf("CMD_sc_setup FAIL errno=%d(%s)\n", errno, strerror(errno));
            goto out;
        }

        if (setup_total == 0 || setup_total > payload_len) {
            fprintf(stderr, "CMD_sc_setup returned invalid total=%u payload=%zu\n",
                    setup_total, payload_len);
            errno = EINVAL;
            goto out;
        }

        while (done < setup_total) {
            size_t left = setup_total - done;
            size_t step = left > opt.chunk_size ? opt.chunk_size : left;
            unsigned long dec_args[3];

            dec_args[0] = (unsigned long)(payload + done);
            dec_args[1] = (unsigned long)dec_out;
            dec_args[2] = (unsigned long)step;

            printf("CMD_sc_decrypt off=%zu step=%zu ... ", done, step);
            fflush(stdout);
            if (ioctl_with_fallback(devfd, dec_args,
                                    CMD_SC_DECRYPT, CMD_SC_DEC_32, CMD_SC_DEC_64,
                                    &cmd_dec_cache) < 0) {
                printf("FAIL errno=%d(%s)\n", errno, strerror(errno));
                goto out;
            }
            printf("OK\n");
            done += step;
        }

        loop++;
        printf("PASS loop=%lu\n", loop);
    }

    printf("Completed loops=%lu without error\n", loop);
    ret = EXIT_SUCCESS;

out:
    if (devfd >= 0)
        close(devfd);
    free(dec_out);
    free(input);
    return ret;
}
