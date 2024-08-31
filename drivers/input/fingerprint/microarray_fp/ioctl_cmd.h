/* Copyright (C) MicroArray
 * MicroArray Fprint Driver Code 
 * ioctl_cmd.h
 * Date: 2017-3-15
 * Version: v4.0.06
 * Author: guq
 * Contact: guq@microarray.com.cn
 */
#ifndef __IOCTL_CMD_H__
#define __IOCTL_CMD_H__

#define MA_DRV_VERSION	    (0x00004006)

#define MA_IOC_MAGIC            'M'
//#define MA_IOC_INIT           _IOR(MA_IOC_MAGIC, 0, unsigned char)
#define TIMEOUT_WAKELOCK        _IO(MA_IOC_MAGIC, 1)
#define SLEEP                   _IO(MA_IOC_MAGIC, 2)    //陷入内核
#define WAKEUP                  _IO(MA_IOC_MAGIC, 3)    //唤醒
#define ENABLE_CLK              _IO(MA_IOC_MAGIC, 4)    //打开spi时钟
#define DISABLE_CLK             _IO(MA_IOC_MAGIC, 5)    //关闭spi时钟
#define ENABLE_INTERRUPT        _IO(MA_IOC_MAGIC, 6)    //开启中断上报
#define DISABLE_INTERRUPT       _IO(MA_IOC_MAGIC, 7)    //关闭中断上报
#define TAP_DOWN                _IO(MA_IOC_MAGIC, 8)
#define TAP_UP                  _IO(MA_IOC_MAGIC, 9)
#define SINGLE_TAP              _IO(MA_IOC_MAGIC, 11)
#define DOUBLE_TAP              _IO(MA_IOC_MAGIC, 12)
#define LONG_TAP                _IO(MA_IOC_MAGIC, 13)

#define MA_IOC_VTIM             _IOR(MA_IOC_MAGIC,  14, unsigned char)     //version time
#define MA_IOC_CNUM             _IOR(MA_IOC_MAGIC,  15, unsigned char)     //cover num
#define MA_IOC_SNUM             _IOR(MA_IOC_MAGIC,  16, unsigned char)     //sensor type
#define MA_IOC_UKRP             _IOW(MA_IOC_MAGIC,  17, unsigned char)     //user define the report key

#define MA_KEY_UP/*KEY_UP*/                  _IO(MA_IOC_MAGIC,  18)                  //nav up
#define MA_KEY_LEFT/*KEY_LEFT*/                _IO(MA_IOC_MAGIC,  19)                  //nav left
#define MA_KEY_DOWN/*KEY_DOWN*/                _IO(MA_IOC_MAGIC,  20)                  //nav down
#define MA_KEY_RIGHT/*KEY_RIGHT*/               _IO(MA_IOC_MAGIC,  21)                  //nav right

#define MA_KEY_F14/*KEY_F14*/                 _IO(MA_IOC_MAGIC,  23)  //for chuanyin
#define SET_MODE                _IOW(MA_IOC_MAGIC, 33, unsigned int)    //for yude
#define GET_MODE                _IOR(MA_IOC_MAGIC, 34, unsigned int)    //for yude


#define ENABLE_IRQ/*ENABLE_IQ*/               _IO(MA_IOC_MAGIC, 31)
#define DISABLE_IRQ/*DISABLE_IQ*/              _IO(MA_IOC_MAGIC, 32)

#define MA_IOC_GVER             _IOR(MA_IOC_MAGIC,   35, unsigned int)      //get the driver version,the version mapping in the u32 is the final  4+4+8,as ******** ******* ****(major verson number) ****(minor version number) ********(revised version number), the front 16 byte is reserved.
#define SCREEN_OFF              _IO(MA_IOC_MAGIC,    36)
#define SCREEN_ON               _IO(MA_IOC_MAGIC,    37)
#define SET_SPI_SPEED           _IOW(MA_IOC_MAGIC,   38, unsigned int)


#define WAIT_FACTORY_CMD        _IO(MA_IOC_MAGIC,    39)//for fingerprintd
#define WAKEUP_FINGERPRINTD     _IO(MA_IOC_MAGIC,    40)//for factory test
#define WAIT_FINGERPRINTD_RESPONSE                                  _IOR(MA_IOC_MAGIC,    41, unsigned int)//for factory test
#define WAKEUP_FACTORY_TEST_SEND_FINGERPRINTD_RESPONSE              _IOW(MA_IOC_MAGIC,    42, unsigned int)//for fingerprintd
#define WAIT_SCREEN_STATUS_CHANGE                                   _IOR(MA_IOC_MAGIC,    43, unsigned int)
#define GET_INTERRUPT_STATUS                                        _IOR(MA_IOC_MAGIC,    44, unsigned int)
#define SYNC					_IO(MA_IOC_MAGIC, 45)
#define SYNC2					_IO(MA_IOC_MAGIC, 46)
#define GET_SCREEN_STATUS		_IOR(MA_IOC_MAGIC, 47, unsigned int)
#define MA_RESET                _IOW(MA_IOC_MAGIC, 48, unsigned int)
#define GET_MA_RESET_STATE      _IOR(MA_IOC_MAGIC, 49, unsigned int)
#endif /* __IOCTL_CMD_H__ */

