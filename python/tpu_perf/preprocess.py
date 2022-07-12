
_preprocess_functions = dict()

def load_plugins():
    import logging
    import importlib
    try:
        importlib.import_module('dataset')
    except ModuleNotFoundError:
        logging.warning('No dataset plugin')

def preprocess_method(key):
    def register(fn):
        _preprocess_functions[key] = fn
    return register

def get_preprocess_method(key):
    return _preprocess_functions[key]
