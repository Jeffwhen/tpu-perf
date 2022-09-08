import logging
import importlib
import os

def load_plugins(name):
    for dirpath, dirnames, filenames in os.walk('.'):
        for dn in dirnames:
            if dn != name:
                continue
            import_path = os.path.relpath(os.path.join(dirpath, dn), '.')
            import_path = import_path.replace('/', '.')
            try:
                importlib.import_module(import_path)
            except ModuleNotFoundError as err:
                if err.name not in import_path:
                    raise err
                logging.warning('No dataset plugin')

def dict_override(a, b):
    r = a.copy()
    for k, v in b.items():
        r[k] = v
    return r

from datetime import timedelta
def format_seconds(s):
    delta = timedelta(seconds=s)
    pairs = zip(
        [int(v.split('.')[0]) for v in str(delta).split(':')],
        ['hours', 'minutes', 'seconds'])
    return ' '.join(f'{v} {u}' for v, u in pairs if v) or '0 second'
