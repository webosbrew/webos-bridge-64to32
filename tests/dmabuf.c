#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Candidate struct layouts for the alloc ioctl.
 * We don't know the exact struct, so try several common ones.    */

/* Layout A: standard DMA-BUF Heaps (Linux 5.6+) — 24 bytes */
struct layout_a {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};

/* Layout B: ion-style (Android / older kernels) — 16 bytes */
struct layout_b {
    size_t   len;       /* 4 bytes on 32-bit */
    uint32_t heap_id_mask;
    uint32_t flags;
    int      fd;
};

/* Layout C: compact — 12 bytes */
struct layout_c {
    uint32_t len_lo;
    uint32_t len_hi;
    uint32_t flags;
};

/* ioctl builder: _IOWR(magic, nr, size) on 32-bit arm */
static unsigned long mk_iowr(unsigned char magic, unsigned char nr,
                              unsigned int size)
{
    return (3UL << 30) | ((unsigned long)size << 16)
         | ((unsigned long)magic << 8) | nr;
}

static void probe(int fd, unsigned char magic, unsigned char nr,
                  void *buf, unsigned int bufsz)
{
    unsigned long ioc = mk_iowr(magic, nr, bufsz);
    int r = ioctl(fd, ioc, buf);
    int e = errno;
    if (r == 0)
        printf("  MATCH  magic='%c'(0x%02x) nr=0x%02x  ioc=0x%08lx  "
               "bufsz=%u  -> OK\n",
               magic >= 0x20 ? magic : '.', magic, nr, ioc, bufsz);
    else if (e != ENOTTY)          /* ENOTTY = wrong magic, skip silently */
        printf("  MAYBE  magic='%c'(0x%02x) nr=0x%02x  ioc=0x%08lx  "
               "bufsz=%u  -> errno=%d (%s)\n",
               magic >= 0x20 ? magic : '.', magic, nr, ioc, bufsz,
               e, strerror(e));
}

int main(void)
{
    /* ── sysfs / proc first ───────────────────────────────────────────── */
    printf("=== /proc/misc ===\n");
    FILE *f = fopen("/proc/misc", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f))
            if (strstr(line, "dma")) printf("  %s", line);
        fclose(f);
    }

    printf("=== /sys/class/misc/dma_buf_unified/ ===\n");
    system("ls /sys/class/misc/dma_buf_unified/ 2>/dev/null || echo '  (not found)'");
    system("cat /sys/class/misc/dma_buf_unified/dev 2>/dev/null"
           " && echo '' || true");

    /* ── open device ──────────────────────────────────────────────────── */
    int fd = open("/dev/dma_buf_unified", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("open /dev/dma_buf_unified"); return 1; }
    printf("\ndevice fd=%d  probing ioctl space...\n\n", fd);

    /* Candidate magic bytes used by dma_buf / ion / vendor allocators */
    static const unsigned char magics[] = {
        'H',  /* standard DMA-BUF Heaps (Linux 5.6+)  */
        'I',  /* ION (Android)                         */
        'D',  /* generic dma_buf                       */
        'L',  /* LG proprietary?                       */
        'M',  /* Mali?                                 */
        'G',  /* graphics?                             */
        'd',  /* lowercase dma?                        */
        0xb2, /* Broadcom / misc                       */
        0xa0, /* webOS vendor range                    */
    };
    /* Candidate opcodes: 0=alloc, 1=free, 2=map, 3=share */
    static const unsigned char nrs[] = { 0, 1, 2, 3 };
    /* Candidate struct sizes */
    static const unsigned int sizes[] = {
        sizeof(struct layout_a),   /* 24 */
        sizeof(struct layout_b),   /* 16 */
        sizeof(struct layout_c),   /* 12 */
        8, 20, 28, 32, 36, 40,
    };

    /* Zero-fill test buffers */
    unsigned char buf[64] = {0};

    /* Set a plausible allocation size for layout_a / layout_b tries */
    struct layout_a *a = (void *)buf;
    a->len      = (uint64_t)1920 * 1080 * 4;
    a->fd_flags = O_RDWR | O_CLOEXEC;

    for (size_t mi = 0; mi < sizeof(magics); mi++) {
        for (size_t ni = 0; ni < sizeof(nrs); ni++) {
            for (size_t si = 0; si < sizeof(sizes)/sizeof(sizes[0]); si++) {
                probe(fd, magics[mi], nrs[ni], buf, sizes[si]);
            }
        }
    }

    close(fd);
    printf("\nDone. If no MATCH line appeared, run:\n"
           "  strace -e ioctl <some-app-that-allocates-buffers>\n"
           "or check the webOS sysroot for a dma_buf_unified header.\n");
    return 0;
}
