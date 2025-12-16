#!/usr/bin/env python3

import argparse
import os
import subprocess

from python.runfiles import Runfiles

agents = {
    'emulator_extract': """
      agent: {
        extract: {
          path: "%(emulator_zip_path)s"
        }
      }
    """,
    'android_home': """
      agent: {
        android_home: {
          extract_dir: "%(platform_tools_extract_dir)s"
        }
      }
    """,
    'junit_xml_result': """
      agent: {
        imports: {
          id: "tradefed"
          src: "results_dir"
        }
        junit_xml_result: {}
      }
    """,
    'avd': """
      agent: {
        avd: {
          avd_config_ini: "avd.ini.encoding=UTF-8"
          avd_config_ini: "disk.dataPartition.size=8G"
          avd_config_ini: "hw.accelerometer=yes"
          avd_config_ini: "hw.audioInput=yes"
          avd_config_ini: "hw.battery=yes"
          avd_config_ini: "hw.camera.back=emulated"
          avd_config_ini: "hw.camera.front=emulated"
          avd_config_ini: "hw.cpu.ncore=4"
          avd_config_ini: "hw.device.hash2=MD5:2fa0e16c8cceb7d385183284107c0c88"
          avd_config_ini: "hw.device.manufacturer=Google"
          avd_config_ini: "hw.device.name=Pixel 7 Pro"
          avd_config_ini: "hw.dPad=no"
          avd_config_ini: "hw.gps=yes"
          avd_config_ini: "hw.gpu.enabled=yes"
          avd_config_ini: "hw.keyboard=yes"
          avd_config_ini: "hw.lcd.density=560"
          avd_config_ini: "hw.lcd.height=3120"
          avd_config_ini: "hw.lcd.width=1440"
          avd_config_ini: "hw.mainKeys=no"
          avd_config_ini: "hw.ramSize=4096"
          avd_config_ini: "hw.sdCard=no"
          avd_config_ini: "hw.sensors.orientation=yes"
          avd_config_ini: "hw.sensors.proximity=yes"
          avd_config_ini: "hw.trackBall=no"
          avd_config_ini: "skin.dynamic=no"
          avd_config_ini: "skin.name=1440x3120"
          avd_config_ini: "skin.path=1440x3120"
          avd_config_ini: "tag.display=Google Play"
          avd_config_ini: "tag.id=google_apis_playstore"
          cleanup: true
          extract_dir: "%(image_extract_dir)s"
        }
      }
    """,
    'qemu_next': """
      agent: {
        imports: {
          id: "android_home"
          src: "android_home"
        }
        imports: {
          id: "avd"
          src: "avd_path"
        }
        imports: {
          id: "extract"
          src: "extract_dir"
        }
        goldfish: {
          args: "-wipe-data"
          args: "-verbose"
          args: "-show-kernel"
          args: "-guest-angle"
          args: "-not-in-bazel"
          cleanup: true
          emulator_path: "emulator"
        }
      }
    """,
    'qemu_next_local': """
      agent: {
        imports: {
          id: "android_home"
          src: "android_home"
        }
        imports: {
          id: "avd"
          src: "avd_path"
        }
        goldfish: {
          args: "-wipe-data"
          args: "-verbose"
          args: "-show-kernel"
          args: "-guest-angle"
          args: "-not-in-bazel"
          cleanup: true
          emulator_path: "emulator"
          extract_dir: "%(emulator_zip_path)s"
        }
      }
    """,
    'qemu_now_local': """
      agent: {
        imports: {
          id: "android_home"
          src: "android_home"
        }
        imports: {
          id: "avd"
          src: "avd_path"
        }
        goldfish: {
          args: "-gpu"
          args: "swiftshader_indirect"
          args: "-no-window"
          args: "-no-snapshot"
          args: "-wipe-data"
          args: "-delay-adb"
          args: "-no-metrics"
          args: "-restart-when-stalled"
          args: "-append-userspace-opt"
          args: "androidboot.radio.allow_mock_modem=1"
          extract_dir: "%(emulator_zip_path)s"
          cleanup: true
        }
      }
    """,
    'tradefed': """
      agent: {
        imports: {
          id: "tradefed"
          src: "has_retry_data"
          dst: "retry"
        }
        tradefed: {
          args: "run"
          args: "commandAndExit"
          %(tradefed_args)s
          args: "--skip-preconditions"
          args: "--skip-all-system-status-check"
          args: "--no-has-server-side-config"
          args: "-l"
          args: "INFO"
          # The agent looks for android-*, so we use the parent dir.
          extract_dir: "%(test_tmpdir)s"
          build_tools_extract_dir: "%(build_tools_extract_dir)s"
          platform_tools_extract_dir: "%(platform_tools_extract_dir)s"
          timeout_seconds: 3600
          preclean: true
        }
      }
    """,
}

sequence = [
    'emulator_extract',
    'android_home',
    'junit_xml_result',
    'avd',
    'qemu_next',
    'tradefed'
]


parser = argparse.ArgumentParser(
    prog='run_cts',
    description='Creates a test sequencer proto file and runs test sequencer')

parser.add_argument(
    '--system_img_path',
    required=True,
    help='Points to system.img. If local/image exists, this flag is ignored.')

parser.add_argument(
    '--build_tools_aapt_path',
    required=True,
    help='Points to the build tools aapt executable')

parser.add_argument(
    '--media_readme_path',
    help='Optional: Points to the README.txt file in the media zip')

parser.add_argument(
    '--platform_tools_adb_path',
    required=True,
    help='Points to the platform tools adb executable')

parser.add_argument(
    '--tradefed_exec_path',
    required=True,
    help='Points to the tradefed executable (e.g. cts-tradefed)')

parser.add_argument(
    '--test_seq_path',
    required=True,
    help='Points to the test_seq test sequencer executable')

parser.add_argument(
    '--tradefed_args',
    required=True,
    help='Adds additional (comma-separated) arguments to the tradefed invocation')

args = parser.parse_args()


def main():
  # adb requires HOME to be defined and to exist
  env = os.environ.copy()
  env['HOME'] = test_tmpdir('home', True)
  # A symlink is needed because the path is too long for tradefed
  os.symlink(
      tradefed_dir(),
      test_tmpdir('android-cts')
  )

  args = [
          './test_seq',
          '--name',
          test_seq_results_dir(),
          '--runtime_dir',
          test_tmpdir('runtime'),
          '--common_dir',
          test_tmpdir('common'),
          create_proto_file(),
  ]
  print('run_cts.py: Running in %s: %s' % (os.getcwd(), args))
  subprocess.run(
      args,
      cwd=test_seq_dir(),
      env=env,
      check=True,
  )


def create_proto_file() -> str:
  runfiles = Runfiles.Create()
  keys = {
      'build_tools_extract_dir': build_tools_extract_dir(),
      'emulator_zip_path': emulator_zip_path(runfiles),
      'image_extract_dir': image_extract_dir(runfiles),
      'platform_tools_extract_dir': platform_tools_extract_dir(),
      'test_tmpdir': test_tmpdir(),
      'tradefed_args': tradefed_args(),
  }
  template = '\n'.join(agents[k] for k in sequence)
  proto_data = template % keys
  # A bit of a hack here.  Plan files use a relative location and need the pwd
  # patched in
  proto_data = proto_data.replace('PWD', os.getcwd())
  proto_data = proto_data.replace('MEDIA_EXTRACT_DIR', media_extract_dir())
  proto_path = test_tmpdir('sequence.txtpb')
  with open(proto_path, 'w') as fout:
    fout.write(proto_data)

  return proto_path


def build_tools_extract_dir() -> str:
  aapt_path = os.path.abspath(
      os.path.join(os.getcwd(), args.build_tools_aapt_path))
  return os.path.dirname(os.path.dirname(aapt_path))


def emulator_zip_path(runfiles) -> str:
  local_emulator_path = os.path.abspath(runfiles.Rlocation(f"goldfish+/emulator/tests/local/emulator"))
  if os.path.exists(local_emulator_path):
    sequence.remove("emulator_extract")
    if os.path.exists(os.path.join(local_emulator_path, "emulator/emulator")):
      print(f'Using a LOCAL qemu_now Emulator: {local_emulator_path}')
      sequence[sequence.index("qemu_next")] = "qemu_now_local"
    else:
      print(f'Using a LOCAL qemu_next Emulator: {local_emulator_path}')
      sequence[sequence.index("qemu_next")] = "qemu_next_local"
    return local_emulator_path
  return runfiles.Rlocation(f"goldfish+/emulator/sdk-repo-linux--developer.zip")


def image_extract_dir(runfiles) -> str:
  local_image_path = os.path.abspath(runfiles.Rlocation(f"goldfish+/emulator/tests/local/image"))
  if os.path.exists(local_image_path):
    print(f'Using a LOCAL Image: {local_image_path}')
    return local_image_path

  sysimg_path = os.path.abspath(
      os.path.join(os.getcwd(), args.system_img_path))
  return os.path.dirname(os.path.dirname(sysimg_path))


def media_extract_dir() -> str:
  if not args.media_readme_path:
    return 'MEDIA_README_NOT_SET'
  readme_path = os.path.abspath(os.path.join(
        os.getcwd(), args.media_readme_path))
  return os.path.dirname(readme_path)


def platform_tools_extract_dir() -> str:
  adb_path = os.path.abspath(
      os.path.join(os.getcwd(), args.platform_tools_adb_path))
  return os.path.dirname(os.path.dirname(adb_path))


def test_seq_dir() -> str:
  bin_path = os.path.abspath(
      os.path.join(os.getcwd(), args.test_seq_path))
  return os.path.dirname(bin_path)


def test_seq_results_dir() -> str:
  return os.path.abspath(os.path.join(
        os.getcwd(),
        os.getenv('TEST_UNDECLARED_OUTPUTS_DIR'),
        'results'
  ))


def test_tmpdir(subdir: str = '', create: bool = False) -> str:
  path = os.path.abspath(os.path.join(os.getcwd(), os.getenv('TEST_TMPDIR')))
  if subdir:
    path = os.path.join(path, subdir)
  if create:
    os.makedirs(path, exist_ok=True)
  return path


def tradefed_dir() -> str:
  exec_path = os.path.abspath(os.path.join(
        os.getcwd(), args.tradefed_exec_path))
  return os.path.dirname(os.path.dirname(os.path.dirname(exec_path)))


def tradefed_args() -> str:
  return '\n'.join(
      f'    args: "{arg}"' for arg in args.tradefed_args.split(','))


if __name__ == '__main__':
  main()

