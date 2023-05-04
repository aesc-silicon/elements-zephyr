# Copyright (c) 2023 Daniel Schultz
#
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import subprocess
import shlex
import json
import yaml
import checksumdir

from west import log

ZEPHYR_ROOT_DIR = Path(__file__).resolve().parent.parent.parent
ELEMENTS_ROOT_DIR = ZEPHYR_ROOT_DIR / Path("../modules/elements/")
ZIBAL_ROOT_DIR = ELEMENTS_ROOT_DIR / Path("zibal/")
NAFARR_ROOT_DIR = ELEMENTS_ROOT_DIR / Path("nafarr/")
ZIBAL_META_DIR = ZIBAL_ROOT_DIR / Path("meta/zephyr/")
F4PGA_ROOT_DIR = ZEPHYR_ROOT_DIR / Path("../f4pga/")
OSS_CAD_BUILD_DIR =  ZEPHYR_ROOT_DIR / Path("../oss-cad-suite/")
NEXTPNR_XILINX_ROOT_DIR = ZEPHYR_ROOT_DIR / Path("../nextpnr-xilinx/")


class Elements:

    def __init__(self, build_dir, board, dry_run):
        self.build_dir = build_dir
        self.board = board
        self.dry_run = dry_run

        self.open_meta()

    def _log_cmd(self, cmd: str):
        if not self.dry_run:
            log.dbg(cmd)
        else:
            log.inf(cmd)

    def check_call(self, cmd: str, env, cwd):
        '''Subclass subprocess.check_call() wrapper.

        Subclasses should use this method to run command in a
        subprocess and check that it executed correctly, rather than
        using subprocess directly, to keep accurate debug logs.
        '''
        self._log_cmd(cmd)
        if self.dry_run:
            return
        subprocess.check_call(shlex.split(cmd), env=env, cwd=cwd)

    def open_meta(self):
        meta_file = ZIBAL_META_DIR / f"{self.board}.yaml"
        with open(meta_file, 'r') as stream:
            try:
                self.meta = yaml.safe_load(stream)
            except yaml.YAMLError as exc:
                log.die(exc)

    def get_env(self):
        env = os.environ.copy()
        env['SOC'] = self.meta['soc']
        env['BOARD'] = self.meta['board']
        env['PART'] = self.meta['part'].lower()
        if 'fpga' in self.meta:
            env['FPGA_VENDOR'] = str(self.meta['fpga'].get('vendor', None))
            env['FPGA_ARCH'] = str(self.meta['fpga'].get('arch', None))
            env['FPGA_FAMILY'] = str(self.meta['fpga'].get('family', None))
            env['FPGA_DEVICE'] = str(self.meta['fpga'].get('device', None))
            env['FPGA_PACKAGE'] = str(self.meta['fpga'].get('package', None))
            env['FPGA_FREQUENCY'] = str(self.meta['fpga'].get('frequency', None))
        env['BUILD_ROOT'] = self.build_dir
        env['NAFARR_BASE'] = NAFARR_ROOT_DIR
        env['ZIBAL_BASE'] = ZIBAL_ROOT_DIR
        env['ELEMENTS_BASE'] = ZEPHYR_ROOT_DIR / ".."
        env['PATH'] = str(OSS_CAD_BUILD_DIR / "bin") + os.pathsep + env['PATH']
        env['PATH'] = str(NEXTPNR_XILINX_ROOT_DIR / "bin") + os.pathsep + env['PATH']
        env['NEXTPNR_XILINX_ROOT'] = str(NEXTPNR_XILINX_ROOT_DIR)
        env['F4PGA_INSTALL_DIR'] = F4PGA_ROOT_DIR
        return env

    def save_cache(self, cache_file, cache):
        if not cache_file.parent.exists():
            cache_file.parent.mkdir(parents=True, exist_ok=True)
        with open(cache_file, 'w+') as stream:
            yaml.dump(cache, stream)

    def open_cache(self, cache_file):
        with open(cache_file, 'r') as stream:
            try:
                return yaml.safe_load(stream)
            except yaml.YAMLError as exc:
                log.die(exc)

    def update_cache(self, key, hash_):
        cache_file = Path(self.build_dir) / self.meta['soc'] / self.meta['board'] / "cache.yaml"
        cache = self.open_cache(cache_file)
        cache[key] = hash_
        self.save_cache(cache_file, cache)

    def is_cached(self, key, hash_):
        cache_file = Path(self.build_dir) / self.meta['soc'] / self.meta['board'] / "cache.yaml"
        cache = {}
        if not cache_file.exists():
            cache[key] = hash_
            self.save_cache(cache_file, cache)
            return False

        cache = self.open_cache(cache_file)
        return cache.get(key, '') == hash_

    def generate(self):
        soc = self.meta['soc']
        board = self.meta['board']

        zephyr_cached = self.is_cached('zephyr',
                                       checksumdir.dirhash(Path(self.build_dir) / "zephyr"))
        elements_cached = self.is_cached('elements', checksumdir.dirhash(ELEMENTS_ROOT_DIR))
        if zephyr_cached and elements_cached:
            print("elements: no work to do.")
            return

        command = f"sbt \"runMain elements.soc.{soc.lower()}.{board}Generate\""
        env = self.get_env()
        cwd = ZIBAL_ROOT_DIR

        self.check_call(command, env, cwd)

        self.update_cache('zephyr', checksumdir.dirhash(Path(self.build_dir) / "zephyr"))
        self.update_cache('elements', checksumdir.dirhash(ELEMENTS_ROOT_DIR))

    def synthesize(self):
        top = f"{self.meta['board']}Top"
        build = Path(self.build_dir) / self.meta['soc'] / self.meta['board']

        if self.is_cached('zibal', checksumdir.dirhash(build / "zibal")):
            print("elements: no work to do.")
            return

        flow = {
            "default_part": self.meta['part'],
            "values": {
                "top": top
            },
            "dependencies": {
                "sources": [f"{build}/zibal/{top}.v"],
                "synth_log": f"{build}/fpga/{top}.synth.log",
                "pack_log": f"{build}/fpga/{top}.pack.log"
            },
            self.meta['part']: {
                "default_target": "bitstream",
                "dependencies": {
                    "build_dir":  str(build / "fpga"),
                    "xdc": [
                        f"{build}/zibal/{top}.xdc"
                    ]
                }
            }
        }
        with open(build / "zibal" / f"{top}.flow", 'w') as outfile:
            json.dump(flow, outfile)

        command = f"{ZIBAL_ROOT_DIR}/eda/{self.meta['vendor']}/fpga/syn.sh"
        env = self.get_env()
        cwd = build / "fpga"

        if not os.path.exists(cwd):
            os.mkdir(cwd)
        self.check_call(command, env, cwd)
        # Create symlink for openfpgaloader
        self.check_call(f"ln -sf {build}/fpga/{top}.bit {build}/{top}.bit", env, cwd)
        self.update_cache('zibal', checksumdir.dirhash(build / "zibal"))


    def synthesize_vivado(self, vivado_binary):
        top = f"{self.meta['board']}Top"
        build = Path(self.build_dir) / self.meta['soc'] / self.meta['board']
        vivado = Path(vivado_binary).parent
        if not (vivado / "vivado").exists():
                log.die(f"Vivado binary does not exist in {vivado}")

        env = self.get_env()
        cwd = build / "vivado"
        logs = cwd / "logs"
        env['PATH'] += os.pathsep + str(vivado)
        env['TCL_PATH'] = ZIBAL_ROOT_DIR / "eda/Xilinx/vivado/syn"
        command = f"vivado -mode batch -source {env['TCL_PATH']}/syn.tcl " \
                  f"-log {logs}/vivado.log -journal {logs}/journal.jou"

        if not os.path.exists(cwd):
            os.mkdir(cwd)
        if not os.path.exists(logs):
            os.mkdir(logs)
        syn = cwd / "syn"
        if not os.path.exists(syn):
            os.mkdir(syn)

        self.check_call(command, env, cwd)
        # Create symlink for openfpgaloader
        self.check_call(f"ln -sf {build}/vivado/syn/{top}.bit {build}/{top}.bit", env, cwd)


    def simulate(self, test_app = "simulate", duration = ""):
        soc = self.meta['soc']
        board = self.meta['board']

        cmd = f"sbt \"runMain elements.soc.{soc.lower()}.{board}Simulate {test_app} {duration}\""
        env = self.get_env()
        cwd = ZIBAL_ROOT_DIR
        self.check_call(cmd, env, cwd)

    def open(self):
        env = self.get_env()
        cwd = Path(self.build_dir) / self.meta['soc'] / self.meta['board'] / "zibal" / \
            f"{self.meta['board']}Board"

        self.check_call("gtkwave -o simulate.vcd", env, cwd)
