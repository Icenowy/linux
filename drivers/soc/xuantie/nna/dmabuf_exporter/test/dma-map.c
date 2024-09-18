#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <dmabuf_exporter.h>

int main(int argc, char **argv)
{
    int fd;
    int buff_fd;
    unsigned long bufSize;
    void *uptr;
    int i;

    if (argc < 2) {
        printf("%s buffersize\n", argv[0]);
        return 1;
    }
    bufSize = atoi(argv[1]);

    fd = open("/dev/dmabuf", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, DMABUF_IOCTL_CREATE, bufSize)) {
        perror("ioctl DMABUF_IOCTL_CREATE");
        return 1;
    }

    buff_fd = ioctl(fd, DMABUF_IOCTL_EXPORT, 0);
    if (buff_fd < 0) {
        printf("error exporting\n");
        return 1;
    }
    printf("export fd : %d\n", buff_fd);

    uptr = mmap(NULL, bufSize, PROT_READ | PROT_WRITE, MAP_SHARED, buff_fd, 0);
    if (uptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    printf("mapped to %p\n", uptr);

    for (i = 0; getpagesize() * i < bufSize; i++) {
        int *iptr = (int *)((uintptr_t)uptr + getpagesize() * i);
        int val = (0xbeef << 16) | i;
        
        printf("write [%p] = %x\n", iptr, val);
        *iptr = val;
    }
    for (i = 0; getpagesize() * i < bufSize; i++) {
        int *iptr = (int *)((uintptr_t)uptr + getpagesize() * i);
        int val = (0xbeef << 16) | i;

        printf("read [%p] = %x [expected %x]\n", iptr, *iptr, val);
        if (*iptr != val) {
            printf("!!! ERROR !!!\n");
            return 1;
        }
    }
    sleep(3);

    close(buff_fd);
    close(fd);

    return 0;
}
