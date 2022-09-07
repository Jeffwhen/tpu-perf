_harness_functions = dict()

def load_plugins():
    from .. import util
    util.load_plugins('harness')

def harness(key):
    def register(fn):
        _harness_functions[key] = fn
    return register

def get_harness(key):
    return _harness_functions[key]
