# Copyright (c) 2023 Daniel Schultz
#
# SPDX-License-Identifier: Apache-2.0

'''Runner for flashing with openfpgaloader.'''

from pathlib import Path
import os
import yaml

from west import log

from runners.core import ZephyrBinaryRunner, RunnerCaps, BuildConfiguration

ZEPHYR_ROOT_DIR = Path(__file__).resolve().parent.parent.parent.parent
ZIBAL_ROOT_DIR = ZEPHYR_ROOT_DIR / Path("../modules/elements/zibal/")
ZIBAL_META_DIR = ZIBAL_ROOT_DIR / Path("meta/zephyr/")

class OpenFPGALoaderBinaryRunner(ZephyrBinaryRunner):
    '''Runner front-end for openfpgaloader.'''

    def __init__(self, cfg, sram_only):
        super().__init__(cfg)
        build_conf = BuildConfiguration(cfg.build_dir)
        self.build_dir = build_conf.build_dir
        self.board = build_conf.options['CONFIG_BOARD']
        self.sram_only = sram_only

        self.open_meta()

    def open_meta(self):
        meta_file = ZIBAL_META_DIR / f"{self.board}.yaml"
        with open(meta_file, 'r') as stream:
            try:
                self.meta = yaml.safe_load(stream)
            except yaml.YAMLError as exc:
                log.die(exc)

    @classmethod
    def name(cls):
        return 'openfpgaloader'

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={'flash'})

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument('--sram-only', required=False, action='store_true',
                            help='flashes into the sram only')

    @classmethod
    def do_create(cls, cfg, args):
        return OpenFPGALoaderBinaryRunner(cfg, sram_only=args.sram_only)

    def get_env(self):
        env = os.environ.copy()
        env['FPGA_FAM'] = self.meta['f4pga']['family']
        env['SOC'] = self.meta['soc']
        env['BOARD'] = self.meta['board']
        env['OPENFPGALOADER_BOARD'] = self.meta['board_openfpgaloader']
        env['BUILD_ROOT'] = self.build_dir
        env['FAMILY'] = self.meta['family'].lower()
        env['DEVICE'] = f"{self.meta['device']}_test".lower()
        env['PART'] = self.meta['part'].lower()
        return env

    def do_run(self, command, **kwargs):
        self.logger.info("Board: %s", self.board)

        command = f"{ZIBAL_ROOT_DIR}/eda/f4pga/flash.sh"
        env = self.get_env()
        env['FLASH'] = '' if self.sram_only else '-f'
        self.check_call([command], env=env)

        self.logger.info("Board flashed")
