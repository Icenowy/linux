#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include "i2c-designware-core.h"

#define DEBUG

#define DW_IC_DMA_CR                (0x88)
#define DW_IC_DMA_TDLR              (0x8c)
#define DW_IC_DMA_RDLR              (0x90)
#define DW_IC_DMA_CR_TXEN           (0x2)
#define DW_IC_DMA_CR_DIS            (0x0)

//Because the fifo register bit width is 32bits, each transfer data is 4byte
#define DW_IC_DMA_DATA_BLOCK_BYTES  (0x4)

//#define __dev_vdgb dev_dbg
#define __dev_vdgb(fmt, ...)

static int i2c_dw_hwparams_to_dma_slave_config(struct dw_i2c_dev *dev)
{
    int ret = 0;
    struct dma_slave_config slave_config;
    struct i2c_dw_dma *dma = &dev->dma;
    struct platform_device *pdev;
    struct resource *iores_mem;
    phys_addr_t reg_addr;

    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);
    memset(&slave_config, 0, sizeof(slave_config));

    //get i2c fifo addr
    pdev = to_platform_device(dev->dev);
    iores_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    reg_addr = iores_mem->start + DW_IC_DATA_CMD;
    slave_config.direction = DMA_MEM_TO_DEV;
    slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
    slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
    slave_config.dst_addr = reg_addr;
    //for light only support "false"
    slave_config.device_fc = false;

    ret = dmaengine_slave_config(dma->dma_chan, &slave_config);
    if (ret) {
        dev_err(dev->dev, "dmaengine_slave_config failed\n");
        return ret;
    }

    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return 0;
}

static void i2c_dw_dma_callback(void *data)
{
    struct dw_i2c_dev *dev = (struct dw_i2c_dev *)data;
    struct i2c_dw_dma *dma = &dev->dma;

    dev->tx_status &= ~STATUS_WRITE_IN_PROGRESS;
    complete(&dma->dma_complete);
    dmaengine_terminate_async(dma->dma_chan);
}

static int get_msg_size(struct dw_i2c_dev *dev)
{
    struct i2c_msg *msgs = dev->msgs;
    u32 addr = msgs[dev->msg_write_idx].addr;
    int i = 0;
    int len = 0;

    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);
    for (i = dev->msg_write_idx; i < dev->msgs_num; i++) {
        if (msgs[i].addr != addr) {
            dev_err(dev->dev, "%s: invalid target address\n", __func__);
            dev->msg_err = -EINVAL;
            break;
        }

        len += msgs[i].len;
    }

    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return len;
}

static int alloc_dma_buf(struct dw_i2c_dev *dev, int size)
{
    struct i2c_dw_dma *dma =  &dev->dma;
    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);

    dma->buf = dma_alloc_coherent(dev->dev, size, &dma->dma_addr, GFP_KERNEL);
    if (!dma->buf) {
        dev_err(dev->dev, "i2c alloc dma buf failed\n");
        return -1;
    }

    dma->buf_size = size;
    dma->transfer_len = 0;
    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return 0;
}

static int i2c_dw_release_tx_packets(struct dw_i2c_dev *dev)
{
    struct i2c_dw_dma *dma =  &dev->dma;

    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);

    if(dma->buf && dma->dma_addr && dma->buf_size) {
        dma_free_coherent(dev->dev, dma->buf_size, dma->buf, dma->dma_addr);
    }

    dma->buf = 0;
    dma->dma_addr = 0;
    dma->buf_size = 0;

    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return 0;
}

static int i2c_dw_synthetic_tx_packets(struct dw_i2c_dev *dev)
{
    int ret = 0;
    struct i2c_dw_dma *dma =  &dev->dma;
    struct i2c_msg *msgs = dev->msgs;
    bool need_restart = false;
    uint32_t *tx_buf;
    int dma_tx_buf_size;
    u32 addr = msgs[dev->msg_write_idx].addr;

    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);

    dma_tx_buf_size = get_msg_size(dev) * 4;
    if (dma_tx_buf_size <= 0) {
        dev_err(dev->dev, "i2c get_msg_size size is error %d\n", dma_tx_buf_size);
        return -1;
    }

    ret = alloc_dma_buf(dev, dma_tx_buf_size);
    if (ret) {
        dev_err(dev->dev, "i2c alloc_dma_buf failed\n");
        return -1;
    }

    tx_buf = dma->buf;

    for (; dev->msg_write_idx < dev->msgs_num; dev->msg_write_idx++) {
        unsigned char *buf;
        int buf_len;
        u32 flags = msgs[dev->msg_write_idx].flags;

        if (msgs[dev->msg_write_idx].addr != addr) {
            dev_err(dev->dev, "%s: invalid target address\n", __func__);
            dev->msg_err = -EINVAL;
            break;
        }

        /* new i2c_msg */
        buf = msgs[dev->msg_write_idx].buf;
        buf_len = msgs[dev->msg_write_idx].len;

        if ((dev->master_cfg & DW_IC_CON_RESTART_EN) &&
                (dev->msg_write_idx > 0))
            need_restart = true;

        //dev_err(dev->dev, "   msg_buf_len %d\n", buf_len);
        while (buf_len > 0) {
            u32 cmd = 0;

            if (dev->msg_write_idx == dev->msgs_num - 1 &&
                buf_len == 1 && !(flags & I2C_M_RECV_LEN))
                cmd |= BIT(9);

            if (need_restart) {
                cmd |= BIT(10);
                need_restart = false;
            }

            if (msgs[dev->msg_write_idx].flags & I2C_M_RD) {
                *tx_buf = cmd | 0x100;
                dev->rx_outstanding++;
            } else {
                *tx_buf = cmd | *buf++;
            }
            tx_buf++;
            dma->transfer_len++;
            buf_len--;
        }
    }

    dma_sync_single_for_device(dev->dev, dma->dma_addr, dma->buf_size, DMA_TO_DEVICE);

    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return 0;
}

int i2c_dw_xfer_dma_deinit(struct dw_i2c_dev *dev);

int i2c_dw_dma_tx_transfer(struct dw_i2c_dev *dev, unsigned int timeout)
{
    int ret = 0;
    struct i2c_dw_dma *dma = &dev->dma;
    u32 stat;

    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);
    if (dma->dma_chan == NULL) {
        goto error;
    }

    ret = i2c_dw_hwparams_to_dma_slave_config(dev);
    if (ret != 0) {
        goto error;
    }

    regmap_read(dev->map, DW_IC_RAW_INTR_STAT, &stat);
    if (stat & DW_IC_INTR_TX_ABRT) {
        dev->cmd_err |= DW_IC_ERR_TX_ABRT;
        dev->tx_status = 0;
        goto error;
    }

    ret = i2c_dw_synthetic_tx_packets(dev);
    if (ret != 0) {
        dev_err(dev->dev, "%s: i2c_dw_synthetic_tx_packets failed\n", __func__);
        goto error;
    }

    dev->tx_status |= STATUS_WRITE_IN_PROGRESS;
    dma->desc = dmaengine_prep_slave_single(dma->dma_chan, dma->dma_addr,
                                               dma->transfer_len * DW_IC_DMA_DATA_BLOCK_BYTES,
                                               DMA_MEM_TO_DEV,
                                               DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
    if (dma->desc == NULL) {
        dev_err(dev->dev, "%s: dmaengine_prep_slave_single failed\n", __func__);
        goto error;
    }

    dma->desc->callback = i2c_dw_dma_callback;
    dma->desc->callback_param = dev;
    dma->cookie = dmaengine_submit(dma->desc);
    dma_async_issue_pending(dma->dma_chan);

    //wait dma transfer complete
    if (!wait_for_completion_timeout(&dma->dma_complete, timeout)) {
        dev_err(dev->dev, "i2c dma_ch%d write timed out\n", dma->dma_chan->chan_id);
        ret = dmaengine_terminate_async(dma->dma_chan);
        if (ret !=0) {
            dev_err(dev->dev, "i2c dma dmaengine_terminate_async failed,\
                               dma_ch is %d\n", dma->dma_chan->chan_id);
        }
        dmaengine_synchronize(dma->dma_chan);
        goto error;
    }

    dmaengine_synchronize(dma->dma_chan);

    //Release the dma channel immediately after the dma transfer is completed,
    //reducing the dma occupation time
    //i2c_dw_xfer_dma_init(dev);
    return 0;

error:
    //i2c_dw_xfer_dma_init(dev);
    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return -1;
}

static int i2x_dw_get_read_size(struct dw_i2c_dev *dev)
{
    struct i2c_msg *msgs = dev->msgs;
    u32 addr = msgs[dev->msg_write_idx].addr;
    int i = 0;
    int len = 0;
    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);
    for (i = dev->msg_write_idx; i < dev->msgs_num; i++) {
        if (msgs[i].addr != addr) {
            dev_err(dev->dev, "%s: invalid target address\n", __func__);
            dev->msg_err = -EINVAL;
            break;
        }
        if(msgs[i].flags & I2C_M_RD)
            len += msgs[i].len;
    }
    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return len;
}
int i2c_dw_xfer_dma_init(struct dw_i2c_dev *dev)
{
    struct i2c_dw_dma *dma = &dev->dma;
    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);

    // i2c dma set tx data level
    regmap_write(dev->map, DW_IC_DMA_TDLR, dev->tx_fifo_depth / 2);

    if(i2x_dw_get_read_size(dev)>(dev->rx_fifo_depth / 2))
	    regmap_write(dev->map, DW_IC_RX_TL,  dev->rx_fifo_depth / 2);

    // i2c dma tx enable
    regmap_write(dev->map, DW_IC_DMA_CR, DW_IC_DMA_CR_TXEN);

    if (dma->dma_chan == NULL) {
        //Alloc i2c dma channel.
        //The function is to configure the handshake number in i2c dts into the channel
		//Try to obtain the dma channel within 10 seconds
		int i = 2000;
		while(i--) {
			dma->dma_chan = dma_request_slave_channel(dev->dev, "tx");
			if (dma->dma_chan) {
				break;
			}
			msleep(5);
		}

		if (!dma->dma_chan) {
			dev_err(dev->dev, "Failed to request dma channel");
			return -EIO;
        }

        __dev_vdgb(dev->dev,"i2c request dma_ch %d\n", dma->dma_chan->chan_id);
    }

    __dev_vdgb(dev->dev,"i2c dma_ch %d\n", dma->dma_chan->chan_id);
    reinit_completion(&dma->dma_complete);
    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);
    return 0;
}

int i2c_dw_xfer_dma_deinit(struct dw_i2c_dev *dev)
{
    struct i2c_dw_dma *dma = &dev->dma;

    __dev_vdgb(dev->dev, "%s, %d, enter\n", __func__, __LINE__);

    i2c_dw_release_tx_packets(dev);
    if (dma->dma_chan != NULL) {
        dma_release_channel(dma->dma_chan);
    }
    dma->dma_chan = NULL;

    // i2c dma disable
    regmap_write(dev->map, DW_IC_DMA_CR, DW_IC_DMA_CR_DIS);
    __dev_vdgb(dev->dev, "%s, %d, exit\n", __func__, __LINE__);

    return 0;
}

