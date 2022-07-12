import os
import re
import subprocess
import logging
from pprint import pprint

def bulkize(l, n):
    for i in range(0, len(l), n):
        end = min(len(l), i + n)
        yield l[i:end]

def sys_memory_size():
    with open('/proc/meminfo') as f:
       line = next(f)
       m = re.match('^MemTotal:\s+(\d+) kB', line)
       if not m:
           logging.error('Failed to parse memory info')
           raise RuntimeError
       return int(m.group(1))

class CommandExecutor:
    def __init__(self, cwd, env):
        import os
        self.env = os.environ.copy()
        for v in env:
            pair = v.split('=')
            self.env[pair[0].strip()] = pair[1].strip() if len(pair) > 1 else ""
        mem_size = sys_memory_size()
        max_threads = max(1, int(mem_size / 1024 / 1024 / 4))
        self.threads = 4
        if self.threads > max_threads:
            self.threads = max_threads
        self.cwd = cwd
        self.procs = []

    def run(self, *args, **kw_args):
        self.put(*args, **kw_args)
        self.wait()

    def put(self, title, *args, **kw_args):
        if 'cwd' not in kw_args:
            kw_args['cwd'] = self.cwd
        if 'shell' not in kw_args:
            kw_args['shell'] = True
        kw_args['env'] = dict(**self.env, **kw_args.get('env', dict()))
        self.procs.append((title, args, kw_args))

    def drain(self):
        for bulk in bulkize(self.procs, self.threads):
            logs = []
            procs = []
            for title, args, kw_args in bulk:
                cmd_fn = os.path.join(self.cwd, f'{title}.cmd')
                with open(cmd_fn, 'w') as f:
                    pprint(args, f)
                    pprint(kw_args, f)
                    f.write(f'\n\n---------------\n{args}\n')
                log_fn = os.path.join(self.cwd, f'{title}.log')
                log = open(log_fn, 'w')
                p = subprocess.Popen(*args, **kw_args, stdout=log, stderr=log)
                logs.append((log_fn, log))
                procs.append(p)
            for i, p in enumerate(procs):
                log_fn, log = logs[i]
                ret = p.wait()
                log.close()
                if ret != 0:
                    logging.error(f'Command failed, please check {log_fn}')
                    raise RuntimeError('Command failed')

    def wait(self):
        try:
            self.drain()
        finally:
            self.procs.clear()
