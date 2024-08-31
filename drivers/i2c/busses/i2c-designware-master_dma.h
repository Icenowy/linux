//#include "i2c-designware-core.h"

int i2c_dw_dma_tx_transfer(struct dw_i2c_dev *dev, unsigned int timeout);
int i2c_dw_xfer_dma_init(struct dw_i2c_dev *dev);
int i2c_dw_xfer_dma_deinit(struct dw_i2c_dev *dev);

