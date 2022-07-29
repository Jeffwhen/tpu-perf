import os
import re
import csv
import psutil
import sys
import time
import logging
from .buildtree import check_buildtree, BuildTree
from .subp import CommandExecutor

option_cmodel_stats = False

class Average:
    def __init__(self):
        self.clear()

    def put(self, v):
        self.acc += v
        self.count += 1

    def get(self):
        return self.acc / self.count

    def clear(self):
        self.acc = 0
        self.count = 0

def parse_stats(string):
    time_prog = 'INFO:(.+) time\(s\): ([\.\d]+)'
    ret = dict()
    for k, v in re.findall(time_prog, string):
        k = k.strip().replace(' ', '_')
        if k not in ret:
            ret[k] = Average()
        ret[k].put(float(v))
    for k in ret.keys():
        ret[k] = ret[k].get()

    shape_prog = 'Input \d+\).+shape=\[ (\d+) (\d+) (\d+) (\d+) \]'
    shape_info = ':'.join('x'.join(s) for s in re.findall(shape_prog, string))
    ret['shape'] = shape_info

    return ret

def parse_profile(fn):
    with open(fn) as f:
        lines = f.read()
    lines = lines[lines.find('API_END'):]
    data = dict()
    for pair in re.finditer('(\w+) *: *([\d\.]+)', lines):
        v = pair.group(2)
        data[pair.group(1)] = float(v) if '.' in v else int(v)
    return data

def format_float(v):
    if v > 0.1:
        return f'{v:.03f}'
    else:
        return f'{v:.03e}'

def run(tree, path, config, stat_f):
    if not config.get('time', True):
        return
    workdir = config['workdir']
    env = [
        tree.expand_variables(config, v)
        for v in config.get('run_env', [])]
    pool = CommandExecutor(workdir, env)
    rounds = config.get('time_rounds', 2000)
    for b in config['bmnetu_batch_sizes']:
        name = f'{b}b.compilation'
        bmodel_dir = os.path.join(workdir, name)
        name = os.path.join(os.path.basename(workdir.strip('/')), name)
        bmodel = os.path.join(bmodel_dir, 'compilation.bmodel')
        if not os.path.exists(bmodel):
            logging.warning(f'{bmodel} does not exist')
            continue
        info = None

        profile_fn = 'compiler_profile_0.txt'
        profile_path = os.path.join(bmodel_dir, profile_fn)
        if os.path.exists(profile_path):
            info = parse_profile(profile_path)
            rounds = int(1200 / info['runtime'])
            logging.info(f'Run {rounds} times for {name}.{b}b')

        title = f'run.{b}'
        ref_fn = os.path.join(bmodel_dir, 'output_ref_data.dat')
        env = {'BMRUNTIME_PROFILE_OUT_DIR': f'{b}b.profiledata'}
        if os.path.exists(ref_fn) and os.path.getsize(ref_fn):
            logging.info(f'Runtime test {name}')
            pool.put(
                title,
                ['bmrt_test', '--loopnum', str(rounds), '--context', bmodel_dir],
                env=env, shell=False)
        else:
            logging.info(f'Runtime test {name} without reference')
            pool.put(
                title,
                ['bmrt_test', '--loopnum', str(rounds), '--bmodel', bmodel],
                env=env, shell=False)
        try:
            pool.fire()
            pid = pool.pipes[0].pid
            p = psutil.Process(pid)
            cpu_percent = p.cpu_percent(interval=1) / 100
            pool.drain()
            pool.procs.clear()
        except RuntimeError:
            logging.error(f'Runtime test {name} failed')

        log_fn = os.path.join(workdir, f'{title}.log')
        with open(log_fn) as f:
            stats = parse_stats(f.read())
        from math import nan
        real_time = stats['calculate'] * 1000 if 'calculate' in stats else nan
        row = [
            config['name'],
            stats['shape'],
            format_float(config['gops'] * b),
            format_float(real_time)]

        # If profile exists, calculate mac & ddr utilization
        if info is not None:
            s2l = info['S2L']
            l2s = info['L2S']
            s2s = info['S2S']
            calc_mac_util = lambda t: config['gops'] * b / t / 32
            calc_ddr_bandwidth = lambda t: \
                (s2l + l2s + s2s * 2) / t * 1000 / 1024**3 / 64

            est_time = info['runtime']
            if option_cmodel_stats:
                row.append(format_float(est_time))
            if 'gops' not in config:
                logging.warning(
                    f'Profile exists but no GOPs in config.yaml, {config["name"]}')
                row.extend(['N/A'] * (2 if option_cmodel_stats else 1))
            else:
                row.append(f'{calc_mac_util(real_time):.2%}')
                row.append(f'{cpu_percent:.2%}')
                if option_cmodel_stats:
                    row.append(f'{calc_mac_util(est_time):.2%}')
            row.append(f'{calc_ddr_bandwidth(real_time):.2%}')
            if option_cmodel_stats:
                row.append(f'{calc_ddr_bandwidth(est_time):.2%}')

        stat_f.writerow(row)

def main():
    logging.basicConfig(
        level=logging.INFO,
        format='[%(levelname)s %(filename)s:%(lineno)d] %(message)s')

    import argparse
    parser = argparse.ArgumentParser(description='tpu-perf benchmark tool')
    parser.add_argument(
        'models', metavar='MODEL', type=str, nargs='*',
        help='models to run')
    parser.add_argument('--cmodel', action='store_true')
    args = parser.parse_args()
    global option_cmodel_stats
    option_cmodel_stats = args.cmodel

    if not check_buildtree():
        sys.exit(1)

    tree = BuildTree(os.path.abspath('.'))
    stat_fn = os.path.join(tree.global_config['workdir'], 'stats.csv')
    with open(stat_fn, 'w') as f:
        csv_f = csv.writer(f)
        if option_cmodel_stats:
            csv_f.writerow([
                'name',
                'shape',
                'gops',
                'time',
                'cmodel_estimated_time',
                'mac_utilization',
                'cpu_usage',
                'cmodel_estimated_mac_utilization',
                'ddr_utilization',
                'cmodel_estimated_ddr_bandwidth'])
        else:
            csv_f.writerow([
                'name',
                'shape',
                'gops',
                'time',
                'mac_utilization',
                'cpu_usage',
                'ddr_utilization'])

        if not args.models:
            for path, config in tree.walk():
                run(tree, path, config, csv_f)
        else:
            for name in args.models:
                for path, config in tree.read_dir(name):
                    run(tree, path, config, csv_f)

if __name__ == '__main__':
    main()
