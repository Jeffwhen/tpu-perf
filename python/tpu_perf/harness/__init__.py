_harness_functions = dict()

def load_plugins():
    import logging
    import importlib
    try:
        importlib.import_module('harness')
    except ModuleNotFoundError:
        pass

def harness(key):
    def register(fn):
        _harness_functions[key] = fn
    return register

def get_harness(key):
    return _harness_functions[key]
