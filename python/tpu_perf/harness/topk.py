from tpu_perf.infer import SGInfer
from tpu_perf.blob_pb2 import Datum
from threading import Thread
from tpu_perf.harness import harness

import numpy as np
import lmdb

class Runner:
    def __init__(self, bmodel_fn, lmdb_path):
        self.model = SGInfer(bmodel_fn)
        self.worker = Thread(target=self.process)
        self.worker.start()
        self.lmdb_env = lmdb.open(lmdb_path)
        self.lmdb_txn = self.lmdb_env.begin()
        self.label = dict()
        self.stats = dict(count = 0, top1 = 0, top5 = 0)

    def run(self):
        lmdb_cursor = self.lmdb_txn.cursor()
        datum = Datum()
        input_info = self.model.get_input_info()
        input_info = next(iter(input_info.values()))
        batch_size = input_info['shape'][0]
        input_scale = input_info['scale']
        is_fp32 = input_scale == 1

        bulk_data = []
        bulk_label = []
        def enqueue():
            nonlocal bulk_data, bulk_label
            task_id = self.model.put(np.stack(bulk_data))
            self.label[task_id] = bulk_label
            bulk_data = []
            bulk_label = []
        for key, value in lmdb_cursor:
            datum.ParseFromString(value)
            data = np.array(datum.float_data, dtype=np.float32)
            dtype = np.float32
            if not is_fp32:
                data *= input_scale
                dtype = np.int8
            bulk_data.append(data.reshape(datum.shape.dim).astype(dtype))
            bulk_label.append(datum.label)
            if len(bulk_data) < batch_size:
                continue
            enqueue()
        if bulk_data:
            enqueue()
        self.model.put()

    def process(self):
        arg_results = dict()
        while True:
            task_id, results, valid = self.model.get()
            if task_id == 0:
                break
            output = results[0]
            output = output.reshape(output.shape[0], -1)
            argmaxs = np.argmax(output, axis=-1)
            topks = np.argpartition(output, -5)[:, -5:]
            arg_results[task_id] = (argmaxs, topks)
        for task_id, (argmaxs, topks) in arg_results.items():
            labels = self.label.pop(task_id)
            for label, argmax, topk in zip(labels, argmaxs, topks):
                self.stats['count'] += 1
                if label == argmax:
                    self.stats['top1'] += 1
                if label in topk:
                    self.stats['top5'] += 1

    def get_stats(self):
        stats = self.stats.copy()
        count = stats.pop('count')
        for k in stats:
            stats[k] /= count
        return stats

    def join(self):
        self.worker.join()

@harness('topk')
def harness_main(tree, config, args):
    bmodel = tree.expand_variables(config, args['bmodel'])
    lmdb = tree.expand_variables(config, args['lmdb'])
    runner = Runner(bmodel, lmdb)
    runner.run()
    runner.join()
    return runner.get_stats()

def main():
    import sys
    if len(sys.argv) != 3:
        print(f'{sys.argv[0]} <.bmodel> <.lmdb>')
        sys.exit(1)
    runner = Runner(*sys.argv[1:])
    runner.run()
    runner.join()
    print(runner.get_stats())

if __name__ == '__main__':
    main()
