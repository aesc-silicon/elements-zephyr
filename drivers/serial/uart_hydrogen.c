/*
 * Copyright (c) 2020 Phytec Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <arch/cpu.h>
#include <drivers/uart.h>

#define DEV_UART_IRQ_TX_EN		(1 << 0)
#define DEV_UART_IRQ_RX_EN		(1 << 1)

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
typedef void (*irq_cfg_func_t)(void);
#endif

struct uart_hydrogen_data {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback;
	void *cb_data;
#endif
};

struct uart_hydrogen_config {
	uint32_t regs;
	uint32_t sys_clk_freq;
	uint32_t current_speed;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	irq_cfg_func_t cfg_func;
#endif
};

struct uart_hydrogen_regs {
	unsigned int read_write;
	unsigned int status;
	unsigned int clock_div;
	unsigned int frame_cfg;
	unsigned int ip;
	unsigned int ie;
};

#define DEV_UART_CFG(dev)						     \
	((struct uart_hydrogen_config *)(dev)->config)
#define DEV_UART(dev)							     \
	((struct uart_hydrogen_regs *)(DEV_UART_CFG(dev))->regs)
#define DEV_UART_DATA(dev)						     \
	((struct uart_hydrogen_data *)(dev)->data)

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define UART(no)							     \
	static struct uart_hydrogen_data uart_hydrogen_dev_data_##no;	     \
	static void uart_hydrogen_irq_cfg_func_##no(void);		     \
	static struct uart_hydrogen_config uart_hydrogen_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, hydrogen_uart)),	     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, hydrogen_uart), clock_frequency),\
		.current_speed =					     \
			DT_PROP(DT_INST(no, hydrogen_uart), current_speed),  \
		.cfg_func = uart_hydrogen_irq_cfg_func_##no,		     \
	};								     \
	DEVICE_AND_API_INIT(uart_hydrogen_##no,				     \
			    DT_PROP(DT_INST(no, hydrogen_uart), label),	     \
			    uart_hydrogen_init,				     \
			    &uart_hydrogen_dev_data_##no,		     \
			    &uart_hydrogen_dev_cfg_##no,		     \
			    PRE_KERNEL_1,				     \
			    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		     \
			    (void *)&uart_hydrogen_driver_api);		     \
	static void uart_hydrogen_irq_cfg_func_##no(void) {		     \
	IRQ_CONNECT(RISCV_MAX_GENERIC_IRQ +				     \
			DT_IRQN(DT_INST(no, hydrogen_uart)),		     \
		    0,							     \
		    uart_hydrogen_irq_handler,				     \
		    DEVICE_GET(uart_hydrogen_##no),			     \
		    0);							     \
	irq_enable(RISCV_MAX_GENERIC_IRQ +				     \
			DT_IRQN(DT_INST(no, hydrogen_uart)));		     \
	}
#else
#define UART(no)							     \
	static struct uart_hydrogen_data uart_hydrogen_dev_data_##no;	     \
	static struct uart_hydrogen_config uart_hydrogen_dev_cfg_##no = {    \
		.regs = DT_REG_ADDR(DT_INST(no, hydrogen_uart)),	     \
		.sys_clk_freq =						     \
			DT_PROP(DT_INST(no, hydrogen_uart), clock_frequency),\
		.current_speed =					     \
			DT_PROP(DT_INST(no, hydrogen_uart), current_speed),  \
	};								     \
	DEVICE_AND_API_INIT(uart_hydrogen_##no,				     \
			    DT_PROP(DT_INST(no, hydrogen_uart), label),	     \
			    uart_hydrogen_init,				     \
			    &uart_hydrogen_dev_data_##no,		     \
			    &uart_hydrogen_dev_cfg_##no,		     \
			    PRE_KERNEL_1,				     \
			    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		     \
			    (void *)&uart_hydrogen_driver_api);
#endif


static void uart_hydrogen_poll_out(const struct device *dev, unsigned char c)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	while ((uart->status & 0x00FF0000) == 0);
	uart->read_write = c;
}

static int uart_hydrogen_poll_in(const struct device *dev, unsigned char *c)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);
	int val;

	val = uart->read_write;
	if (val & 0x10000) {
		*c = val & 0xFF;
		return 0;
	}

	return -1;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static int uart_hydrogen_fifo_fill(const struct device *dev,
				const uint8_t *tx_data,
				int size)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);
	int i;

	for (i = 0; i < size && (uart->status & 0x00FF0000); i++)
		uart->read_write = tx_data[i];

	/* Acknowledge TX interrupt */
	uart->ip = DEV_UART_IRQ_TX_EN;

	return i;
}

static int uart_hydrogen_fifo_read(const struct device *dev,
				uint8_t *rx_data,
				const int size)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);
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

static void uart_hydrogen_irq_tx_enable(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	uart->ip |= DEV_UART_IRQ_TX_EN;
	uart->ie |= DEV_UART_IRQ_TX_EN;
}

static void uart_hydrogen_irq_tx_disable(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	uart->ie &= ~DEV_UART_IRQ_TX_EN;
}

static int uart_hydrogen_irq_tx_ready(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	return !!(uart->ip & DEV_UART_IRQ_TX_EN);
}

static int uart_hydrogen_irq_tx_complete(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	/*
	 * No TX EMTPY flag for this controller,
	 * just check if TX FIFO is not full
	 */
	return !(uart->status & 0x00FF0000);
}

static void uart_hydrogen_irq_rx_enable(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	uart->ip |= DEV_UART_IRQ_RX_EN;
	uart->ie |= DEV_UART_IRQ_RX_EN;
}

static void uart_hydrogen_irq_rx_disable(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	uart->ie &= ~DEV_UART_IRQ_RX_EN;
}

static int uart_hydrogen_irq_rx_ready(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	return !!(uart->ip & DEV_UART_IRQ_RX_EN);
}

/* No error interrupt for this controller */
static void uart_hydrogen_irq_err_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void uart_hydrogen_irq_err_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static int uart_hydrogen_irq_is_pending(const struct device *dev)
{
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	return !!(uart->ip & (DEV_UART_IRQ_TX_EN | DEV_UART_IRQ_RX_EN));
}

static int uart_hydrogen_irq_update(const struct device *dev)
{
/*
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	uart->ip = DEV_UART_IRQ_TX_EN | DEV_UART_IRQ_RX_EN;
*/
	return 1;
}

static void uart_hydrogen_irq_callback_set(const struct device *dev,
					   uart_irq_callback_user_data_t cb,
					   void *cb_data)
{
	struct uart_hydrogen_data *data = DEV_UART_DATA(dev);

	data->callback = cb;
	data->cb_data = cb_data;
}

static void uart_hydrogen_irq_handler(const void *arg)
{
	struct device *dev = (struct device *)arg;
	struct uart_hydrogen_data *data = DEV_UART_DATA(dev);

	if (data->callback)
		data->callback(dev, data->cb_data);
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static int uart_hydrogen_init(const struct device *dev)
{
	volatile struct uart_hydrogen_config *cfg = DEV_UART_CFG(dev);
	volatile struct uart_hydrogen_regs *uart = DEV_UART(dev);

	uart->clock_div = cfg->sys_clk_freq / cfg->current_speed / 8;
	uart->frame_cfg = 7;

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart->ie = 0U;
	uart->ip = DEV_UART_IRQ_TX_EN | DEV_UART_IRQ_RX_EN;
	cfg->cfg_func();
#endif

	return 0;
}


static const struct uart_driver_api uart_hydrogen_driver_api = {
	.poll_in          = uart_hydrogen_poll_in,
	.poll_out         = uart_hydrogen_poll_out,
	.err_check        = NULL,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill        = uart_hydrogen_fifo_fill,
	.fifo_read        = uart_hydrogen_fifo_read,
	.irq_tx_enable    = uart_hydrogen_irq_tx_enable,
	.irq_tx_disable   = uart_hydrogen_irq_tx_disable,
	.irq_tx_ready     = uart_hydrogen_irq_tx_ready,
	.irq_tx_complete  = uart_hydrogen_irq_tx_complete,
	.irq_rx_enable    = uart_hydrogen_irq_rx_enable,
	.irq_rx_disable   = uart_hydrogen_irq_rx_disable,
	.irq_rx_ready     = uart_hydrogen_irq_rx_ready,
	.irq_err_enable   = uart_hydrogen_irq_err_enable,
	.irq_err_disable  = uart_hydrogen_irq_err_disable,
	.irq_is_pending   = uart_hydrogen_irq_is_pending,
	.irq_update       = uart_hydrogen_irq_update,
	.irq_callback_set = uart_hydrogen_irq_callback_set,
#endif
};


#if DT_NODE_EXISTS(DT_INST(0, hydrogen_uart))
	UART(0)
#endif
#if DT_NODE_EXISTS(DT_INST(1, hydrogen_uart))
	UART(1)
#endif
#if DT_NODE_EXISTS(DT_INST(2, hydrogen_uart))
	UART(2)
#endif
#if DT_NODE_EXISTS(DT_INST(3, hydrogen_uart))
	UART(3)
#endif
#if DT_NODE_EXISTS(DT_INST(4, hydrogen_uart))
	UART(4)
#endif
