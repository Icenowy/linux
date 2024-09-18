/*
  User API for dmabuf_exporter
*/

#ifndef _DMABUF_EXPORTER_H
#define _DMABUF_EXPORTER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define DMABUF_IOCTL_BASE   'D'
#define DMABUF_IOCTL_CREATE _IOR(DMABUF_IOCTL_BASE, 0, unsigned long)
#define DMABUF_IOCTL_EXPORT _IOR(DMABUF_IOCTL_BASE, 1, unsigned long)
#ifdef CONFIG_COMPAT
#define COMPAT_DMABUF_IOCTL_CREATE _IOR(DMABUF_IOCTL_BASE, 0, unsigned int)
#define COMPAT_DMABUF_IOCTL_EXPORT _IOR(DMABUF_IOCTL_BASE, 1, unsigned int)
#endif

#endif /* _DMABUF_EXPORTER_H */

