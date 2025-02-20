.. _elemrv-n:

Overview
********

ElemRV-N is an end-to-end open-source RISC-V microcontroller designed using SpinalHDL.

Version 0.2 of ElemRV-N was successfully fabricated using `IHP's Open PDK`, a 130nm open semiconductor process, with support from `FMD_QNC`.

For more details, refer to the official `GitHub Project`.

.. note::
   The currently supported silicon version is ElemRV-N 0.2.


Supported Features
******************

.. zephyr:board-supported-hw::

Memory Map
==========

The table below describes the system's AXI4 crossbar memory map.

+--------------+-------------+-------------------------------+
| Base Address | Name        | Description                   |
+--------------+-------------+-------------------------------+
| 0x80000000   | On-Chip RAM | 1kB on-chip sram              |
+--------------+-------------+-------------------------------+
| 0x90000000   | Hyperbus    | External HyperRAM             |
+--------------+-------------+-------------------------------+
| 0xA0000000   | XIP Flash   | External SPI NOR Flash        |
+--------------+-------------+-------------------------------+
| 0xF0000000   | Peripherals | APB3 bus with all peripherals |
+--------------+-------------+-------------------------------+

Below is a list of peripherals accessible via the APB3 bus:

+--------------+------+-------------------------------------+
| Base Address | Size |  Name                               |
+--------------+------+-------------------------------------+
| 0xF0800000   | 4 MB | Platform Level Interrupt Controller |
+--------------+------+-------------------------------------+
| 0xF0020000   | 4 kB | Mtimer                              |
+--------------+------+-------------------------------------+
| 0xF0021000   | 4 kB | Reset Controller                    |
+--------------+------+-------------------------------------+
| 0xF0022000   | 4 kB | Clock Controller                    |
+--------------+------+-------------------------------------+
| 0xF0023000   | 4 kB | Hyperbus Controller                 |
+--------------+------+-------------------------------------+
| 0xF0024000   | 4 kB | XPI Flash Controller                |
+--------------+------+-------------------------------------+
| 0xF0000000   | 4 kB | GPIO0 Controller                    |
+--------------+------+-------------------------------------+
| 0xF0001000   | 4 kB | I2C0 Controller                     |
+--------------+------+-------------------------------------+
| 0xF0002000   | 4 kB | I2C1 Controller                     |
+--------------+------+-------------------------------------+
| 0xF0003000   | 4 kB | PIO0 Controller                     |
+--------------+------+-------------------------------------+
| 0xF0004000   | 4 kB | PWM0 Controller                     |
+--------------+------+-------------------------------------+
| 0xF0005000   | 4 kB | SPI0 Controller                     |
+--------------+------+-------------------------------------+
| 0xF0006000   | 4 kB | UART0 Controller with Handshake     |
+--------------+------+-------------------------------------+
| 0xF0007000   | 4 kB | UART1 Controller                    |
+--------------+------+-------------------------------------+
| 0xF0030000   | 4 kB | AES Accelerator                     |
+--------------+------+-------------------------------------+
| 0xF0010000   | 4 kB | Pinmux Controller                   |
+--------------+------+-------------------------------------+


System Clock
============

The system clock for the RISC-V core is set to 20 MHz. This value is specified in the `cpu0` devicetree node using the `clock-frequency` property.


CPU
===

ElemRV-N integrates a VexRiscv RISC-V core featuring a 5-stage pipeline and the following ISA extensions:

* M (Integer Multiply/Divide)
* C (Compressed Instructions)

It also includes the following general-purpose `Z` extensions:

* Zicntr – Base Counter and Timer extensions
* Zicsr – Control and Status Register operations
* Zifencei – Instruction-fetch fence

The complete ISA string for this CPU is: ``RV32IMC_Zicntr_Zicsr_Zifencei``


Hart-Level Interrupt Controller (HLIC)
======================================

Each CPU core is equipped with a Hart-Level Interrupt Controller, configurable through Control and Status Registers (CSRs).


Machine Timer
=============

A RISC-V compliant machine timer is enabled by default and is connected to interrupt line 7 on the HLIC for `cpu0`.


.. _GitHub Project:
   https://github.com/aesc-silicon/elemrv

.. _IHP's Open PDK:
   https://github.com/IHP-GmbH/IHP-Open-PDK

.. _FMD-QNC:
   https://www.elektronikforschung.de/projekte/fmd-qnc
