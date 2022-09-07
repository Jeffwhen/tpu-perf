
_preprocess_functions = dict()

def load_plugins():
    from . import util
    util.load_plugins('dataset')

def preprocess_method(key):
    def register(fn):
        _preprocess_functions[key] = fn
    return register

def get_preprocess_method(key):
    return _preprocess_functions[key]
