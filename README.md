TPU Perf
========

### Prerequisites

Install protoc version 3.19 in order to be compatible with ufw.

```bash
mkdir build
cd build
cmake ..
make install
```

### Usage

Refer to [example](./example) for how to create test specs.

Execute from SDK environment.

```bash
python3 -m tpu_perf.build --time # To build efficiency test models
python3 -m tpu_perf.build # To build precision test models
python3 -m tpu_perf.run # To run efficency test
python3 -m tpu_perf.precision_benchmark # To run precision benchmark
```

### config.yaml

Required and preset varibles

| Name                  | Type      | Usage                                                                         |
|-----------------------|-----------|-------------------------------------------------------------------------------|
| root                  | Preset    | Repo top directory                                                            |
| home                  | Preset    | Current model directory                                                       |
| workdir               | Preset    | Current model working directory                                               |
| lmdb\_out             | Preset    | Dataset output dir. Only set if lmdb field exists.                            |
| shape                 | Preset    | Current shape list. Only set if shapes field exists.                          |
| shape\_key            | Preset    | Current shape string like "1x3x224x224". Only set if shapes field exists.     |
| shape\_param          | Preset    | Current shape string like "[1,3,224,224]". Only set if shapes field exists.   |
| name                  | Required  | Specify network name, should be unique                                        |
| cali                  | Optional  | Calibration command. Will do calibrating if this variable exists              |
| bmnetu\_options       | Optional  | bmnetu compile options. Will do int8 compiling if this variable exists        |
| bmnetu\_batch\_sizes  | Optional  | Specify int8 output batches                                                   |

Variables in top level `config.yaml` are also set for all models.

| Name          | Usage                                                                     |
|---------------|---------------------------------------------------------------------------|
| nnmodels      | Path of nnmodels repo                                                     |
