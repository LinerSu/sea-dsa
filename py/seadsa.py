

"""
Entry point to sea data structure analysis (sea-dsa) tool.
"""

import argparse
import atexit
import io
import os
import os.path
import platform
import resource
import shutil
import subprocess as sub
import signal
import stats
import sys
import tempfile
import threading
from typing import List, Optional


class Config:
    """Global configuration and state management."""
    root = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    verbose = True
    running_process = None
    llvm_version = "14.0"

    @classmethod
    def get_include_dir(cls):
        """Get the include directory path."""
        include_dir = os.path.dirname(sys.argv[0])
        include_dir = os.path.dirname(include_dir)
        return os.path.join(include_dir, 'include')


class Utils:
    """Utility functions for file operations and type checking."""

    @staticmethod
    def str2bool(v):
        """Convert string to boolean."""
        if isinstance(v, bool):
            return v
        if v.lower() in ('yes', 'true', 't', 'y', '1'):
            return True
        elif v.lower() in ('no', 'false', 'f', 'n', '0'):
            return False
        else:
            raise argparse.ArgumentTypeError('Boolean value expected.')

    @staticmethod
    def add_bool_argument(parser, name, default=False, help=None, dest=None, **kwargs):
        """Add boolean option that can be turned on and off."""
        dest_name = dest if dest else name
        mutex_group = parser.add_mutually_exclusive_group(required=False)
        mutex_group.add_argument('--' + name, dest=dest_name, type=Utils.str2bool,
                                 nargs='?', const=True, help=help,
                                 metavar='BOOL', **kwargs)
        mutex_group.add_argument('--no-' + name, dest=dest_name,
                                 type=lambda v: not (Utils.str2bool(v)),
                                 nargs='?', const=False,
                                 help=argparse.SUPPRESS, **kwargs)
        parser.set_defaults(**{dest_name: default})

    @staticmethod
    def def_bc_name(name, wd=None):
        """Generate bitcode filename from input name."""
        base = os.path.basename(name)
        if wd is None:
            wd = os.path.dirname(name)
        fname = os.path.splitext(base)[0] + '.bc'
        return os.path.join(wd, fname)

    @staticmethod
    def def_out_pp_name(name, wd=None):
        """Generate output LLVM IR filename from input name."""
        base = os.path.basename(name)
        if wd is None:
            wd = os.path.dirname(name)
        fname = os.path.splitext(base)[0] + '.ll'
        return os.path.join(wd, fname)

    @staticmethod
    def is_bc_or_ll_file(name):
        """Check if file is bitcode or LLVM IR."""
        ext = os.path.splitext(name)[1]
        return ext == '.bc' or ext == '.ll'

    @staticmethod
    def is_plus_plus_file(name):
        """Check if file is C++ source."""
        ext = os.path.splitext(name)[1]
        return ext in ('.cpp', '.cc')

    @staticmethod
    def isexec(fpath):
        """Check if file path is executable."""
        if fpath is None:
            return False
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

    @staticmethod
    def add_help_arg(ap):
        """Add help argument to parser."""
        ap.add_argument('-h', '--help', action='help',
                        help='Print this message and exit')

    @staticmethod
    def add_in_args(ap):
        """Add input file arguments to parser."""
        ap.add_argument('in_files', metavar='FILE',
                        help='Input file', nargs='+')
        return ap

    @staticmethod
    def add_in_out_args(ap):
        """Add input and output file arguments to parser."""
        Utils.add_in_args(ap)
        ap.add_argument('-o', dest='out_file',
                        metavar='FILE', help='Output file name', default=None)
        return ap

    @staticmethod
    def which(program):
        """Find executable in PATH."""
        if isinstance(program, str):
            choices = [program]
        else:
            choices = program

        for p in choices:
            fpath, _ = os.path.split(p)
            if fpath:
                if Utils.isexec(p):
                    return p
            else:
                for path in os.environ["PATH"].split(os.pathsep):
                    exe_file = os.path.join(path, p)
                    if Utils.isexec(exe_file):
                        return exe_file
        return None

    @staticmethod
    def create_work_dir(dname=None, save=False):
        """Create working directory."""
        if dname is None:
            workdir = tempfile.mkdtemp(prefix='seadsa-')
        else:
            workdir = dname

        if Config.verbose and False:
            print("Working directory {0}".format(workdir))

        if not save:
            atexit.register(shutil.rmtree, path=workdir)
        return workdir


def run_with_limits(cmd, cpu=-1, mem=-1, out=None):
    """Run `cmd` (list) with optional CPU (seconds) and MEM (MB) limits.

    Returns (returncode, timeout, out_of_memory, segfault, unknown).
    This is a lightweight replacement of clam.run_command_with_limits
    sufficient for the seadsa/clang runners.
    """
    timeout = False
    out_of_memory = False
    segfault = False
    unknown = False

    def set_limits():
        # set CPU limit
        if cpu and cpu > 0:
            try:
                resource.setrlimit(resource.RLIMIT_CPU, (cpu, cpu))
            except Exception:
                pass
        # set address space limit (approximate memory)
        if mem and mem > 0:
            try:
                mb = mem * 1024 * 1024
                resource.setrlimit(resource.RLIMIT_AS, (mb, mb))
            except Exception:
                pass

    try:
        if out is None:
            p = sub.Popen(cmd, stdout=sub.PIPE, stderr=sub.PIPE,
                          preexec_fn=set_limits)
        else:
            p = sub.Popen(cmd, stdout=out, stderr=sub.PIPE,
                          preexec_fn=set_limits)
        Config.running_process = p

        # simple watchdog: if cpu provided, also kill after cpu+1 seconds
        timer = None
        if cpu and cpu > 0:
            def kill():
                nonlocal timeout
                try:
                    timeout = True
                    p.kill()
                except Exception:
                    pass
            timer = threading.Timer(cpu + 1, kill)
            timer.daemon = True
            timer.start()

        out_bytes, err_bytes = p.communicate()
        if timer:
            timer.cancel()

        ret = p.returncode
        # negative returncode means terminated by signal
        if ret is None:
            unknown = True
        elif ret < 0:
            sig = -ret
            if sig == signal.SIGSEGV:
                segfault = True
            elif sig == signal.SIGKILL:
                # heuristically treat SIGKILL as potential OOM
                out_of_memory = True
            elif sig == signal.SIGXCPU or sig == signal.SIGALRM:
                timeout = True
            else:
                unknown = True
        else:
            # non-zero exit code treated as unknown error
            if ret != 0:
                unknown = True

        Config.running_process = None
        return (ret, timeout, out_of_memory, segfault, unknown)
    except OSError as e:
        Config.running_process = None
        unknown = True
        return (1, timeout, out_of_memory, segfault, unknown)


class CliCmd (object):
    def __init__(self, name='', help='', allow_extra=False):
        self.name = name
        self.help = help
        self.allow_extra = allow_extra

    def mk_arg_parser(self, argp):
        """Make argument parser for this command."""
        Utils.add_help_arg(argp)
        return argp

    def run(self, args=None, extra=[]):
        return 0

    def name_out_file(self, in_files, args=None, work_dir=None):
        out_file = 'out'
        if work_dir is not None:
            out_file = os.path.join(work_dir, out_file)
        return out_file

    def main(self, argv):
        import argparse
        ap = argparse.ArgumentParser(prog=self.name,
                                     description=self.help,
                                     add_help=False)
        ap = self.mk_arg_parser(ap)

        if self.allow_extra:
            args, extra = ap.parse_known_args(argv)
        else:
            args = ap.parse_args(argv)
            extra = []
        return self.run(args, extra)


class LimitedCmd (CliCmd):
    def __init__(self, name='', help='', allow_extra=False):
        super(LimitedCmd, self).__init__(name, help, allow_extra)

    def mk_arg_parser(self, argp):
        argp = super(LimitedCmd, self).mk_arg_parser(argp)
        argp.add_argument('--cpu', type=int, dest='cpu', metavar='SEC',
                          help='CPU time limit (seconds)', default=-1)
        argp.add_argument('--mem', type=int, dest='mem', metavar='MB',
                          help='MEM limit (MB)', default=-1)
        return argp


class ClangCmd(LimitedCmd):
    """Runner class for invoking clang. Mirrors the behavior of the
    module-level `clang` function but packaged as a LimitedCmd.
    """

    def __init__(self):
        super(ClangCmd, self).__init__(name='clang',
                                       help='Invoke clang', allow_extra=True)

    @staticmethod
    def get_clang(is_plus_plus):
        """Find clang executable."""
        if is_plus_plus:
            cmd_name = Utils.which(['clang++-mp-' + Config.llvm_version,
                                   'clang++-' + Config.llvm_version,
                                    'clang++'])
        else:
            cmd_name = Utils.which(['clang-mp-' + Config.llvm_version,
                                   'clang-' + Config.llvm_version,
                                    'clang'])
        if cmd_name is None:
            raise IOError('clang was not found')
        return cmd_name

    @staticmethod
    def get_clang_version(clang_cmd):
        """Get clang version."""
        p = sub.Popen([clang_cmd, '--version'], stdout=sub.PIPE)
        out, _ = p.communicate()
        clang_version = "not-found"
        found = False
        tokens = out.split()
        for t in tokens:
            if found:
                clang_version = t.decode(
                    'utf-8') if isinstance(t, bytes) else t
                break
            if t == b'version' or t == 'version':
                found = True
        return clang_version

    def run(self, in_name, out_name, args, extra_args=None, cpu=-1, mem=-1):
        if extra_args is None:
            extra_args = []

        if Utils.is_bc_or_ll_file(in_name):
            if Config.verbose:
                print('--- Clang skipped: input file is already bitecode')
            shutil.copy2(in_name, out_name)
            return 0

        if out_name in ('', None):
            out_name = Utils.def_bc_name(in_name)

        clang_cmd = self.get_clang(Utils.is_plus_plus_file(in_name))
        clang_version = self.get_clang_version(clang_cmd)
        if clang_version != "not-found":
            if not clang_version.startswith(Config.llvm_version):
                print("WARNING: clang version " + clang_version +
                      " different from " + Config.llvm_version)

        clang_args = [clang_cmd, '-emit-llvm', '-o', out_name, '-c', in_name]
        clang_args.append('-Xclang')
        clang_args.append('-disable-O0-optnone')
        clang_args.append('-fdeclspec')
        clang_args.append('-fno-discard-value-names')
        clang_args.extend(extra_args)
        clang_args.append('-m{0}'.format(args.machine))

        if args.include_dir is not None:
            if ':' in args.include_dir:
                idirs = ["-I{}".format(x.strip())
                         for x in args.include_dir.split(":") if x.strip() != '']
                clang_args.extend(idirs)
            else:
                clang_args.append('-I' + args.include_dir)

        clang_args.append('-I' + Config.get_include_dir())

        if not args.disable_scalarize:
            clang_args.append('-fno-vectorize')
            clang_args.append('-fno-slp-vectorize')

        if platform.system() == 'Darwin':
            osx_sdk_path = sub.run(
                ["xcrun", "--show-sdk-path"], text=True, stdout=sub.PIPE)
            if osx_sdk_path.returncode == 0:
                clang_args.append('--sysroot=' + osx_sdk_path.stdout.strip())

        if Config.verbose:
            print('Clang command: ' + ' '.join(clang_args))

        returncode, timeout, out_of_mem, segfault, unknown = run_with_limits(
            clang_args, cpu, mem)
        if timeout:
            sys.exit(20)
        elif out_of_mem:
            sys.exit(21)
        elif segfault or unknown or returncode != 0:
            sys.exit(22)
        return returncode


class SeadsaCmd(LimitedCmd):
    """Runner for `seadsa` binary. Builds a simple command line and
    runs it under resource limits.
    """

    def __init__(self):
        super(SeadsaCmd, self).__init__(
            name='seadsa', help='Run seadsa', allow_extra=True)

    def find_seadsa(self, explicit: Optional[str] = None) -> str:
        """Find seadsa executable."""
        if explicit and Utils.isexec(explicit):
            return explicit
        if 'SEADSA' in os.environ:
            cand = os.environ['SEADSA']
            if Utils.isexec(cand):
                return cand
        found = Utils.which(['seadsa'])
        if found:
            return found
        # fall back to plain name; let OS decide
        return 'seadsa'

    def mk_arg_parser(self, ap):
        """Make argument parser for seadsa command."""
        ap = super(SeadsaCmd, self).mk_arg_parser(ap)
        ap = Utils.add_in_out_args(ap)
        return ap

    def run(self, in_name, out_name, args, extra_opts=None, cpu=-1, mem=-1):
        if extra_opts is None:
            extra_opts = []
        seadsa_path = self.find_seadsa(
            getattr(args, 'seadsa', None) if args is not None else None)
        cmd = [seadsa_path]
        if in_name:
            cmd.append(in_name)
        if out_name:
            cmd.extend(['-o', out_name])
        if hasattr(args, 'passthrough') and args.passthrough:
            cmd.extend(args.passthrough)
        if extra_opts:
            cmd.extend(extra_opts)

        if getattr(args, 'verbose', False):
            print('Seadsa command: ' + ' '.join(cmd))

        ret, timed_out, out_of_mem, segfault, unknown = run_with_limits(
            cmd, cpu, mem)
        if timed_out:
            sys.exit(20)
        if out_of_mem:
            sys.exit(21)
        if segfault or unknown or (ret is not None and ret != 0):
            sys.exit(22)
        return ret


class AgregateCmd(CliCmd):
    """Aggregate command that dispatches to subcommands."""

    def __init__(self, name='', help='', cmds=None):
        super(AgregateCmd, self).__init__(name, help, allow_extra=True)
        self.cmds = cmds if cmds is not None else []

    def mk_arg_parser(self, argp):
        """Make argument parser with subparsers."""
        Utils.add_help_arg(argp)
        # https://stackoverflow.com/questions/22990977/why-does-this-argparse-code-behave-differently-between-python-2-and-3
        sb = argp.add_subparsers(dest='parser')
        sb.required = True
        for c in self.cmds:
            sp = sb.add_parser(c.name, help=c.help, add_help=False)
            sp = c.mk_arg_parser(sp)
            sp.set_defaults(func=c.run)
        return argp

    def run(self, args=None, extra=None):
        """Run the selected subcommand."""
        if extra is None:
            extra = []
        return args.func(args, extra)


def killall():
    """Kill any running process."""
    if Config.running_process is not None:
        try:
            Config.running_process.terminate()
            Config.running_process.kill()
            Config.running_process.wait()
            Config.running_process = None
        except OSError:
            pass


def main():
    """Main entry point."""
    cmds = [ClangCmd(), SeadsaCmd()]
    cmd = AgregateCmd('seadsa.py', 'Sea-DSA analysis tool', cmds=cmds)
    return cmd.main(sys.argv[1:])


if __name__ == '__main__':
    # unbuffered output
    sys.stdout = io.TextIOWrapper(
        open(sys.stdout.fileno(), 'wb', 0), write_through=True)
    try:
        signal.signal(signal.SIGTERM, lambda x, y: killall())
        sys.exit(main())
    except KeyboardInterrupt:
        pass
    finally:
        killall()
        stats.brunch_print()
