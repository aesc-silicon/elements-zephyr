/*
 * SPDX-FileCopyrightText: 2026 Aesc Silicon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for Aesc Silicon Programmable IO controller
 * @ingroup pio_aesc_interface
 */

#ifndef ZEPHYR_DRIVERS_MISC_PIO_AESC_PIO_AESC_H_
#define ZEPHYR_DRIVERS_MISC_PIO_AESC_PIO_AESC_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/device.h>

/** Interrupt source bit: RX FIFO has data */
#define PIO_AESC_IRQ_RX_DATA		BIT(0)
/** Interrupt source bit: loop program completed */
#define PIO_AESC_IRQ_LOOP_DONE		BIT(1)

/**
 * @brief Callback invoked from the PIO ISR.
 *
 * @param dev     PIO device instance.
 * @param pending Bitmask of active interrupt sources (PIO_AESC_IRQ_* bits).
 * @param user_data Opaque pointer supplied to pio_aesc_irq_callback_set().
 */
typedef void (*pio_aesc_callback_t)(const struct device *dev, uint32_t pending,
				    void *user_data);

/**
 * @defgroup pio_aesc_interface Aesc Silicon Programmable IO
 * @{
 */

/**
 * @brief PIO instruction set.
 *
 * Single-pin commands select the target IO pin via the @p pin field of
 * struct pio_cmd. Multi-pin (_SET) commands ignore @p pin and treat the
 * @p data field as a bitmask where each bit corresponds to one IO pin.
 */
enum pio_command {
	/* Single-pin commands */
	PIO_CMD_HIGH          = 0x0, /**< Drive pin high (output)               */
	PIO_CMD_LOW           = 0x2, /**< Drive pin low (output)                */
	PIO_CMD_FLOAT         = 0x4, /**< Set pin to high-impedance (input)     */
	PIO_CMD_TOGGLE        = 0x6, /**< Toggle pin direction output <-> input  */
	PIO_CMD_WAIT          = 0x8, /**< Wait @p data clock-divider ticks      */
	PIO_CMD_WAIT_FOR_HIGH = 0x9, /**< Stall until pin reads high            */
	PIO_CMD_WAIT_FOR_LOW  = 0xA, /**< Stall until pin reads low             */
	PIO_CMD_READ          = 0xB, /**< Sample pin after global read-delay    */
	/* Multi-pin commands (data = bitmask) */
	PIO_CMD_HIGH_SET      = 0x1, /**< Drive all masked pins high            */
	PIO_CMD_LOW_SET       = 0x3, /**< Drive all masked pins low             */
	PIO_CMD_FLOAT_SET     = 0x5, /**< Float all masked pins                 */
	PIO_CMD_TOGGLE_SET    = 0x7, /**< Toggle direction of all masked pins   */
	/* Flow control */
	PIO_CMD_LOOP          = 0xC, /**< @p data=0: loop forever, @p data=N: run N times */
};

/**
 * @brief Logical representation of a PIO instruction.
 *
 * Construct an array of these and pass it to pio_aesc_write_program().
 * Use pio_encode_cmd() to manually encode a single instruction into the
 * 32-bit word written to program memory.
 *
 * For single-pin commands, @p pin selects the target IO pin (0-based).
 * For multi-pin (_SET) commands, @p pin is ignored and @p data is a bitmask.
 * For @p PIO_CMD_WAIT, @p data is the number of clock-divider ticks to wait.
 * For @p PIO_CMD_LOOP, @p data=0 loops forever; @p data=N runs the program
 * N times then asserts the loopDone interrupt.
 */
struct pio_cmd {
	enum pio_command command; /**< Instruction opcode                        */
	uint8_t          pin;     /**< Target pin index (single-pin commands)    */
	uint32_t         data;    /**< Tick count, pin bitmask, or loop count    */
};

/**
 * @brief Encode a struct pio_cmd into a 32-bit program memory word.
 *
 * The hardware instruction word layout is:
 * @code
 *   bits [31:8]  data     (24 bits)
 *   bits  [7:4]  pin      (4 bits, supports pins 0-15)
 *   bits  [3:0]  command  (4 bits)
 * @endcode
 *
 * @param cmd Pointer to the logical command to encode. Must not be NULL.
 *
 * @return Encoded 32-bit instruction word ready to write to program memory.
 */
static inline uint32_t pio_encode_cmd(const struct pio_cmd *cmd)
{
	return ((uint32_t)cmd->data << 8)
	     | (((uint32_t)cmd->pin & 0xF) << 4)
	     | ((uint32_t)cmd->command & 0xF);
}

/**
 * @brief Disable the PIO state machine.
 *
 * Stops program execution immediately. The program memory and write pointer
 * are preserved. Call pio_aesc_enable() to resume from the beginning, or
 * pio_aesc_write_program() to load a new program.
 *
 * @param dev PIO device instance.
 */
void pio_aesc_disable(const struct device *dev);

/**
 * @brief Enable the PIO state machine.
 *
 * Starts program execution. The rising edge of the enable signal resets the
 * execution pointer and loop counter to zero. The program must be loaded via
 * pio_aesc_write_program() before calling this function.
 *
 * @param dev PIO device instance.
 */
void pio_aesc_enable(const struct device *dev);

/**
 * @brief Wait for the current program to finish executing.
 *
 * Blocks until the execution pointer reaches the write pointer, indicating
 * all instructions have been executed. Does not disable the state machine
 * or modify the program memory.
 *
 * @param dev PIO device instance.
 */
void pio_aesc_flush(const struct device *dev);

/**
 * @brief Disable the PIO and reset the program memory write pointer.
 *
 * Stops execution immediately and resets the write pointer to zero, allowing
 * a new program to be loaded. The execution pointer is reset on the next call
 * to pio_aesc_enable().
 *
 * Equivalent to pio_aesc_disable() followed by a program memory reset.
 *
 * @param dev PIO device instance.
 */
void pio_aesc_reset(const struct device *dev);

/**
 * @brief Write a single instruction into the program memory.
 *
 * Appends one instruction at the current write pointer position and advances
 * the write pointer. The PIO must be disabled before writing instructions.
 *
 * @param dev PIO device instance.
 * @param cmd Pointer to the instruction to write. Must not be NULL.
 *
 * @retval 0        Success.
 * @retval -ENOBUFS Program memory is full.
 */
int pio_aesc_write_command(const struct device *dev, const struct pio_cmd *cmd);

/**
 * @brief Load a complete program into the program memory.
 *
 * Calls pio_aesc_reset() to disable the state machine and clear the write
 * pointer, then writes @p count instructions sequentially. Call
 * pio_aesc_enable() afterwards to start execution.
 *
 * @param dev   PIO device instance.
 * @param cmds  Array of instructions to write. Must not be NULL.
 * @param count Number of instructions in @p cmds.
 *
 * @retval 0        Success.
 * @retval -ENOBUFS Program exceeds program memory depth.
 */
int pio_aesc_write_program(const struct device *dev,
			   const struct pio_cmd *cmds, size_t count);

/**
 * @brief Set the clock-divider tick frequency at runtime.
 *
 * Computes the clock divider register value as:
 * @code
 *   divider = (sys_clk_freq / hz) - 1
 * @endcode
 *
 * The initial tick frequency is taken from the @p aesc,tick-frequency
 * device tree property during driver initialisation.
 *
 * @param dev PIO device instance.
 * @param hz  Desired tick frequency in Hz. Must be non-zero and must evenly
 *            divide the system clock frequency for accurate timing.
 *
 * @retval 0 Success.
 */
int pio_aesc_set_tick_frequency(const struct device *dev, uint32_t hz);

/**
 * @brief Set the number of clock-divider ticks to wait before sampling
 *        a pin during a READ instruction.
 *
 * Allows the IO line to settle after switching to input mode before the
 * sample is taken. A value of 0 samples the pin immediately.
 *
 * The initial value is taken from the @p aesc,read-delay device tree
 * property during driver initialisation.
 *
 * @param dev   PIO device instance.
 * @param ticks Number of clock-divider ticks to wait (0-255).
 *
 * @retval 0 Success.
 */
int pio_aesc_set_read_delay(const struct device *dev, uint8_t ticks);

/**
 * @brief Pop one result from the RX FIFO.
 *
 * Returns the value sampled by the most recent READ instruction. Each call
 * consumes one entry from the FIFO. Use pio_aesc_rx_available() to check
 * whether data is ready before calling this function.
 *
 * @param dev    PIO device instance.
 * @param result Pointer to store the sampled pin value (0 or 1).
 *               Must not be NULL.
 *
 * @retval 0        Success, @p result contains the sampled value.
 * @retval -ENODATA RX FIFO is empty.
 */
int pio_aesc_read(const struct device *dev, uint8_t *result);

/**
 * @brief Return the number of results currently in the RX FIFO.
 *
 * @param dev PIO device instance.
 *
 * @return Number of unread results available (>= 0).
 */
int pio_aesc_rx_available(const struct device *dev);

/**
 * @brief Enable one or more PIO interrupt sources.
 *
 * @param dev  PIO device instance.
 * @param mask Bitmask of sources to enable (PIO_AESC_IRQ_* bits).
 */
void pio_aesc_irq_enable(const struct device *dev, uint32_t mask);

/**
 * @brief Disable one or more PIO interrupt sources.
 *
 * @param dev  PIO device instance.
 * @param mask Bitmask of sources to disable (PIO_AESC_IRQ_* bits).
 */
void pio_aesc_irq_disable(const struct device *dev, uint32_t mask);

/**
 * @brief Register a callback to be called from the PIO ISR.
 *
 * Replaces any previously registered callback. Pass @p cb = NULL to
 * unregister.
 *
 * @param dev       PIO device instance.
 * @param cb        Callback function, or NULL.
 * @param user_data Opaque pointer passed to @p cb.
 */
void pio_aesc_irq_callback_set(const struct device *dev,
			       pio_aesc_callback_t cb, void *user_data);

/** @} */

#endif /* ZEPHYR_DRIVERS_MISC_PIO_AESC_PIO_AESC_H_ */
