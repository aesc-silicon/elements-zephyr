/*
 * Copyright (c) 2023 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT elements_spi

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <soc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(DT_DRV_COMPAT);

struct spi_elements_data {
	unsigned int res;
};

struct spi_elements_config {
	uint32_t regs;
};

struct spi_elements_regs {
	unsigned int read_write;
	unsigned int status;
	unsigned int config;
	unsigned int clock_div;
	unsigned int ss_setup;
	unsigned int ss_hold;
	unsigned int ss_disable;
	unsigned int ip;
	unsigned int ie;
};

#define SPI_SS_ENABLE			0x01000000
#define SPI_SS_DISABLE			0x00000000

#define SPI_CMD_DATA			0x00000000
#define SPI_CMD_SS			0x10000000

#define SPI_SS_ENABLE_OFFSET		24
#define SPI_SS_ENABLE_MASK		0xF
#define SPI_SS_SET_ENABLE(_flag_)					     \
	((_flag_) & SPI_SS_ENABLE_MASK << SPI_SS_ENABLE_OFFSET)
#define SPI_SS_INDEX_OFFSET		0
#define SPI_SS_INDEX_MASK		0x00FFFFFF
#define SPI_SS_SET_INDEX(_index_)					     \
	((_index_) & SPI_SS_INDEX_MASK << SPI_SS_INDEX_OFFSET)

#define SPI_DATA_SET_WRITE		0x00000000
#define SPI_DATA_SET_READ		0x01000000

#define SPI_DATA_DATA_OFFSET		0
#define SPI_DATA_DATA_MASK		0x00FFFFFF
#define SPI_DATA_SET_DATA(_data_)					     \
	((_data_) & SPI_DATA_DATA_MASK << SPI_DATA_DATA_OFFSET)

#define SPI_GET_READ_FIFO(_fifo_)					     \
	((_fifo_) >> 16)
#define SPI_GET_WRITE_FIFO(_fifo_)					     \
	((_fifo_) & 0xFFFF)

#define SPI_GET_READ_VALID(_data_)					     \
	((_data_) >> 30)


#define DEV_SPI_CFG(dev)						     \
	((struct spi_elements_config *)(dev)->config)
#define DEV_SPI(dev)							     \
	((struct spi_elements_regs *)(DEV_SPI_CFG(dev))->regs)
#define DEV_SPI_DATA(dev)						     \
	((struct spi_elements_data *)(dev)->driver_data)

#define ELEMENTS_SPI_INIT(no)						     \
	static struct spi_elements_data spi_elements_dev_data_##no;	     \
	static struct spi_elements_config spi_elements_dev_cfg_##no = {	     \
		.regs = DT_REG_ADDR(DT_INST(no, elements_spi)),		     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      spi_elements_init,			     \
			      NULL,					     \
			      &spi_elements_dev_data_##no,		     \
			      &spi_elements_dev_cfg_##no,		     \
			      PRE_KERNEL_1,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      (void *)&spi_elements_driver_api);


/*
 * Supported configurations:
 * - frequency
 * - CPOL
 * - CPHA
 * - CS_ACTIVE_HIGH
 */
static int spi_elements_configure(const struct device *dev,
				  const struct spi_config *config)
{
	volatile struct spi_elements_regs *spi = DEV_SPI(dev);
	unsigned int clock_div;
	unsigned int cfg;

	/* Only operation in master mode allowed */
	if (config->operation & SPI_OP_MODE_SLAVE)
		return -ENOTSUP;
	/* No loop-back available */
	if (config->operation & SPI_MODE_LOOP)
		return -ENOTSUP;
	/* Only MSB transfers are supported */
	if (config->operation & SPI_TRANSFER_LSB)
		return -ENOTSUP;
	/* Controller doesn't support configurable word size */
	if (SPI_WORD_SIZE_GET(config->operation) != 8)
		return -ENOTSUP;
	/* Driver currently doesn't support lock */
	if (config->operation & SPI_LOCK_ON)
		return -ENOTSUP;
	/* Single line only */
	if (config->operation & SPI_LINES_MASK)
		return -ENOTSUP;

	cfg = (SPI_MODE_GET(config->operation) >> 1) & 0x3;
	if (config->operation & SPI_CS_ACTIVE_HIGH)
		cfg |= ~0 << 4;

	if (config->frequency > 0) {
		clock_div = 100000000 / config->frequency / 2;

		spi->clock_div = clock_div;
		spi->ss_setup = clock_div;
		spi->ss_hold = clock_div;
		spi->ss_disable = clock_div;
	}

	return 0;
}

static void spi_elements_ss(const struct device *dev,
			    unsigned int slave_no,
			    volatile unsigned int flag)
{
	volatile struct spi_elements_regs *spi = DEV_SPI(dev);
	unsigned int slave = slave_no + 1;

	spi->read_write = SPI_CMD_SS | flag | SPI_SS_SET_INDEX(slave);
}

static int spi_elements_write(const struct device *dev,
			      const struct spi_buf_set *tx_bufs)
{
	volatile struct spi_elements_regs *spi = DEV_SPI(dev);
	unsigned int cnt = 0;
	unsigned char val;
	int buf_iter;
	int dat_iter;
	const struct spi_buf *buffer;

	if (!tx_bufs)
		goto out;

	for (buf_iter = 0; buf_iter < tx_bufs->count; buf_iter++) {
		buffer = &tx_bufs->buffers[buf_iter];
		for (dat_iter = 0; dat_iter < buffer->len; dat_iter++) {
			/* Wait while FiFo is full */
			while ((spi->status >> 16) == 0)
				;
			val = ((unsigned char *) buffer->buf)[dat_iter];
			uint32_t spiData = val & SPI_DATA_DATA_MASK;
			spiData = spiData << SPI_DATA_DATA_OFFSET;
			spiData = SPI_CMD_DATA | SPI_DATA_SET_WRITE | spiData;
			spi->read_write = spiData;
			cnt++;
		}
	}

out:
	return cnt;
}

static int spi_elements_read(const struct device *dev,
			     const struct spi_buf_set *rx_bufs)
{
	volatile struct spi_elements_regs *spi = DEV_SPI(dev);
	unsigned int cnt = 0;
	unsigned int val;
	unsigned int chr;
	int buf_iter;
	int dat_iter;
	const struct spi_buf *buffer;

	if (!rx_bufs)
		goto out;

	for (buf_iter = 0; buf_iter < rx_bufs->count; buf_iter++) {
		buffer = &rx_bufs->buffers[buf_iter];
		for (dat_iter = 0; dat_iter < buffer->len; dat_iter++) {
			spi->read_write = SPI_CMD_DATA | SPI_DATA_SET_READ;
			do {

				val = spi->read_write;
			} while (!SPI_GET_READ_VALID(val));
			chr = val & 0xFF;
			((unsigned char *)buffer->buf)[dat_iter] = chr;
		}
	}

out:
	return cnt;
}

static int spi_elements_transceive(const struct device *dev,
				   const struct spi_config *config,
				   const struct spi_buf_set *tx_bufs,
				   const struct spi_buf_set *rx_bufs)
{
	unsigned int ret = 0;

	ret = spi_elements_configure(dev, config);
	if (ret)
		goto error;

	spi_elements_ss(dev, config->slave, SPI_SS_ENABLE);
	spi_elements_write(dev, tx_bufs);
	spi_elements_read(dev, rx_bufs);
	spi_elements_ss(dev, config->slave, SPI_SS_DISABLE);

error:
	return ret;
}

static int spi_elements_release(const struct device *dev, const struct spi_config *config)
{
	return 0;
}

int spi_elements_init(const struct device *dev)
{
	return 0;
}

static const struct spi_driver_api spi_elements_driver_api = {
	.transceive = spi_elements_transceive,
	.release = spi_elements_release,
};

DT_INST_FOREACH_STATUS_OKAY(ELEMENTS_SPI_INIT)
