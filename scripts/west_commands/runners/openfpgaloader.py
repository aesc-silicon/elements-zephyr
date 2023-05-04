# Copyright (c) 2023 Daniel Schultz
#
# SPDX-License-Identifier: Apache-2.0

'''Runner for flashing with openfpgaloader.'''

from pathlib import Path
import os
import yaml
import shlex

from west import log

from runners.core import ZephyrBinaryRunner, RunnerCaps, BuildConfiguration

ZEPHYR_ROOT_DIR = Path(__file__).resolve().parent.parent.parent.parent
OSS_CAD_BUILD_DIR =  ZEPHYR_ROOT_DIR / Path("../oss-cad-suite/")
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
        env['PATH'] = str(OSS_CAD_BUILD_DIR / "bin") + os.pathsep + env['PATH']
        return env

    def do_run(self, command, **kwargs):
        self.logger.info("Board: %s", self.board)

        board = self.meta['openfpgaloader']['board']
        flash = '' if self.sram_only else '-f'
        bitstream = Path(self.build_dir) / self.meta['soc'] / self.meta['board'] / \
            f"{self.meta['board']}Top.bit"
        command = f"openFPGALoader -b {board} {flash} {bitstream}"

        env = self.get_env()
        self.check_call(shlex.split(command), env=env)

        self.logger.info("Board flashed")
