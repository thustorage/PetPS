
import argparse
import itertools
import subprocess
import datetime

from bench_util import Pnuke


# RUNNING_SECONDS = 100
RUNNING_SECONDS = 100
# EXP_SELECT = [1, 2, 3]
EXP_SELECT = [2]
LOG_DIR = "log_KVEngineBench"
LOG_DIR = "log_KVEngineBench_Write"

CONFIGS = []
if 1 in EXP_SELECT:
    # Exp 1
    CONFIG = {
        "thread_num": [18, ],
        "key_space_m": [250],
        "binding": [
            {
                "db": ['KVEnginePetKV'],
                "prefetch_method": [1],
                "warmup_thread_num":[36],
            },
            # {
            #     "db": ['KVEnginePetKV'],
            #     "prefetch_method": [1],
            #     "warmup_thread_num":[1],
            # },
            {
                "db": ['KVEngineF14'],
                "prefetch_method": [1],
                "warmup_thread_num":[1],
            },
            # {
            #     "db": ['KVEngineF14'],
            #     "prefetch_method": [0],
            #     "warmup_thread_num":[1],
            # },
            # {
            #     "db": ['KVEngineF14'],
            #     "prefetch_method": [0],
            #     "warmup_thread_num":[36],
            # },
            {
                "db": [
                    'KVEngineDash',
                    # 'KVEngineLevel',
                    # 'KVEngineClevel',
                    # 'KVEngineCCEHVM',
                    'KVEngineMapPM',
                ],
            },

            # {
            #     "db": [
            #         # 'KVEngineLevel',
            #         'KVEngineClevel',
            #         # 'KVEngineCCEHVM',
            #     ],
            #     "warmup_thread_num":[1],
            # },
        ],
        "binding2": [
            # {
            #     "zipf_theta": [0, 0.9, 0.99],
            #     "dataset": ["zipfian", ],
            # },
            # {"dataset": ["dataset", ]}
            {"dataset": ["zipfian", ]}
        ],
        "batch_read_count": [500],
        "value_size": [512],
    }
    CONFIGS.append(CONFIG)


if 2 in EXP_SELECT:
    # Exp 1
    CONFIG = {
        # "thread_num": [9, 18, 27, 36],
        "thread_num": [18, ],
        "read_ratio": [50, 65, 80, 95, 100],
        "key_space_m": [250],
        "binding": [
            {
                "db": ['KVEnginePetKV'],
                "prefetch_method": [1],
                "warmup_thread_num":[36],
            },
            # {
            #     "db": ['KVEngineF14'],
            #     "prefetch_method": [1],
            #     "warmup_thread_num":[1],
            # },
            # {
            #     "db": ['KVEngineF14'],
            #     "prefetch_method": [0],
            #     "warmup_thread_num":[1],
            # },
            # {
            #     "db": ['KVEngineF14'],
            #     "prefetch_method": [0],
            #     "warmup_thread_num":[36],
            # },
            {
                "db": [
                    'KVEngineDash',
                    # 'KVEngineLevel',
                    # 'KVEngineClevel',
                    # 'KVEngineCCEHVM',
                    # 'KVEngineMapPM',
                    # "KVEngineMultiMapPM",

                ],
            },

            # 补测
            # {
            #     "db": [
            #         # 'KVEngineLevel',
            #         'KVEngineClevel',
            #         # 'KVEngineCCEHVM',
            #     ],
            #     "warmup_thread_num":[1],
            # },
        ],
        "binding2": [
            # {
            #     "zipf_theta": [0, 0.9, 0.99],
            #     "dataset": ["zipfian", ],
            # },
            # {"dataset": ["dataset", ]}
            {"dataset": ["zipfian", ]}
        ],
        "batch_read_count": [500],
        "value_size": [512],
    }
    CONFIGS.append(CONFIG)

parser = argparse.ArgumentParser(description='')
parser.add_argument("--rank", default=0, type=int)
FLAGS = parser.parse_args()


def gen_product(config):
    keys, values = zip(*config.items())
    permutations_config = [dict(zip(keys, v))
                           for v in itertools.product(*values)]
    return permutations_config


def gen_binding(config_list):
    r = []
    for each in config_list:
        r += gen_product(each)
    return r


def disjoint_dicts_to_one_dict(dicts):
    a = dicts[0].copy()
    for i in range(1, len(dicts)):
        a.update(dicts[i])
    return a


def get_next_config(CONFIG):
    CONFIG_BINDING = CONFIG['binding']
    del CONFIG['binding']
    CONFIG_BINDING_2 = CONFIG['binding2']
    del CONFIG['binding2']
    for each_k in CONFIG.keys():
        CONFIG[each_k] = [str(each) for each in CONFIG[each_k]]

    permutations_config = gen_product(CONFIG)
    permutations_binding_config = gen_binding(CONFIG_BINDING)
    permutations_binding_config_2 = gen_binding(CONFIG_BINDING_2)

    permutations_config = itertools.product(
        permutations_config, permutations_binding_config)
    # [(dictA, dictB), (dictA, dictB), (dictA, dictB),]
    permutations_config = [disjoint_dicts_to_one_dict(each)
                           for each in permutations_config]
    permutations_config = list(permutations_config)
    permutations_config = itertools.product(
        permutations_config, permutations_binding_config_2)
    permutations_config = [disjoint_dicts_to_one_dict(each)
                           for each in permutations_config]

    for each in permutations_config:
        print(each)
        yield each


# deduplicate runned config
def find_runned_config(logdir):
    import re
    import os

    ret = []
    for each in os.listdir(logdir):
        if re.search(r'run_(\d+)', each):
            with open(f'{logdir}/{each}/config', 'r') as f:
                import json
                config = json.load(f)
            del config['cli']
            ret.append(config)
    return ret


def deduplicate_runned(logdir, to_do_configs):
    def dict_in_dictlist(dic, dic_list):
        for each in dic_list:
            if each == dic:
                return True
        return False
    already_runned_config = find_runned_config(logdir)
    ret = []
    for each in to_do_configs:
        if dict_in_dictlist(each, already_runned_config):
            continue
        ret.append(each)
    return ret


# assign run_id
def find_start_run_id(logdir):
    import re
    import os
    ids = [int(re.search(r'run_(\d+)', each)[1])
           for each in os.listdir(logdir)]
    if len(ids) != 0:
        max_id = max(ids)
    if len(ids) == 0:
        max_id = -1
    return max_id + 1


config_list = []
for each_config in CONFIGS:
    config_generator = get_next_config(each_config)
    config_list = config_list + list(config_generator)


# config_list = deduplicate_runned(f'{LOG_DIR}/0', config_list)

run_id_start = find_start_run_id(f'{LOG_DIR}/0')
all_configs = list(
    zip(range(run_id_start, run_id_start + len(config_list)), config_list))

if FLAGS.rank == 1:
    all_configs = list(reversed(all_configs))

for jdt, (run_id, config) in enumerate(all_configs):
    Pnuke("127.0.0.1", "perf_kv_engine")
    print(datetime.datetime.now(),
          f'{jdt} / {len(all_configs)}', flush=True)

    subprocess.run("rm -rf /media/aep0/*", shell=True)
    log_dir = f"{LOG_DIR}/{FLAGS.rank}/run_{run_id}/"
    subprocess.run(f"mkdir -p {log_dir}", shell=True)

    config_cli = ' '.join(
        [f'--{k}={v}' for k, v in config.items()])
    execute_str = f'''../build/bin/perf_kv_engine \
        {config_cli} --logtostderr \
        --running_seconds={RUNNING_SECONDS} \
        >"{log_dir}/log" 2>&1'''
    config['cli'] = execute_str
    with open(f'{log_dir}/config', 'w') as f:
        import json
        json.dump(config, f, indent=2)

    print(execute_str, flush=True)
    subprocess.run("rm -rf /media/aep0/*", shell=True)
    p = subprocess.Popen(execute_str, shell=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for line in p.stdout.readlines():
        print(line)
    retval = p.wait()
