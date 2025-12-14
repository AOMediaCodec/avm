<p align="center">
<img src="./logo.png" width=320>
</p>

# ParaKit
ParaKit is a Python toolkit for training <u>P</u>robability and <u>A</u>daptation <u>R</u>ate <u>A</u>djustment (PARA) parameters used to define context model initializations for the AV2 video coding standard, currently under development by the Alliance for Open Media (AVM).

ParaKit is named after Apple's CWG-D115 proposal to the AVM's Coding Working Group, entitled "<u>PARA</u>: Probability Adaptation Rate Adjustment for Entropy Coding", where the "<u>Kit</u>" comes from the word toolkit, often referring to a collection of software tools.

---

## 1. Requirements
ParaKit is built on top of the AV2 reference software (AVM), so the  requirements include:

- For the compilation of AVM software, it is recommended to install a recent version of `cmake` (e.g., version 3.29 and after).
- For setting up necessary Python packages, it is required to install `Homebrew` (e.g., version 4.3.3 and after).

ParaKit's training is data-driven, so it requires collecting data from AVM coded bitstreams. For this purpose, a sample implementation under the `CONFIG_PARAKIT_COLLECT_DATA` macro (disabled by default) is provided to allow developers to collect data for a selection of contexts.
[Section 5](#5-data-collection-guidelines-for-modifying-avm) below provides some instructions on how to modify the ParaKit-AVM codebase to collect data for the context(s) of interest.

After making necessary modifications to the AVM for data collection, ParaKit has the following two requirements to be able to run training:

1. a binary `avmdec`*, compiled from a version of AVM for data collection, and
2. compatible AVM bitstreams, from which the data will be collected using `avmdec`.

*Note: `avmdec` needs to be compiled on the same platform that the developer will run ParaKit.

---

## 2. Installation
<b>Step 1:</b> clone AVM and change the directory.
```
git clone https://gitlab.com/AOMediaCodec/avm.git ParaKit-AVM
cd ParaKit-AVM
```

<b>Step 2:</b> change directory to `ParaKit` and run the setup.sh script, which creates a python virtual environment `venv` and installs necessary python packages within `venv`.
```
cd ParaKit
source setup.sh
```

<b>Step 3:</b> compile the AVM decoder by running the following command which will build `avmdec` under ./binaries directory.
```
source setup_decoder.sh
```
Note that `setup_decoder.sh` script is provided for convenience. The user can always copy a valid `avmdec` binary compiled from the AVM codebase by enabling the `CONFIG_PARAKIT_COLLECT_DATA` macro.

Also, make sure that `avmdec` binary under `binaries/` is executable, if not, run the following command.
```
sudo chmod +x ./binaries/avmdec
```
<b>Important note:</b> The sample AVM implementation under the `CONFIG_PARAKIT_COLLECT_DATA` macro can collect data only for `eob_flag_cdf16` and `eob_flag_cdf32` contexts. To support other contexts, the developer needs to replace the binary compiled with the necessary changes to AVM. Please refer to [Section 5](#5-data-collection-guidelines-for-modifying-avm) for more details on modifying the AVM codebase.

<b>Installation complete:</b> After the steps above, we are ready to use ParaKit and train for `eob_flag_cdf16` and `eob_flag_cdf32` contexts. You may now run the unit test as the next step.

<b>Unit test (optional, but recommended):</b> run the following unit test script to further check if the installation is complete without any issues.
```
python run_unit_test.py
```
The unit test uses the two sample bitstreams under `unit_test/bitstreams` compatible with the AVM version, tag research-v8.0.0. Developers will need to switch to `research-v8.0.0-parakit` branch or replace the bitstreams with the compatible ones to reproduce this step.

## 3. Usage: running training via ParaKit
<b>Step 1:</b> replace `avmdec` under `binaries/` with a new decoder binary (based on to collect data for desired contexts). The `setup_decoder.sh` script can be used to compile new binaries from modified AVM. Make sure that `avmdec` is executable (see Step 3 in [Section 2](#2-installation)).

<b>Step 2:</b> copy compatible AVM bitstreams under `bitstreams/` directory. The only requirement is that each bitstream's filename should start with `Bin_`.

<b>Step 3:</b> check and modify `parameters.yaml` file to set necessary user defined configurations. See [Section 4](#4-details-of-configuring-parametersyaml) for more details.

<b>Step 4:</b> run the `run.py` python script.
```
python run.py
```
This step will run the whole training pipeline that:

1. collects the data in csv format by decoding all the bitstreams in the `bitstreams/` directory. The csv files will be generated under `results/data/` directory,
2. runs the training for each csv data under `results/data/` and generates a result report file in json format,
3. collects and combines the results in json files, and
4. generates the context initialization tables in a single `Context-Table_*.h` file under `results/`.

<b>Step 5:</b> use the generated tables from the `Context-Table_*.h` file under `results/` by copying them into the AVM codebase for testing.

<b>Rerunning instructions:</b> To be able to run the training on a new dataset or to rerun, it is recommended to delete (or move) the existing data and bistream files. Specifically, developers should delete or move `.csv` files under the `results/data/` folder and the files under `bitstreams/` for a new round of training. Note that the training software expects `bitstreams/` and `results/data/` directories to be present under `ParaKit/`. So, only the csv and bitstreams files under folders should be deleted or moved.

---

## 4. Details of configuring parameters.yaml
ParaKit requires the `parameters.yaml` file present in the main directory.
The sample `./parameters.yaml` provided in the repository is configured to train for `eob_flag_cdf16` and `eob_flag_cdf32` contexts as follows:
```
TEST_OUTPUT_TAG: "Test-Tag"
BITSTREAM_EXTENSION: "av2"
DESIRED_CTX_LIST:
 - eob_flag_cdf16
 - eob_flag_cdf32
eob_flag_cdf16: "av2_default_eob_multi16_cdfs"
eob_flag_cdf32: "av2_default_eob_multi32_cdfs"
```
where the mandatory fields are:

- `TEST_OUTPUT_TAG` is the tag used to identify a test (this tag appears in the resulting generated context table `results/Context-Table_*.h`),
- `BITSTREAM_EXTENSION` specifies the extension of bitstreams copied under `bitstreams/`,
- `DESIRED_CTX_LIST` specifies the context(s) to be trained,
- `eob_flag_cdf16: "av2_default_eob_multi16_cdfs"` and `eob_flag_cdf32: "av2_default_eob_multi32_cdfs"` define the context name to context table mapping.

<b>Important note:</b> The developer needs to make sure that the context names (e.g., `eob_flag_cdf16` or `eob_flag_cdf32`) follow the same convention in the ParaKit's AVM decoder (`avmdec`).
The csv data files obtained from `avmdec` are in `Stat_context_name_*.csv` format, where in the above example, `context_name` is replaced by `eob_flag_cdf16` or `eob_flag_cdf32`.

---

## 5. Data collection: guidelines for modifying AVM
The data collection requires some modifications to AVM decoder implementation. For this purpose, a sample implementation is provided under the `CONFIG_PARAKIT_COLLECT_DATA` macro (disabled by default) as a reference, where the basic data collection module is implemented in `avm_read_symbol_probdata` function by extending the existing `avm_read_symbol` function in AVM. The comments including `@ParaKit` text provides additional information to guide developers on how to extend data collection for different contexts.

The current implementation in AVM only supports data collection from `eob_flag_cdf16` and `eob_flag_cdf32` context groups. Developers can extend this to add support for new (or any other) contexts on by following the changes under `CONFIG_PARAKIT_COLLECT_DATA` macro and instructions in the comments by searching the text `@ParaKit` on their local AVM version.

---

## Contact
Please contact Hilmi Egilmez for any questions regarding the use of ParaKit.

E-mail: h_egilmez@apple.com

---
