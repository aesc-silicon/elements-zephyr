/*
 * Copyright (c) 2023 aesc silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT elements_uart

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/drivers/uart.h>
#include <soc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_elements, CONFIG_UART_LOG_LEVEL);

#define DEV_UART_IRQ_TX_EN		(1 << 0)
#define DEV_UART_IRQ_RX_EN		(1 << 1)

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
typedef void (*elements_cfg_func_t)(void);
#endif

struct uart_elements_data {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback;
	void *cb_data;
#endif
};

struct uart_elements_config {
	uint32_t regs;
	uint32_t sys_clk_freq;
	uint32_t current_speed;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	elements_cfg_func_t cfg_func;
	uint32_t irq_no;
#endif
};

struct uart_elements_regs {
	unsigned int read_write;
	unsigned int status;
	unsigned int clock_div;
	unsigned int frame_cfg;
	unsigned int ip;
	unsigned int ie;
};

#define DEV_UART_CFG(dev)						     \
	((struct uart_elements_config *)(dev)->config)
#define DEV_UART(dev)							     \
	((struct uart_elements_regs *)(DEV_UART_CFG(dev))->regs)
#define DEV_UART_DATA(dev)						     \
	((struct uart_elements_data *)(dev)->data)

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define ELEMENTS_UART_INIT(no)						     \
	static struct uart_elements_data uart_elements_dev_data_##no;	     \
	static void uart_elements_irq_cfg_func_##no(void);		     \
	static struct uart_elements_config uart_elements_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, elements_uart)),	     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, elements_uart), clock_frequency),\
		.current_speed =					     \
			DT_PROP(DT_INST(no, elements_uart), current_speed),  \
		.cfg_func = uart_elements_irq_cfg_func_##no,		     \
		.irq_no = DT_IRQN(DT_INST(no, elements_uart)),		     \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      uart_elements_init,			     \
			      NULL,					     \
			      &uart_elements_dev_data_##no,		     \
			      &uart_elements_dev_cfg_##no,		     \
			      PRE_KERNEL_1,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      (void *)&uart_elements_driver_api);	     \
	static void uart_elements_irq_cfg_func_##no(void) {		     \
	IRQ_CONNECT(CONFIG_2ND_LVL_ISR_TBL_OFFSET +			     \
		    DT_IRQN(DT_INST(no, elements_uart)),		     \
		    0,							     \
		    uart_elements_irq_handler,				     \
		    DEVICE_DT_INST_GET(no),				     \
		    0);							     \
	}
#else
#define ELEMENTS_UART_INIT(no)						     \
	static struct uart_elements_data uart_elements_dev_data_##no;	     \
	static struct uart_elements_config uart_elements_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, elements_uart)),	     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, elements_uart), clock_frequency),\
		.current_speed =					     \
			DT_PROP(DT_INST(no, elements_uart), current_speed),  \
	};								     \
	DEVICE_DT_INST_DEFINE(no,					     \
			      uart_elements_init,			     \
			      NULL,					     \
			      &uart_elements_dev_data_##no,		     \
			      &uart_elements_dev_cfg_##no,		     \
			      PRE_KERNEL_1,				     \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	     \
			      (void *)&uart_elements_driver_api);
#endif


static void uart_elements_poll_out(const struct device *dev, unsigned char c)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	while ((uart->status & 0x00FF0000) == 0);
	uart->read_write = c;
}

static int uart_elements_poll_in(const struct device *dev, unsigned char *c)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	int val;

	val = uart->read_write;
	if (val & 0x10000) {
		*c = val & 0xFF;
		return 0;
	}

	return -1;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static int uart_elements_fifo_fill(const struct device *dev,
				const uint8_t *tx_data,
				int size)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	int i;

	for (i = 0; i < size && (uart->status & 0x00FF0000); i++)
		uart->read_write = tx_data[i];

	/* Acknowledge TX interrupt */
	uart->ip = DEV_UART_IRQ_TX_EN;

	return i;
}

static int uart_elements_fifo_read(const struct device *dev,
				uint8_t *rx_data,
				const int size)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	int i;
	uint32_t val;

	for (i = 0; i < size; i++) {
		val = uart->read_write;

		if (!(val & 0x10000))
			break;

		rx_data[i] = val & 0xFF;
	}

	/* Acknowledge RX interrupt */
	uart->ip = DEV_UART_IRQ_RX_EN;

	return i;
}

static void uart_elements_irq_tx_enable(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	volatile struct uart_elements_config *cfg = DEV_UART_CFG(dev);

	uart->ip |= DEV_UART_IRQ_TX_EN;
	uart->ie |= DEV_UART_IRQ_TX_EN;
	irq_enable(irq_to_level_2(cfg->irq_no));
}

static void uart_elements_irq_tx_disable(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	volatile struct uart_elements_config *cfg = DEV_UART_CFG(dev);

	uart->ie &= ~DEV_UART_IRQ_TX_EN;
	if (!uart->ie)
		irq_disable(irq_to_level_2(cfg->irq_no));
}

static int uart_elements_irq_tx_ready(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	return !!(uart->ip & DEV_UART_IRQ_TX_EN);
}

static int uart_elements_irq_tx_complete(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	/*
	 * No TX EMTPY flag for this controller,
	 * just check if TX FIFO is not full
	 */
	return !(uart->status & 0x00FF0000);
}

static void uart_elements_irq_rx_enable(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	volatile struct uart_elements_config *cfg = DEV_UART_CFG(dev);

	uart->ip |= DEV_UART_IRQ_RX_EN;
	uart->ie |= DEV_UART_IRQ_RX_EN;
	irq_enable(irq_to_level_2(cfg->irq_no));
}

static void uart_elements_irq_rx_disable(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);
	volatile struct uart_elements_config *cfg = DEV_UART_CFG(dev);

	uart->ie &= ~DEV_UART_IRQ_RX_EN;
	if (!uart->ie)
		irq_disable(irq_to_level_2(cfg->irq_no));
}

static int uart_elements_irq_rx_ready(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	return !!(uart->ip & DEV_UART_IRQ_RX_EN);
}

/* No error interrupt for this controller */
static void uart_elements_irq_err_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void uart_elements_irq_err_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static int uart_elements_irq_is_pending(const struct device *dev)
{
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	return !!(uart->ip & (DEV_UART_IRQ_TX_EN | DEV_UART_IRQ_RX_EN));
}

static int uart_elements_irq_update(const struct device *dev)
{
/*
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	uart->ip = DEV_UART_IRQ_TX_EN | DEV_UART_IRQ_RX_EN;
*/
	return 1;
}

static void uart_elements_irq_callback_set(const struct device *dev,
					   uart_irq_callback_user_data_t cb,
					   void *cb_data)
{
	struct uart_elements_data *data = DEV_UART_DATA(dev);

	data->callback = cb;
	data->cb_data = cb_data;
}

static void uart_elements_irq_handler(const void *arg)
{
	struct device *dev = (struct device *)arg;
	struct uart_elements_data *data = DEV_UART_DATA(dev);

	if (data->callback)
		data->callback(dev, data->cb_data);
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static int uart_elements_init(const struct device *dev)
{
	volatile struct uart_elements_config *cfg = DEV_UART_CFG(dev);
	volatile struct uart_elements_regs *uart = DEV_UART(dev);

	uart->clock_div = cfg->sys_clk_freq / cfg->current_speed / 8;
	uart->frame_cfg = 7;

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart->ie = 0U;
	uart->ip = DEV_UART_IRQ_TX_EN | DEV_UART_IRQ_RX_EN;
	cfg->cfg_func();
#endif

	return 0;
}


static const struct uart_driver_api uart_elements_driver_api = {
	.poll_in          = uart_elements_poll_in,
	.poll_out         = uart_elements_poll_out,
	.err_check        = NULL,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill        = uart_elements_fifo_fill,
	.fifo_read        = uart_elements_fifo_read,
	.irq_tx_enable    = uart_elements_irq_tx_enable,
	.irq_tx_disable   = uart_elements_irq_tx_disable,
	.irq_tx_ready     = uart_elements_irq_tx_ready,
	.irq_tx_complete  = uart_elements_irq_tx_complete,
	.irq_rx_enable    = uart_elements_irq_rx_enable,
	.irq_rx_disable   = uart_elements_irq_rx_disable,
	.irq_rx_ready     = uart_elements_irq_rx_ready,
	.irq_err_enable   = uart_elements_irq_err_enable,
	.irq_err_disable  = uart_elements_irq_err_disable,
	.irq_is_pending   = uart_elements_irq_is_pending,
	.irq_update       = uart_elements_irq_update,
	.irq_callback_set = uart_elements_irq_callback_set,
#endif
};


DT_INST_FOREACH_STATUS_OKAY(ELEMENTS_UART_INIT)
