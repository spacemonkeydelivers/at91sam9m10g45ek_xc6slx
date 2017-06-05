#include <linux/drivers/misc/fpga-sk-at91sam9m10g45-xc6slx.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <string.h>
#include <stdio.h>
#include <stdint.h>

typedef struct program_opt
{
    int mmap_mode;
    int byte_granularity;

    int valid;
} program_opt;

struct program_opt parse_opts(int argc, char* argv[])
{
    program_opt result;

    result.mmap_mode        = SKFP_MMAP_MODE_DEFAULT;
    result.byte_granularity = 1;
    result.valid            = 1;

    int i = 0;
    for (i = 2; i < argc; ++i) {

        if (strcmp(argv[i], "--int") == 0) {
            result.byte_granularity = 0;
        }
        if (strcmp(argv[i], "--byte") == 0) {
            result.byte_granularity = 1;
        }
        if (strcmp(argv[i], "--attrs") == 0) {

            int a_idx = i + 1;

            if (a_idx >= argc) {
                fprintf(stderr, "%s", "invalid number of arguments!\n");
                result.valid = -1;
                return result;
            }
            const char* attr = argv[a_idx];

            if (strcmp(attr, "WB") == 0)
                result.mmap_mode = SKFP_MMAP_MODE_WB;
            else if (strcmp(attr, "WC") == 0)
                result.mmap_mode = SKFP_MMAP_MODE_WC;
            else if (strcmp(attr, "WT") == 0)
                result.mmap_mode = SKFP_MMAP_MODE_WT;
            else if (strcmp(attr, "DEFAULT") == 0)
                result.mmap_mode = SKFP_MMAP_MODE_DEFAULT;
            else
                result.valid = -1;

            if (result.valid <= 0) {
                fprintf(stderr, "invalid --attr argument: \"%s\"\n", attr);
                return result;
            }

            i = a_idx;
        }
    }
    return result;
}

void write_byte_sequence(volatile uint8_t* const ptr, int bytes_num)
{
    if (!bytes_num)
        return;

    int i = 0;
    for (i = 0; i < bytes_num; ++i) {
        ptr[i] = (uint8_t)i;
    }
}
void write_int_sequence(volatile uint32_t* const ptr, int bytes_num)
{
    if (!bytes_num)
        return;

    uintptr_t PTR_VAL = (uintptr_t)ptr;
    if (PTR_VAL & 0b11) //this means that address is not 4-byte aligined
    {
        volatile uint8_t* byte_ptr = (volatile uint8_t*)ptr;
        *byte_ptr = 0xf0 | (PTR_VAL & 0xf);
        //inefficient, but should work
        write_int_sequence((volatile uint32_t*)(byte_ptr + 1), bytes_num - 1);
        return;
    }
    const int inum = bytes_num / 4;
    int i = 0;
    for (i = 0; i < inum; ++i) {
        ptr[i] = (uint32_t)i;
    }

    write_byte_sequence((volatile uint8_t*)(ptr + inum), bytes_num % 4);
}
int main (int argc, char* argv[])
{
	if (argc < 2) {
	printf("%s",
               "usage:\nfpga_loader <filename> [--attrs attr] [--byte] [--int]\n"
               "    attr can be one of WB,WC,WT,DEFAULT\n");
        return 0;
    }

    program_opt opts = parse_opts(argc, argv);
    if (opts.valid <= 0)
        return -1;

    int result       = -1;
    int window_size  = 0;
    volatile void* mmap_area = 0;

    const char* filename = argv[1];

    int fd = open(filename, O_RDWR);

    if (fd == -1) {
        perror("open()");
        goto cleanup;
    }

    printf("%s is opened\n", filename);

    if (ioctl(fd, SKFP_IOCQSIZE, &window_size) == -1) {
        perror("ioctl (size)");
        goto cleanup;
    }
    printf("resulting buffer size: 0x%x\n", window_size);

    if (ioctl(fd, SKFP_IOCSCMODE, &opts.mmap_mode) == -1) {
        perror("ioctl (set mode)");
        goto cleanup;
    }

    mmap_area = mmap(0, window_size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    if (mmap_area == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }

    if (opts.byte_granularity) {
        printf("writing data using byte access...\n");
        write_byte_sequence(mmap_area, window_size);
    }
    else {
        printf("writing data using 32-bit access...\n");
        write_int_sequence(mmap_area, window_size);
    }
    printf("%d bytes were written\n", window_size);

    result = 0;

cleanup:

    if (mmap_area && (mmap_area != MAP_FAILED)) {
        munmap((void*)mmap_area, window_size);
        mmap_area = 0;
    }

    if (fd != -1)
        close(fd);
    fd = 0;
    return result;
}
