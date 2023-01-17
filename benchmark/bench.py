import enum
from ftplib import all_errors
from numpy import outer
import time
import argparse
import itertools
import os
import subprocess
import datetime
from bench_util import RemoteExecute
import exp_config
from zmq import SERVER
import concurrent.futures


PROJECT_PATH = "/home/xieminhui/petps"
LOG_PREFIX = f'{PROJECT_PATH}/benchmark/log'

#  sudo service  memcached stop


def ParallelSSH(hosts, command):
    print(hosts, command)
    SSH = "ssh -o StrictHostKeyChecking=no"

    def command_fn(host):
        subprocess.run(
            f'''{SSH} {host} "{command}"''', shell=True, check=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
        for each in hosts:
            executor.submit(command_fn, each)


def mount_master(hosts):
    ParallelSSH(
        hosts, f"mkdir -p {PROJECT_PATH};")
    ParallelSSH(
        hosts, f"ls {PROJECT_PATH};")

    ParallelSSH(
        hosts, f"fusermount -u petps;")
    ParallelSSH(
        hosts, f"sudo mount 10.0.2.130:{PROJECT_PATH} petps")


def config_each_server(hosts):
    ParallelSSH(
        hosts, f"sudo swapoff -a")


if __name__ == "__main__":
    from exp_config import ALL_SERVERS_INCLUDING_NOT_USED
    from bench_util import Pnuke
    Pnuke(ALL_SERVERS_INCLUDING_NOT_USED, "petps_server")
    Pnuke(ALL_SERVERS_INCLUDING_NOT_USED, "benchmark_client")

    exp_lists = []
    each = exp_config.ExpEnd2End()
    each.SetLogDir(f'{LOG_PREFIX}/exp-end2end')
    exp_lists.append(each)

    # each = exp_config.ExpSensitive()
    # each.SetLogDir(f'{LOG_PREFIX}/exp-sensitive-embsize')
    # exp_lists.append(each)

    # each = exp_config.ExpDRAM()
    # each.SetLogDir(f'{LOG_PREFIX}/exp-dram-rerun')
    # exp_lists.append(each)

    # each = exp_config.ExpSensitiveWrite()
    # each.SetLogDir(f'{LOG_PREFIX}/exp-sensitive-write')
    # exp_lists.append(each)

    # each = exp_config.ExpSensitiveBatchCount()
    # each.SetLogDir(f'{LOG_PREFIX}/exp-sensitive-batchcount')
    # exp_lists.append(each)

    # each = exp_config.ExpFakeKV()
    # each.SetLogDir(f'{LOG_PREFIX}/exp-perf-sgl-fakeKV')
    # exp_lists.append(each)

    # each = exp_config.ExpMotivation()
    # each.SetLogDir(f'{LOG_PREFIX}/exp-motivation')
    # exp_lists.append(each)

    for i, each in enumerate(exp_lists):
        # mount NFS
        mount_master(
            [each for each in ALL_SERVERS_INCLUDING_NOT_USED if each != '10.0.2.130'])
        config_each_server(
            [each for each in ALL_SERVERS_INCLUDING_NOT_USED if each != '10.0.2.130'])

        print("=================-====================")
        print(f"Experiment {i}/{len(exp_lists)}: ", each.name)
        each.RunExperiment()