# Copyright (c) 2023 Daniel Schultz
#
# SPDX-License-Identifier: Apache-2.0

import argparse

from zephyr_ext_common import Forceable

from run_common import get_build_dir, load_cmake_cache, rebuild, _banner

from elements_base import Elements

GENERATE_DESCRIPTION = '''\
This command generates a RISC-V MCU design for a specific board.
'''

GENERATE_USAGE = '''\
west generate [-h] [-b BOARD] [-d BUILD_DIR]
'''

SYNTHESIZE_DESCRIPTION = '''\
This command synthesizes a RISC-V MCU design for a specific board.
'''

SYNTHESIZE_USAGE = '''\
west synthesize [-h] [-b BOARD] [-d BUILD_DIR] [--vendor-xilinx VIVADO_BINARY]
'''

SIMULATE_DESCRIPTION = '''\
This command simulates a RISC-V MCU design for a specific board.
'''

SIMULATE_USAGE = '''\
west simulate [-h] [-b BOARD] [-d BUILD_DIR] [--duration DURATION_IN_MS] [--show-only]
'''

TEST_DESCRIPTION = '''\
This command runs a test application for a specific board.
'''

TEST_USAGE = '''\
west test [-h] [-b BOARD] [-d BUILD_DIR] <application>
'''


class Generate(Forceable):
    def __init__(self):
        super(Generate, self).__init__(
            'generate',
            # Keep this in sync with the string in west-commands.yml.
            'generate a RISC-V MCU design',
            GENERATE_DESCRIPTION,
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            usage=GENERATE_USAGE)

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument('-b', '--board',
                            help='board to build for with optional board revision')
        parser.add_argument('-d', '--build-dir',
                            help='build directory to create or use')
        self.add_force_arg(parser)

        group = parser.add_argument_group('cmake and build tool')
        group.add_argument('-n', '--just-print', '--dry-run', '--recon',
                           dest='dry_run', action='store_true',
                           help="just print build commands; don't run them")
        group.add_argument('-c', '--cmake-cache', metavar='FILE',
                           help=argparse.SUPPRESS)

        return parser

    def do_run(self, args, remainder):
        build_dir = get_build_dir(args)
        cache = load_cmake_cache(build_dir, args)
        if not args.board:
            board = cache['CACHED_BOARD']
        else:
            board = args.board

        rebuild(self, build_dir, args)
        elements = Elements(build_dir, board, args.dry_run)
        _banner(f'west {self.name}: generating')
        elements.generate()


class Synthesize(Forceable):

    def __init__(self):
        super(Synthesize, self).__init__(
            'synthesize',
            # Keep this in sync with the string in west-commands.yml.
            'synthesize a RISC-V MCU design',
            SYNTHESIZE_DESCRIPTION,
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            usage=SYNTHESIZE_USAGE)

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument('-b', '--board',
                            help='board to build for with optional board revision')
        parser.add_argument('-d', '--build-dir',
                            help='build directory to create or use')
        parser.add_argument('--vendor-xilinx', dest='vendor_xilinx',
                            help="Use Xilinx Vivado instead of F4PGA")

        self.add_force_arg(parser)

        group = parser.add_argument_group('cmake and build tool')
        group.add_argument('-n', '--just-print', '--dry-run', '--recon',
                           dest='dry_run', action='store_true',
                           help="just print build commands; don't run them")
        group.add_argument('-c', '--cmake-cache', metavar='FILE',
                           help=argparse.SUPPRESS)

        return parser

    def do_run(self, args, remainder):
        build_dir = get_build_dir(args)
        cache = load_cmake_cache(build_dir, args)
        if not args.board:
            board = cache['CACHED_BOARD']
        else:
            board = args.board

        rebuild(self, build_dir, args)
        elements = Elements(build_dir, board, args.dry_run)
        _banner(f'west {self.name}: generating')
        elements.generate()
        _banner(f'west {self.name}: synthesizing')
        if args.vendor_xilinx:
            elements.synthesize_vivado(args.vendor_xilinx)
        else:
            elements.synthesize()


class Simulate(Forceable):

    def __init__(self):
        super(Simulate, self).__init__(
            'simulate',
            # Keep this in sync with the string in west-commands.yml.
            'simulate a RISC-V MCU design',
            SIMULATE_DESCRIPTION,
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            usage=SIMULATE_USAGE)

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument('-b', '--board',
                            help='board to build for with optional board revision')
        parser.add_argument('-d', '--build-dir',
                            help='build directory to create or use')
        parser.add_argument('--duration',
                            help='Duration of the simulation in millisecond')
        self.add_force_arg(parser)

        group = parser.add_argument_group('cmake and build tool')
        group.add_argument('-n', '--just-print', '--dry-run', '--recon',
                           dest='dry_run', action='store_true',
                           help="just print build commands; don't run them")
        group.add_argument('-c', '--cmake-cache', metavar='FILE',
                           help=argparse.SUPPRESS)
        parser.add_argument('--show-only', required=False, action='store_true',
                            help='Shows the simulation only')

        return parser

    def do_run(self, args, remainder):
        build_dir = get_build_dir(args)
        cache = load_cmake_cache(build_dir, args)
        if not args.board:
            board = cache['CACHED_BOARD']
        else:
            board = args.board

        rebuild(self, build_dir, args)
        elements = Elements(build_dir, board, args.dry_run)
        if not args.show_only:
            _banner(f'west {self.name}: generating')
            elements.generate()
            _banner(f'west {self.name}: simulating')
            if args.duration:
                elements.simulate(duration=args.duration)
            else:
                elements.simulate()
        _banner(f'west {self.name}: opening simulation')
        elements.open()


class Test(Forceable):

    def __init__(self):
        super(Test, self).__init__(
            'test',
            # Keep this in sync with the string in west-commands.yml.
            'runs a test application',
            TEST_DESCRIPTION,
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            usage=TEST_USAGE)

        # Remember to update west-completion.bash if you add or remove
        # flags
        parser.add_argument('-b', '--board',
                            help='board to build for with optional board revision')
        parser.add_argument('-d', '--build-dir',
                            help='build directory to create or use')
        self.add_force_arg(parser)

        group = parser.add_argument_group('cmake and build tool')
        group.add_argument('-n', '--just-print', '--dry-run', '--recon',
                           dest='dry_run', action='store_true',
                           help="just print build commands; don't run them")
        group.add_argument('-c', '--cmake-cache', metavar='FILE',
                           help=argparse.SUPPRESS)
        parser.add_argument('application', help='Test application to run')

        return parser

    def do_run(self, args, remainder):
        build_dir = get_build_dir(args)
        cache = load_cmake_cache(build_dir, args)
        if not args.board:
            board = cache['CACHED_BOARD']
        else:
            board = args.board

        rebuild(self, build_dir, args)
        elements = Elements(build_dir, board, args.dry_run)
        _banner(f'west {self.name}: generating')
        elements.generate()
        _banner(f'west {self.name}: testing')
        elements.simulate(args.application)
