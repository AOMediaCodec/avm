# AVM Stats Python API

This is a Python wrapper library around the protobuf frames dumped by `extract_proto`.

## Setup
1. Install the protobuf compiler, e.g.:
```bash
apt install libprotobuf-dev protobuf-compiler
```
For full compatibility with the Python bindings, protobuf-compiler version 3.19.0 or newer is *strongly* recommended; your package manager may have an outdated version. Refer to [protoc-installation](https://grpc.io/docs/protoc-installation/) for more details.

If installing a newer version is not an option, a workaround is to set this environment variable:
```bash
export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
```
This will allow older versions of protoc to work, but will result in much slower parsing, and potential protobuf decoding errors.

2. You may also need to initialize the git submodules for the Abseil dependency:
```bash
git submodule update --init
```

3. Build avm with `extract_proto` enabled.
For convenience, first define a few environment variables for paths that will be used repeatedly:
```bash
export AOM_ROOT=/path/to/avm/git/root
export AOM_BUILD_DIR=/path/to/avm/build/dir
export LIBAOM_TEST_DATA_PATH=/path/to/avm/testdata
```

Then build:
```bash
cmake -B ${AOM_BUILD_DIR} -S ${AOM_ROOT} -DCONFIG_ACCOUNTING=1 -DCONFIG_INSPECTION=1 -DCONFIG_EXTRACT_PROTO=1 -DENABLE_TESTDATA=1
make -C ${AOM_BUILD_DIR} -j all testdata
```
Note that `CONFIG_EXTRACT_PROTO` depends on both `CONFIG_INSPECTION` and `CONFIG_ACCOUNTING`.

4. (Optional) Create and activate a Python virtual environment, e.g.:
```bash
python3 -m venv /tmp/my_venv/
source /tmp/my_venv/bin/activate
```

5. Generate the Python bindings for the protobuf:
```bash
protoc --python_out=${AOM_ROOT}/tools/py_stats/avm_stats --proto_path=${AOM_ROOT}/tools/extract_proto avm_frame.proto
```

6. Install the avm_stats library and dependencies:
```bash
pip install -r ${AOM_ROOT}/tools/py_stats/requirements.txt
pip install ${AOM_ROOT}/tools/py_stats
```

## Unit Tests
1. Generate pytest test data. Note that this depends on AVM being built with `-DENABLE_TESTDATA=1`, and assumes `make -j testdata` has already been run.

Note that by default, `make -j testdata` will place the downloaded test data into `${AOM_BUILD_DIR}`, but it is recommended to set `LIBAOM_TEST_DATA_PATH` to explicitly set the test data path. See the testing section in the root level README file for more details.

```bash
${AOM_ROOT}/tools/py_stats/tests/generate_testdata.sh ${AOM_ROOT} ${AOM_BUILD_DIR} ${LIBAOM_TEST_DATA_PATH}
```

2. Run unit tests:
```bash
cd ${AOM_ROOT}/tools/py_stats && python3 -m pytest
```

## Examples
The examples folder contains multiple scripts that show some sample visualizations that can be built with this library.
All of the examples have two required arguments: `--stream` and `--extract_proto_bin`:
```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/${script_name}.py --stream /path/to/some/stream --extract_proto_bin /path/to/extract_proto/bin
```
Some of the examples may also optionally take a `--source` argument, which should be the path to the source file (i.e. pre-encoding) if available.
Note that both .yuv and .y4m files are supported. For a .yuv file, it is not necessary to provide an explicit width and height, since the AVM stream itself has that info.

All following example commands will assume that:
 - The extract_proto binary is available in `${AOM_BUILD_DIR}`
 - The test stream "park_joy_90_8_420.ivf" and its original YUV "park_joy_90_8_420.y4m" are available in `${LIBAOM_TEST_DATA_PATH}`

If you get an error like "UserWarning: FigureCanvasAgg is non-interactive, and thus cannot be shown" when trying to run an example script, you may also need to install pyqt5:
```bash
pip install PyQt5
```

### Pixel pipeline
This example shows the state of the pixels are various stages in the codec pipeline, including the original (source) pixels, the prediction, the residual, the reconstruction before and after filtering, and the distortion.

```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/pixel_pipeline.py \
    --stream ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.ivf \
    --source ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.y4m \
    --extract_proto_bin ${AOM_BUILD_DIR}/extract_proto \
    --frame 0 \
    --plane y
```
![](img/pixel_pipeline.png "Sample output from pixel_pipeline.py with park_joy_90p_8_420")

If `--source` is omitted, the visualizations that require the original pixels (e.g. distortion) will not be shown.
Note that `--frame` and `--plane` are optional and will default to the first frame and luma (Y) if unspecified.

### Partition tree
This example shows the partition tree with the following annotations:
 - Superblock boundaries (in blue)
 - Coding unit boundaries (in red)
 - Transform unit boundaries, for blocks with TX split (in green)
 - Prediction mode names
 - Motion vectors, for inter frames

```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/partition_tree.py \
    --stream ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.ivf \
    --extract_proto_bin ${AOM_BUILD_DIR}/extract_proto \
    --frame 0 \
    --plane y
```
![](img/partition_tree_luma_intra.png "Sample output from partition_tree.py with park_joy_90p_8_420 (intra frame)")

To see motion vectors as well, use `--frame 1` to visualize an inter frame:
```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/partition_tree.py \
    --stream ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.ivf \
    --extract_proto_bin ${AOM_BUILD_DIR}/extract_proto \
    --frame 1 \
    --plane y
```
![](img/partition_tree_luma_inter.png "Sample output from partition_tree.py with park_joy_90p_8_420 (inter frame)")

### Prediction modes
This example plots a pie chart showing the distribution of prediction mode decisions in the given frame.

```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/prediction_modes.py \
    --stream ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.ivf \
    --extract_proto_bin ${AOM_BUILD_DIR}/extract_proto \
    --frame 0 \
    --plane y
```
![](img/prediction_modes_luma_intra.png "Sample output from prediction_mode.py with park_joy_90p_8_420")

### Bit distribution heatmap
This example plots a heatmap showing where bits are spent in each frame.

```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/bits_heatmap.py \
    --stream ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.ivf \
    --extract_proto_bin ${AOM_BUILD_DIR}/extract_proto \
    --frame 0 \
    --plane y
```
![](img/bits_heatmap.png "Sample output from bits_heatmap.py with park_joy_90p_8_420")

This script takes an optional `--filter` argument to filter out only a specific type of bitstream symbols. Filtering is applied based on the name of the C function each particular symbol is read in. For example, to see where bits are spent on the intra luma prediction mode:
```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/bits_heatmap.py \
    --stream ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.ivf \
    --source ${LIBAOM_TEST_DATA_PATH}/park_joy_90p_8_420.y4m \
    --extract_proto_bin ${AOM_BUILD_DIR}/extract_proto \
    --frame 0 \
    --plane y \
    --filter read_intra_luma_mode
```
![](img/bits_heatmap_read_intra_luma_mode.png "Sample output from bits_heatmap.py with park_joy_90p_8_420 for symbol type 'read_intra_luma_mode'")

To see available symbol types, refer to the stdout of the script, e.g.:
```
Available symbol functions:
av1_read_cctx_type
av1_read_coeffs_txb
av1_read_coeffs_txb_skip
av1_read_sec_tx_type
av1_read_sig_txtype
av1_read_tx_type
decode_eob
read_ccso
read_cdef
read_cfl_alphas
read_cfl_index
read_coeff_hidden
read_coeffs_forward_2d
read_coeffs_reverse
read_coeffs_reverse_2d
read_filter_intra_mode_info
read_fsc_mode
read_intra_luma_mode
read_intra_uv_mode
read_mh_dir
read_mrl_index
read_partition
read_secondary_tx_set
read_tx_partition
```

Note that the script will also display the proportion of total bits used for the selected symbol type. In this particular example, about 10% of the bits in the frame are used to code `read_intra_luma_mode`:
```
Bits for read_intra_luma_mode: 760.7987823486328 (9.55%)
```

### Prediction mode / block size aggregation
All previous examples focused on visualizing a single frame from a single stream. This example shows how to compute aggregated stats across an arbitrary number of AVM streams.
A typical use case might be to compare how bits are spent in CTC frames under different test conditions.

This example script takes one or more glob paths and will extract all frames from all streams found:
```bash
python3 ${AOM_ROOT}/tools/py_stats/examples/aggregate_prediction_modes.py \
    --stream_glob "/path/to/some/interesting/streams/*.ivf" \
    --stream_glob "/path/to/some/other/streams/*.ivf" \
    --extract_proto_bin ${AOM_BUILD_DIR}/extract_proto \
    --output_csv /tmp/aggregate_dump.csv
```

The script will produce three different plots:
1. The overall distribution of block sizes, comparing intra and inter frames:
![](img/block_size_distribution.png "Sample output from aggregate_prediction_modes.py showing block size distribution")

2. The overall distribution of prediction modes, comparing intra and inter frames:
![](img/prediction_mode_distribution.png "Sample output from aggregate_prediction_modes.py showing prediction mode distribution")

3. Prediction mode distribution, weighted by the number of bits used to code each mode, comparing intra and inter frames:
![](img/prediction_mode_distribution_by_bits.png "Sample output from aggregate_prediction_modes.py showing prediction mode distribution weighted by bits")

The script also takes an optional `--output_csv` argument that will dump the final Pandas dataframe to a CSV file.

### Launch an example Jupyter notebook
This demonstrates many of the same visualizations listed above, but in a single Jupyter notebook.
Before launching, ensure the `AOM_BUILD_DIR` and `LIBAOM_TEST_DATA_PATH` environment variables are set.

```bash
jupyter lab ${AOM_ROOT}/tools/py_stats/examples/frame_vis.ipynb
```

You can also optionally set `JUPYTER_WORKING_DIR` to change where temporary files get stored. By default, the notebook will use `/tmp/frame_vis_notebook`.