#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <getopt.h>

#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <time.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define EXIT_USAGE      1
#define EXIT_SOCKET     2
#define EXIT_BIND       3
#define EXIT_MEMORY     4
#define EXIT_FILE       5

static const char *app_name = "utxrx";
static int opt_txrx = 0; /* 1=tx, -1=rx */
static int opt_port = 12345;
static const char *opt_addr = "255.255.255.255";
static in_addr_t udp_addr;
static unsigned long opt_count = 1000;
static size_t opt_size = 1024;
static unsigned long opt_delay = 10*1000; /* us */
static unsigned char opt_byte = 0xAA;
static const char *opt_file = NULL;

static void usage(const char *msg)
{
    if (msg)
    {
        printf("%s\n", msg);
    }
    printf("%s [--options] [addr]\n", app_name);
    printf("\n");
    printf("--help|h:           this help\n");
    printf("--tx|t:             do transmit (default %s)\n", opt_txrx > 0 ? "transmit" : "receive");
    printf("--rx|r:             do receive (default %s)\n", opt_txrx > 0 ? "transmit" : "receive");
    printf("--port|p=num:       udp port (default %d)\n", opt_port);
    printf("--count|c=num:      packet count (default %lu)\n", opt_count);
    printf("--size|s=num:       packet size (default %zu)\n", opt_size);
    printf("--delay|d=num:      mdelay between transmit (default %lu)\n", opt_delay / 1000);
    printf("--Delay|d=num:      udelay between transmit (default %lu)\n", opt_delay);
    printf("--byte|b=num:       byte to fill packet (default %#x)\n", opt_byte);
    printf("--file|f=str:       file to read/write (default %s)\n", opt_file ?: "");
    printf("\n");
}

static int option(int argc, char *argv[])
{
    enum {
        OPT_HELP  = 'h',
        OPT_TX    = 't',
        OPT_RX    = 'r',
        OPT_PORT  = 'p',
        OPT_COUNT = 'c',
        OPT_SIZE  = 's',
        OPT_MDELAY= 'd',
        OPT_UDELAY= 'D',
        OPT_BYTE  = 'b',
        OPT_FILE  = 'f',
    };
    static struct option options[] =
    {
        {"help",    no_argument,       0, OPT_HELP},
        {"tx",      no_argument,       0, OPT_TX},
        {"rx",      no_argument,       0, OPT_RX},
        {"port",    required_argument, 0, OPT_PORT},
        {"count",   required_argument, 0, OPT_COUNT},
        {"size",    required_argument, 0, OPT_SIZE},
        {"delay",   required_argument, 0, OPT_MDELAY},
        {"Delay",   required_argument, 0, OPT_UDELAY},
        {"byte",    required_argument, 0, OPT_BYTE},
        {"file",    required_argument, 0, OPT_FILE},
        {0,         0,                 0, 0}
    };
    int c, index;

    udp_addr = inet_addr(opt_addr);

    for (;;)
    {
        index = 0;
        c = getopt_long(argc, argv, "htrp:c:s:d:D:b:f:", options, &index);
        if (c == -1)
            break;
        switch (c)
        {
        case 0:
            break;
        case '?':
            exit(EXIT_USAGE);
        case OPT_HELP:
            usage(NULL);
            exit(0);
        case OPT_TX:
            opt_txrx = 1;
            break;
        case OPT_RX:
            opt_txrx = -1;
            break;
        case OPT_PORT:
            opt_port = atoi(optarg);
            break;
        case OPT_COUNT:
            opt_count = strtoul(optarg, NULL, 0);
            break;
        case OPT_SIZE:
            opt_size = strtoul(optarg, NULL, 0);
            break;
        case OPT_MDELAY:
            opt_delay = strtoul(optarg, NULL, 0) * 1000;
            break;
        case OPT_UDELAY:
            opt_delay = strtoul(optarg, NULL, 0);
            break;
        case OPT_BYTE:
            opt_byte = strtoul(optarg, NULL, 0);
            break;
        case OPT_FILE:
            opt_file = optarg;
            break;
        }
    }
    if (optind < argc)
    {
        opt_addr = argv[optind];
        udp_addr = inet_addr(opt_addr);
        if (udp_addr == htonl(INADDR_NONE))
        {
            fprintf(stderr, "bad address %s\n", opt_addr);
            exit(EXIT_USAGE);
        }
    }
    if (opt_txrx > 0) /* tx */
    {
        if (udp_addr == htonl(INADDR_NONE))
        {
            opt_addr = "127.0.0.1";
            udp_addr = htonl(INADDR_LOOPBACK);
        }
    }
    if (opt_txrx < 0) /* rx */
    {
        if (udp_addr == htonl(INADDR_NONE))
        {
            opt_addr = "0.0.0.0";
            udp_addr = htonl(INADDR_ANY);
        }
    }
    return 0;
}

sig_atomic_t cancel = 0;
static void sig_handler(int sig)
{
    cancel = 1;
}

static long get_time_ms(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        return -1;
    else
        return ts.tv_sec * 1000L + (ts.tv_nsec + 500000L) / 1000000L;
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL))
	return -1;
    else
        return tv.tv_sec * 1000L + (tv.tv_usec + 500L) / 1000L;
#endif
}

static int do_transmit(void)
{
    int ret  = 0;
    int sock = -1;
    struct sockaddr_in addr;
    void *buf = NULL;
    int fd = -1;
    ssize_t cnt = opt_size;

    unsigned long total   = 0;
    unsigned long success = 0;
    unsigned long failure = 0;

    long begin_ms, end_ms;

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        ret = EXIT_SOCKET;
        goto out;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons (opt_port);
    addr.sin_addr.s_addr = udp_addr;

    buf = malloc(opt_size);
    if (buf == NULL)
    {
        fprintf(stderr, "failed to allocate memory\n");
        ret = EXIT_MEMORY;
        goto out;
    }
    if (opt_file)
    {
        fd = open(opt_file, O_RDONLY);
        if (fd < 0)
        {
            fprintf(stderr, "failed to open file %s\n", opt_file);
            ret = EXIT_FILE;
            goto out;
        }
        cnt = read(fd, buf, opt_size);
        if (cnt < 0)
        {
            fprintf(stderr, "failed to read file %s\n", opt_file);
            ret = EXIT_FILE;
            goto out;
        }
        printf("transmiting %lu packets of %zu bytes from %s with delay %lu us\n", opt_count, opt_size, opt_file, opt_delay);
    }
    else
    {
        memset(buf, opt_byte, opt_size);
        printf("transmiting %lu packets of %zu bytes %#x with delay %lu us\n", opt_count, opt_size, opt_byte, opt_delay);
    }
    begin_ms = get_time_ms();

    while (!cancel && total < opt_count && cnt > 0)
    {
        ret = sendto(sock, buf, cnt, 0, (const struct sockaddr *)&addr, sizeof(addr));
        if (ret < 0)
        {
            ++failure;
        }
        else
        {
            ++success;
        }
        ++total;

        if (fd >= 0)
        {
            cnt = read(fd, buf, opt_size);
            if (cnt < 0)
            {
                fprintf(stderr, "failed to read file %s\n", opt_file);
                break;
            }
        }
        if (opt_delay)
        {
            usleep(opt_delay);
        }
    }

    end_ms = get_time_ms();
    printf("transmitted %lu, failed %lu packets of %zu bytes\n", success, failure, opt_size);
    if (end_ms > begin_ms && begin_ms >= 0)
    {
        printf("totally %lu packets in %ld ms = %lu packets per second\n", total, (end_ms-begin_ms), total * 1000 / (end_ms-begin_ms));
    }

    ret = 0;
out:
    if (fd >= 0)
    {
        close(fd);
    }
    if (sock >= 0)
    {
        close(sock);
    }
    if (buf)
    {
        free(buf);
    }
    return ret;
}

static int do_receive(void)
{
    int ret  = 0;
    int sock = -1;
    struct sockaddr_in addr;
    socklen_t addrlen;
    void *buf = NULL;
    int fd = -1;

    unsigned long total   = 0;
    unsigned long success = 0;
    unsigned long failure = 0;

    long begin_ms, end_ms;

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        ret = EXIT_SOCKET;
        goto out;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons (opt_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(sock, (const struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0)
    {
        perror("bind");
        ret = EXIT_BIND;
        goto out;
    }

    buf = malloc(opt_size);
    if (buf == NULL)
    {
        fprintf(stderr, "failed to allocate memory\n");
        ret = EXIT_MEMORY;
        goto out;
    }
    if (opt_file)
    {
        fd = open(opt_file, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
        if (fd < 0)
        {
            fprintf(stderr, "failed to open file %s\n", opt_file);
            ret = EXIT_FILE;
            goto out;
        }
        printf("receiving %lu packets of %zu bytes to %s\n", opt_count, opt_size, opt_file);
    }
    else
    {
        printf("receiving %lu packets of %zu bytes\n", opt_count, opt_size);
    }
    begin_ms = 0;

    while (!cancel && total < opt_count)
    {
        addrlen = sizeof(addr);
        ret = recvfrom(sock, buf, opt_size, 0, (struct sockaddr *)&addr, &addrlen);
        if (begin_ms == 0)
        {
            begin_ms = get_time_ms();
        }
        if (ret != opt_size)
        {
            ++failure;
        }
        else
        {
            ++success;
        }
        ++total;
        if (fd >= 0 && ret > 0)
        {
            ssize_t cnt = write(fd, buf, ret);
            if (cnt < 0)
            {
                fprintf(stderr, "failed to write file %s\n", opt_file);
                break;
            }
        }
    }

    end_ms = get_time_ms();
    printf("received %lu, failed %lu packets of %zu bytes\n", success, failure, opt_size);
    if (end_ms > begin_ms && begin_ms >= 0)
    {
        printf("totally %lu packets in %ld ms = %lu packets per second\n", total, (end_ms-begin_ms), total * 1000 / (end_ms-begin_ms));
    }

    ret = 0;
out:
    if (fd >= 0)
    {
        close(fd);
    }
    if (sock >= 0)
    {
        close(sock);
    }
    if (buf)
    {
        free(buf);
    }
    return ret;
}

int main(int argc, char *argv[])
{
    int ret;

    app_name = basename(argv[0]);
    if (opt_txrx == 0)
    {
        if (strcmp(app_name, "utx") == 0)
            opt_txrx = 1;
        else if (strcmp(app_name, "urx") == 0)
            opt_txrx = -1;
    }
    if (option(argc, argv))
    {
        return EXIT_USAGE;
    }

#if 0
    signal(SIGINT, sig_handler);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
#endif

    if (opt_txrx > 0)
    {
        ret = do_transmit();
    }
    else if (opt_txrx < 0)
    {
        ret = do_receive();
    }
    else
    {
        ret = EXIT_USAGE;
        usage(NULL);
    }

    return ret;
}

/*
 * Local Variables:
 *   c-file-style: "stroustrup"
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: set ai cindent et sta sw=4:
 */
