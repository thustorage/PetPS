from pprint import pprint
import subprocess
import os
import datetime
import time
from bench_util import *
from bench_base import *
import concurrent.futures

ALL_SERVERS_INCLUDING_NOT_USED = [
    # client
    '10.0.2.110',
    # PS
    '10.0.2.130',
]
# Each element has the format (IP, NUMA_ID)
SINGLE_PS_SERVERS = [
    ('10.0.2.130', 0),
]
SINGLE_CLIENT_SERVERS = [
    ('10.0.2.110', 0),
]

PRELOAD_WHEN_INIT = False
SERVER_THREAD_NUM = 18
WARM_UP_RATIO = 0.8


def ConvertHostNumaList2Host(host_numa_lists):
    # [(host0, 0), (host0, 1), (host1, 0)] to (host0, host1)
    return list(set([each[0] for each in host_numa_lists]))


def InitPMToFsDax(hosts,):
    if type(hosts) is not list:
        hosts = [hosts]

    def command_fn(each, numa_id):
        command = f"""
        df -h | grep aep{numa_id} >/dev/null
        if [ $? -ne 0 ]; then
            if [ ! -f "/dev/pmem{numa_id}" ]; then
                sudo ndctl create-namespace -e namespace{numa_id}.0 --mode=fsdax -f
            fi
            sudo mkfs.ext4 /dev/pmem{numa_id}
            sudo mkdir -p /media/aep{numa_id}
            sudo mount -o dax /dev/pmem{numa_id} /media/aep{numa_id}
            sudo chown -R xieminhui:xieminhui /media/aep{numa_id}
        fi
        rm -rf /media/aep{numa_id}/*
        """
        RemoteExecute(each, command, PROJECT_ABS_PATH)

    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
        for each, numa_id in hosts:
            executor.submit(command_fn, each, numa_id)


def InitPMToDevDax(hosts,):
    if type(hosts) is not list:
        hosts = [hosts]

    def command_fn(each, numa_id):
        command = f"""
        (lsblk | grep pmem{numa_id}) && sudo fuser -cuk /dev/pmem{numa_id}
        (lsblk | grep pmem{numa_id}) && sudo umount -f /dev/pmem{numa_id}
        (lsblk | grep pmem{numa_id}) && sudo ndctl create-namespace -e namespace{numa_id}.0 --mode=devdax -f
        sudo chown xieminhui:xieminhui /dev/dax{numa_id}.0

        echo reinit pm_command
        ./build/bin/pm_command --command=reinit --logtostderr --numa_id={numa_id}

        sudo mkdir /media/aep{numa_id} >/dev/null 2>&1
        sudo chown -R xieminhui:xieminhui /media/aep{numa_id}
        rm -rf /media/aep{numa_id}/*
        """
        RemoteExecute(each, command, PROJECT_ABS_PATH)

    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
        for each, numa_id in hosts:
            executor.submit(command_fn, each, numa_id)


def InitFSDaxIndex(hosts, db, key_space_m, value_size, warm_thread_num):
    if type(hosts) is not list:
        hosts = [hosts]
    print(f"""<Init {db}>, with
          hosts={hosts}
          key_space_m={key_space_m}
          value_size={value_size}
          """)

    print("======== reinit to AEP ======== ")
    InitPMToFsDax(hosts)
    if PRELOAD_WHEN_INIT:
        def command_fn(i, each, numa_id):
            command = f"""
            echo "======== preload AEP ========"
            ./build/bin/petps_server --key_space_m={key_space_m} --logtostderr \
                --value_size={value_size} --preload=true --use_sglist=false \
                --max_kv_num_per_request=500 --thread_num=16 --warmup_thread_num={warm_thread_num} --db={db} \
                --num_server_processes={len(hosts)} --global_id={i} --numa_id={numa_id} --warmup_ratio={WARM_UP_RATIO}

            echo "======== check AEP ========"
            ./build/bin/petps_server --key_space_m={key_space_m} --logtostderr \
                --value_size={value_size} --check_all_inserted=true --use_sglist=true \
                --max_kv_num_per_request=500 --thread_num=32 --db={db} \
                --num_server_processes={len(hosts)} --global_id={i} --numa_id={numa_id} --warmup_ratio={WARM_UP_RATIO}
            """
            RemoteExecute(each, command, PROJECT_ABS_PATH)
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
            for i, (each, numa_id) in enumerate(hosts):
                executor.submit(command_fn, i, each, numa_id)
    else:
        print("no preload when init index")

    print(f"""<Init {db}> done""")


def InitDouble(hosts, key_space_m, value_size, warm_thread_num):
    if type(hosts) is not list:
        hosts = [hosts]
    print(f"""<InitDoublePM>, with
          hosts={hosts}
          key_space_m={key_space_m}
          value_size={value_size}
          warm_thread_num={warm_thread_num}
          """)

    InitPMToDevDax(hosts)
    if PRELOAD_WHEN_INIT:
        for i, (each, numa_id) in enumerate(hosts):
            command = f"""./build/bin/petps_server  --key_space_m={key_space_m}  --logtostderr \
                            --value_size={value_size} --preload=true --use_sglist=true \
                            --max_kv_num_per_request=500  --thread_num=16 --warmup_thread_num={warm_thread_num} \
                            --num_server_processes={len(hosts)} --global_id={i} --numa_id={numa_id} --warmup_ratio={WARM_UP_RATIO}

                        ./build/bin/petps_server --key_space_m={key_space_m} --logtostderr \
                            --value_size={value_size} --check_all_inserted=true --use_sglist=true \
                            --max_kv_num_per_request=500 --thread_num=32 \
                            --num_server_processes={len(hosts)} --global_id={i} --numa_id={numa_id} --warmup_ratio={WARM_UP_RATIO}
            """
            RemoteExecute(each, command, PROJECT_ABS_PATH)
    else:
        print("no preload when init index")

    print(f"""<InitDoublePM> done""")


def InitIndexPM(hosts, db, key_space_m, value_size, warm_thread_num):
    if db == 'KVEnginePetKV' or db == "KVEnginePetKV":
        InitDouble(hosts, key_space_m, value_size, warm_thread_num)
    elif db == 'KVEngineF14':
        InitPMToDevDax(hosts)
    elif db == 'KVEngineFakeKV':
        InitPMToDevDax(hosts)
    else:
        InitFSDaxIndex(hosts, db, key_space_m, value_size, warm_thread_num)


def InitIndexDRAM(hosts, db, key_space_m, value_size, warm_thread_num):
    for i, (each, numa_id) in enumerate(hosts):
        command = f"""rm -rf /media/aep{numa_id}/dram-placeholder"""
        RemoteExecute(each, command, PROJECT_ABS_PATH)


def InitIndex(hosts, db, use_dram, key_space_m, value_size, warm_thread_num):
    if use_dram == 'false':
        InitIndexPM(hosts, db, key_space_m, value_size, warm_thread_num)
    elif use_dram == 'true':
        InitIndexDRAM(hosts, db, key_space_m, value_size, warm_thread_num)
    else:
        assert False, "use_dram is invalid"


class ParameterServerRun(CSRun):
    def __init__(self, ps_servers, client_servers, exp_id, run_id, log_dir, server_config, client_config, ) -> None:
        super().__init__(ps_servers, client_servers, exp_id, run_id,
                         log_dir, server_config, client_config,  "./build/bin/petps_server", "./build/bin/benchmark_client")

    def check_config(self,):
        super().check_config()
        if self.client_config['batch_read_count'] > self.server_config['max_kv_num_per_request']:
            raise Exception(
                "assert batch_read_count <= max_kv_num_per_request")
        if len(self.ps_servers) == 1:
            if self.client_config['batch_read_count'] * 8 + 40 >= 4096:
                raise Exception(
                    "message size too large")

    def CheckProcessLive(self, servers, pattern) -> bool:
        for host, numa_id in servers:
            ret = RemoteExecute(
                # host, f"ps ux | grep {pattern} | grep numa_id={numa_id}|grep -v grep >/dev/null 2>&1", "", print_show=True)
                host, f"ps aux | grep {pattern} | grep numa_id={numa_id}|grep -v grep ", "", print_show=False)
            if ret != 0:
                return False
        return True

    def _ClientWaitServer(self):
        for ps_id, _ in enumerate(self.ps_servers):
            ret = subprocess.run(
                f"grep 'I open mlx' {self.log_dir}/ps_{ps_id} >/dev/null 2>&1", shell=True).returncode
            sleep_seconds = 0
            while ret != 0:
                ret = subprocess.run(
                    f"grep 'I open mlx' {self.log_dir}/ps_{ps_id} >/dev/null 2>&1", shell=True).returncode
                time.sleep(5)
                sleep_seconds += 5
                if sleep_seconds > 45*60:
                    for _ in range(100):
                        print("DEADLOCK in _ClientWaitServer")
                    break

    def run(self):
        super().run()
        sleep_seconds = 0
        for client_id, _ in enumerate(self.client_servers):
            while True:
                ret = subprocess.run(
                    f"grep 'All client threads stopped' {self.log_dir}/client_{client_id} >/dev/null 2>&1", shell=True).returncode
                if ret == 0:
                    break
                time.sleep(5)
                sleep_seconds += 5
                if sleep_seconds > 30*60:
                    for _ in range(100):
                        print("DEADLOCK in wait client finish")
                    break

        print("tail down")
        Pnuke(ConvertHostNumaList2Host(self.client_servers), "benchmark_client")
        Pnuke(ConvertHostNumaList2Host(self.ps_servers), "petps_server")


class ParameterServerExperiment(CSExperiment):
    def __init__(self, exp_id, common_config, server_config, client_config, ps_servers, client_servers) -> None:
        super().__init__(exp_id, common_config, server_config,
                         client_config, ps_servers, client_servers)

    def _PostprocessConfig(self, server_config, client_config):
        # don't use self
        client_config['key_space_m'] *= WARM_UP_RATIO
        client_config['key_space_m'] = int(client_config['key_space_m'])

    def _CreateRun(self, run_id, run_log_dir, run_server_config, run_client_config,):
        return ParameterServerRun(self.ps_servers, self.client_servers,
                                  self.exp_id, run_id, run_log_dir,
                                  run_server_config, run_client_config)

    def _BeforeStartAllRun(self):
        print("pnuke petps_server/benchmark_client")
        Pnuke(ALL_SERVERS_INCLUDING_NOT_USED, "petps_server")
        Pnuke(ALL_SERVERS_INCLUDING_NOT_USED, "benchmark_client")


def ConfigSameInTheseKeys(previous_dict, next_dict, keys):
    if type(keys) is not list:
        keys = [keys]
    for each in keys:
        if previous_dict[each] != next_dict[each]:
            return False
    return True


COMMON_CLIENT_CONFIGS = {
    "thread_cut_off": ['true'],
    # "thread_cut_off": ['false'],
}


class ExpEnd2End(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "overall"
        COMMON_CONFIGS = {
            "value_size": [512],
            "key_space_m": [250],
            # "key_space_m": [25],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "binding": [
                {
                    "db": ["KVEnginePetKV"],
                    "use_sglist": ['false', 'true'],
                },
                {
                    "db": [
                        # "KVEnginePersistShmKV",
                        "KVEngineMap",
                        "KVEngineDash",
                        "KVEngineLevel",
                        # "KVEngineClevel",
                        "KVEngineCCEHVM",
                        "KVEngineMapPM",
                    ],
                    "use_sglist": ['false'],
                },
                # {
                #     "db": ["KVEngineF14"],
                #     "use_sglist": ['true', 'false', ],
                #     "warmup_thread_num": [1],
                # },
            ],
            "preload": ["false" if PRELOAD_WHEN_INIT else "true"],
            "thread_num": [SERVER_THREAD_NUM],
            "use_dram": ['false', ],
            "prefetch_method": [1]
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "batch_read_count": [500],
            # "dataset": ["zipfian", "dataset"],
            "dataset": ["zipfian", ],
            "binding": [
                {
                    "thread_num": [24],
                    "async_req_num": [2], },
            ],
        }
        self.name = NAME
        super().__init__(0, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        return list(sorted(runs, key=lambda run: run.server_config['db']))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return
        warm_thread_num = 16
        if (not PRELOAD_WHEN_INIT) or previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            InitIndex(self.ps_servers, db, use_dram, key_space_m,
                      value_size, warm_thread_num)
            return


class ExpDRAM(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "overall-single-machine-DRAM"
        COMMON_CONFIGS = {
            "value_size": [512],
            "key_space_m": [250],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "binding": [
                {
                    "db": ["KVEnginePetKV"],
                    "use_sglist": ['true', ],
                    "use_dram": ['false', ],
                },
                {
                    "db": ["KVEnginePersistShmKV"],
                    "use_sglist": ['false', ],
                    "use_dram": ['true', ],
                },
            ],
            "preload": ["false" if PRELOAD_WHEN_INIT else "true"],
            "thread_num": [SERVER_THREAD_NUM],
            "prefetch_method": [1]
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "batch_read_count": [500],
            "dataset": ["zipfian", "dataset"],
            "binding": [
                {
                    "thread_num": [24],
                    "async_req_num": [2], },
            ],
        }
        self.name = NAME
        super().__init__(9, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        return list(sorted(runs, key=lambda run: run.server_config['db']))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return
        warm_thread_num = 16
        if not PRELOAD_WHEN_INIT:
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            if use_dram == 'true':
                InitIndexDRAM(SINGLE_PS_SERVERS, db, key_space_m,
                              value_size, warm_thread_num)
            else:
                InitIndexPM(SINGLE_PS_SERVERS, db, key_space_m,
                            value_size, warm_thread_num)
            return

        if previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            if use_dram == 'true':
                InitIndexDRAM(SINGLE_PS_SERVERS, db, key_space_m,
                              value_size, warm_thread_num)
            else:
                InitIndexPM(SINGLE_PS_SERVERS, db, key_space_m,
                            value_size, warm_thread_num)


class ExpSensitive(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "overall-single-sensitive-value_size"
        COMMON_CONFIGS = {
            # "value_size": [512, 256, 128, 64],
            "value_size": [768, 1024],
            "key_space_m": [250],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "binding": [
                # {
                #     "db": ["KVEnginePetKV"],
                #     "use_sglist": ['true'],
                # },
                # {
                #     "db": ["KVEnginePersistShmKV",
                #            "KVEngineDash",
                #            "KVEngineMapPM",
                #            ],
                #     "use_sglist": ['false'],
                # },
                # {
                #     "db": ["KVEnginePetKV"],
                #     "use_sglist": ['true'],
                # },
                {
                    "db": [
                        # "KVEnginePersistShmKV",
                        #    "KVEngineDash",
                        "KVEngineMapPM",
                    ],
                    "use_sglist": ['false'],
                },
                # {
                #     "db": ["KVEngineF14"],
                #     "use_sglist": ['true'],
                #     "warmup_thread_num": [1],
                # }
            ],
            "preload": ["false" if PRELOAD_WHEN_INIT else "true"],
            "thread_num": [SERVER_THREAD_NUM],
            "use_dram": ['false', ],
            "prefetch_method": [1]
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "batch_read_count": [500],
            # "dataset": ["zipfian", "dataset"],
            "dataset": ["dataset"],
            "binding": [
                {
                    "thread_num": [24],
                    "async_req_num": [2], },
            ],
        }
        self.name = NAME
        super().__init__(201, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        return list(sorted(runs, key=lambda run: run.server_config['db']))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return
        warm_thread_num = 16
        if (not PRELOAD_WHEN_INIT) or previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            InitIndex(self.ps_servers, db, use_dram, key_space_m,
                      value_size, warm_thread_num)
            return


class ExpSensitiveWrite(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "overall-sensitive-write"
        COMMON_CONFIGS = {
            "value_size": [512],
            "key_space_m": [250],
            # "key_space_m": [5],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "binding": [
                {
                    "db": ["KVEnginePetKV"],
                    "use_sglist": ['true'],
                },
                {
                    "db": [
                        # "KVEnginePersistShmKV",
                        "KVEngineDash",
                        "KVEngineMultiMapPM",
                    ],
                    "use_sglist": ['false'],
                },
            ],
            "preload": ["false" if PRELOAD_WHEN_INIT else "true"],
            "thread_num": [SERVER_THREAD_NUM],
            "use_dram": ['false', ],

            "prefetch_method": [1]
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "read_ratio": [50, 70, 90, 95, 100],
            "batch_read_count": [500],
            "dataset": ["zipfian", ],
            "binding": [
                {
                    "thread_num": [24],
                    "async_req_num": [2], },
            ],
        }
        self.name = NAME
        super().__init__(2222, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        return list(sorted(runs, key=lambda run: run.server_config['db']))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return
        warm_thread_num = 16
        if (not PRELOAD_WHEN_INIT) or previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            InitIndex(self.ps_servers, db, use_dram, key_space_m,
                      value_size, warm_thread_num)
            return


class ExpSensitiveBatchCount(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "overall-single-sensitive-batchcount"
        COMMON_CONFIGS = {
            "value_size": [512],
            "key_space_m": [250],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "binding": [
                {
                    "db": ["KVEnginePetKV"],
                    "use_sglist": ['false', 'true'],
                },
                {
                    "db": ["KVEnginePersistShmKV",
                           "KVEngineDash",
                           "KVEngineMapPM",
                           ],
                    "use_sglist": ['false'],
                },
            ],
            "preload": ["false" if PRELOAD_WHEN_INIT else "true"],
            "thread_num": [SERVER_THREAD_NUM],
            "use_dram": ['false', ],
            "prefetch_method": [1]
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "batch_read_count": [100, 200, 400, 500],
            # "dataset": ["zipfian", "dataset"],
            "dataset": ["dataset"],
            "binding": [
                {
                    "thread_num": [24],
                    "async_req_num": [2], },
            ],
        }
        self.name = NAME
        super().__init__(202, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        return list(sorted(runs, key=lambda run: run.server_config['db']))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return
        warm_thread_num = 16
        if (not PRELOAD_WHEN_INIT) or previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            InitIndex(self.ps_servers, db, use_dram, key_space_m,
                      value_size, warm_thread_num)
            return


class Experiment2(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "test hotness"
        COMMON_CONFIGS = {
            "value_size": [512],
            "key_space_m": [250],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "binding": [
                {
                    "db": ["KVEnginePetKV",
                           "KVEngineF14",

                           ],
                    "warmup_thread_num": [1, ],
                },
                {
                    "db": ["KVEnginePersistShmKV",
                           "KVEngineDash",
                           "KVEngineLevel", "KVEngineClevel",
                           "KVEngineCCEHVM", "KVEngineMapPM",
                           ],
                    "warmup_thread_num": [32],
                }
            ],
            "thread_num": [SERVER_THREAD_NUM],
            # NOTE: 如果是Dash,不能用sglist=true
            "use_sglist": ['false'],
            "use_dram": ['false', ],
            "prefetch_method": [1],
            "preload": ["false" if PRELOAD_WHEN_INIT else "true"],
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "batch_read_count": [500],
            "zipf_theta": [0, 0.9, 0.99],
            "dataset": ["zipfian", ],
            "binding": [
                {"thread_num": [24, ],
                 "async_req_num": [2], },
            ],
        }

        self.name = NAME
        super().__init__(2, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        return list(sorted(runs, key=lambda run: run.server_config['warmup_thread_num']))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return

        if (not PRELOAD_WHEN_INIT) or previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            warm_thread_num = next_run.server_config["warmup_thread_num"]
            InitIndex(self.ps_servers, db, use_dram, key_space_m,
                      value_size, warm_thread_num)
            return


class ExpFakeKV(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "test sglist; DRAM v.s. AEP"
        COMMON_CONFIGS = {
            "value_size": [512, ],
            "key_space_m": [100],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "db": ["KVEngineFakeKV"],
            "thread_num": [SERVER_THREAD_NUM],
            "use_sglist": ['true', 'false', ],
            "use_dram": ['false', 'true', ],
            "fake_kv_index_sleepns": [0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000],
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "dataset": ["zipfian", ],
            "zipf_theta": [0.99],
            "batch_read_count": [500],
            "binding": [
                {"thread_num": [18],
                    "async_req_num": [2], },
            ],
        }
        self.name = NAME
        super().__init__(5, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        for each in runs:
            pprint(each.__str__())
        return list(sorted(runs, key=lambda run: (run.server_config['key_space_m'], run.server_config['value_size'])))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return
        warm_thread_num = 16
        if (not PRELOAD_WHEN_INIT) or previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            # warm_thread_num = next_run.server_config["warmup_thread_num"]
            InitIndex(self.ps_servers, db, use_dram, key_space_m,
                      value_size, warm_thread_num)
            return


class ExpMotivation(ParameterServerExperiment):
    def __init__(self, ) -> None:
        NAME = "motivation"
        COMMON_CONFIGS = {
            "value_size": [512],
            "key_space_m": [250],
            "max_kv_num_per_request": [500],
            "logtostderr": ['true']
        }
        SERVER_CONFIGS = {
            "db": ["KVEngineDash", "KVEngineLevel", "KVEngineCCEHVM"],
            "thread_num": [SERVER_THREAD_NUM],
            "use_sglist": ['false', ],
            "use_dram": ['false', ],
        }

        CLIENT_CONFIGS = {
            **COMMON_CLIENT_CONFIGS,
            "dataset": ["zipfian", ],
            "batch_read_count": [500],
            "binding": [
                {"thread_num": [24, ],
                    "async_req_num": [2], },
            ],
        }
        self.name = NAME
        super().__init__(7, COMMON_CONFIGS, SERVER_CONFIGS,
                         CLIENT_CONFIGS, SINGLE_PS_SERVERS, SINGLE_CLIENT_SERVERS)

    def _SortRuns(self, runs):
        return list(sorted(runs, key=lambda run: run.server_config['db']))

    def _RunHook(self, previous_run, next_run):
        if next_run is None:
            return
        if (not PRELOAD_WHEN_INIT) or previous_run is None or \
                (not ConfigSameInTheseKeys(previous_run.server_config,
                                           next_run.server_config,
                                           ['key_space_m', 'db', 'value_size'])):
            # i.e., preload every time run
            db = next_run.server_config["db"]
            key_space_m = next_run.server_config["key_space_m"]
            value_size = next_run.server_config["value_size"]
            use_dram = next_run.server_config["use_dram"]
            warm_thread_num = 16
            InitIndex(self.ps_servers, db, use_dram, key_space_m,
                      value_size, warm_thread_num)
            return
