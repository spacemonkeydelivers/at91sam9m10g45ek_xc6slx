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


int main (int argc, char* argv[])
{
	if (argc < 2) {
		printf("%s",
               "usage:\nfpga_loader <filename> [--attrs attr]\n"
               "    attr can be one of WB,WC,WT,DEFAULT\n");
        return 0;
    }
    int MMAP_MODE = SKFP_MMAP_MODE_DEFAULT;

    for (int i = 2; i < argc; ++i) {

         if (strcmp(argv[i], "--attrs") == 0)                                                                                 {                                                                                                                          

             int a_idx = i + 1;

             if (a_idx >= argc) {
                 fprintf(stderr, "%s", "invalid number of erguments!\n");
                 return -1;
             }
             const char* attr = argv[a_idx];

             if (strcmp(attr, "WB") == 0)
                 MMAP_MODE = SKFP_MMAP_MODE_WB;
             else if (strcmp(attr, "WC") == 0)
                 MMAP_MODE = SKFP_MMAP_MODE_WC;
             else if (strcmp(attr, "WT") == 0)
                 MMAP_MODE = SKFP_MMAP_MODE_WT;
             else if (strcmp(attr, "DEFAULT") == 0)
                 MMAP_MODE = SKFP_MMAP_MODE_DEFAULT;

             i = a_idx;
         }                                                                                                                          
    }

    int result       = -1;
    int window_size  = 0;
    void * mmap_area = 0;

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

    if (ioctl(fd, SKFP_IOCSCMODE, &MMAP_MODE) == -1) {
        perror("ioctl (set mode)");
        goto cleanup;
    }
    result = 0;

    mmap_area = mmap(0, window_size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    if (mmap_area == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }

    for (int i = 0; i < window_size; ++i)
    {
        uint8_t* ptr = (uint8_t*)mmap_area + i;
        *ptr = (uint8_t)i;
    }
    printf("%d bytes were written", window_size);

cleanup:

    if (mmap_area && (mmap_area != MAP_FAILED)) {
        munmap(mmap_area, window_size);
        mmap_area = 0;
    }

    if (fd != -1)
        close(fd);
    fd = 0;
    return result;
}
