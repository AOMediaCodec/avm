This testing framework is initially developed by Intel for convex hull study.
It is contributed to Alliance for Open Media for convex hull study and future AV2
Common Test Condition (CTC) testing.

For questions and technical support, please contact ryanlei@meta.com or
maggie.sun@intel.com

This readme file provides a simple introduction of the framework and steps to
use it for different studies.

Prerequisites
1. Inside ./bin folder, make sure you have executables for all external tools,
   such as vmaf(version v2.1.1+), HDRConvert, encoder, decoder, etc. You can get
   the pre-build binaries or source code for the following tools that are needed:
   vmaf tools: https://github.com/Netflix/vmaf/releases/tag/v2.1.1
   HDRConvert: https://gitlab.com/standards/HDRTools 0.22 branch
   libaom research branch encoder/decoder: https://gitlab.com/AOMediaCodec/avm
   SVT AV1 encoder: https://github.com/OpenVisualCloud/SVT-AV1
   Please always follow the manual of these tools to update the command
   parameters used in the script.
   when EnableTimingInfo is enabled, ptime utility is used on Windows to capture
   the run time information for encode and decode. On Linux, time utility from
   Linux system is used to capture run time information.

2. AV2 CTC has adopted VMAF tool as the reference implementation of all quality
   metrics required. Using the versioned '--aom_ctc v1.0' at the command line
   will generate all quality metrics for AV2 CTC.

3. HDRConvert is used to perform downscale and upscale operation. It requires
   a template config file (HDRConvScalerY4MFile.cfg). It is also located under
   the ./bin folder. Test framework will generate individual config file based
   on this template.

4. VBA macro binary file (vbaProject-AV2.bin) for bdrate calculation is also
   stored under ./bin folder, which is needed for BDRATE calculation inside the
   excel file.

5. Test framework is implemented in python 3. It requires few extra python
   packages, such as xlrd, xlsxwriter, argparse, etc.

Things you need to update to configure the test.
1. Update the test clips table in AV2CTCVideo.py file following the existing
   example to provide the list and classes of video sequences that you want to
   test. Currently, only .y4m files are supported. Test script will parse the
   y4m file header to get the basic information of the video clip, such as
   resolution, frame rate, bit depth and color format, etc.

2. Update the test configuration list in ./src/Config.py to specify the test
   configurations. Currently, supported test configurations include Random Access
   (RA), Low Delay (LD), All Intra(AI) and Adaptive Streaming (AS).

3. Update FrameNum in ./src/Config.py to specify number of frames you want to
   process for different test configurations.

4. Update DnScaleRatio list in ./src/Config.py to provide the list of downscaling
   ratios used for adaptive streaming (convex hull) test configuration.

5. Update DnScalingAlgos and UpScalingAlgos list in ./src/Config.py to specify
   downscaling/upscaling filter types that you want to test. The name of the
   filter types is that supported by HDRConvert. The size of these two lists
   must be the same. Filter types for downscaling and upscaling can be different.
   Right now, only the lanczos filter is supported.

6. Update QPs list in ./src/Config.py to specify the list of QPs you want to
   test for different test configurations. QPs must be in the valid QP range for
   different coding standards. For example, [0, 63] for AV1, and [0, 255] for AV2.

7. Update QualityList in ./src/Config.py to specify quality metrics you want to
   calculate. Currently, only VMAF is supported for quality metrics calculation.

8. Update SMOKE_TEST flag, It is used for sanity check purpose, in which only
   few frames are processed to reduce the test time.

Sample command lines for running the adaptive streaming (convex hull) test:
"python ConvexHullTest.py [options]"
below is the full command line options in help message:
  -h, --help                   show this help message and exit
  -f, --function               function to run: clean, scaling, sumscaling,
                               encode, convexhull, summary
  -k, --KeepUpscaleOutput      [0|1] in function clean, if keep upscaled yuv
                               files. It is false by default
  -s, --SaveMemory             [0|1] save memory mode will delete most files in
                               intermediate steps and keeps only necessary ones
                               for RD calculation. It is false by default
  -CmdOnly, --LogCmdOnly       LogCmdOnly mode will only capture the command
                               sequences in the command log file instead of
                               actually run it. It is false by default
  -l, --LoggingLevel           logging level: 0:No Logging, 1: Critical, 2: Error,
                               3:Warning, 4: Info, 5: Debug
  -c, --CodecName              CodecName: av1, av2
  -m, --EncodeMethod           EncodeMethod: aom, svt
  -p, --EncodePreset           EncodePreset: 0,1,2... for aom and svt

when LogCmdOnly is set to True, the test framework will only capture all process
command sequences into a log file (under ./test/testLogs folder with time stamp)
without actually running it on your system. This feature is to support the use
case in which actual processing tasks need to distributed on to a server cluster
and executed in parallel.

Sample command for typical operations:
1.  python ConvexHullTest.py -f clean
    This command will clean up all intermediate files under ./test folder

2.  python ConvexHullTest.py -f scaling
    This command will run the standalone downscaling and upscaling tests.
    Downscaled YUV files are stored under ./test/downscaledYUVs folder.
    Upscaled YUV files are stored under ./test/upscaledYUVs folder.
    Quality metrics log files are stored under ./test/qualityLogs folder.
    Other processing logs and command logs are stored under ./test/testLogs folder.
    For using HDRConvert, individual config files are generated and stored under
    ./test/configFiles folder. All intermediate file names indicate the input,
    output resolution and filter types that are used.

3.  python ConvexHullTest.py -f sumscaling
    This command will summarize the quality metrics for the scaling test into
    excel files under ./analysis/scalingresult folder. There are excel files for
    each individual test sequence and also excel file that summarizes quality
    result for all test sequences based on classes.

4.  python ConvexHullTest.py -f encode -c av2 -m aom -p 1
    This command will run the encoding test. It actually contains downscale
    (optional), encode, decode, upscale, quality metrics steps. Right now, only
    av2 encoding/decoding with libaom is supported. Downscale step will be skipped
    if downscaled yuv files already generated in ./test/downscaledYUVs. Encoded
    bitstreams are stored under ./test/bitstreams folder. Decoded YUV files are
    stored under ./test/decodedYUVs folder. Decoded and then upscaled YUV files
    are stored under ./test/decUpscaledYUVs folder. Quality logs are stored under
    ./test/qualityLogs folder.

5.  python ConvexHullTest.py -f convexhull -c av2 -m aom -p 1
    This command will summarize the per sequence quality result based on
    different scaling ratios and scaling filter types.
    Please make sure the same encoding method/codec name/preset are used as the
    previous steps. Output excel files are stored under ./analysis/rdresult
    folder. For each sequence and downscaling/upscaling filter type, an excel
    sheet is generated that contains the bitrate and quality metric for different
    downscaled resolutions. Rate distortion curve for all quality metrics will
    be drawn in a scatter plot. Convex hull will be calculated and draw on top
    of the rate distortion curve.
    In this step, a summary excel file is also generated, which include the
    convex hull rate distortion points for all test content and downscaling and
    upscaling algorithm pair. This excel file is needed when you want to
    calculate bdrate between two encoding runs to evaluate the quality impact on
    overall convex hull from a coding tool.

6.  python ConvexHullTest.py -f summary -c av2 -m aom -p 1
    This command will summarize the quality metrics across all test sequences
    into an excel file stored under the ./analysis/summary folder. BDRATE between
    different resolutions will be calculated. Average result based on content
    classes will be calculated.

In order to calculate BDRATE between convex hull from two encoding test runs
with different codec or different encoding preset, ./src/ConvexHullBDRate.py
script is needed. Command line options for this script is as following:

usage: ConvexHullBDRate.py [options]

optional arguments:
  -h, --help       show this help message and exit
  -i1 , --input1   convex hull summary excel file for base mode
  -i2 , --input2   convex hull summary excel file for target mode
  -o , --output    output excel file with BDRATE for base and target modes

To use this script, the convex hull summary excel files for base and target
modes are needed. Script will parse the rate distortion data from the convex hull
for each individual test content, then it will use the bdrate macro to calculate
the bdrate between two convex hulls.

a sample command is:
python ConvexHullBDRate.py -i1 ConvexHullRD_ffmpeg_hevc_medium.xlsx
-i2 ConvexHullRD_ffmpeg_hevc_veryslow.xlsx -o ConvexHullBDRate.xlsm

Sample command lines for running the regular AV2 CTC test for All Intra(AI),
Low Delay(LD) and Random Access (RA) configurations:
"python AV2CTCTest.py [options]"

below is the full command line options in help message:

  -h, --help                   show this help message and exit
  -f, --function               function to run: clean, encode, summary
  -s, --SaveMemory             [0|1] save memory mode will delete most files in
                               intermediate steps and keeps only necessary ones
                               for RD calculation. It is false by default
  -CmdOnly, --LogCmdOnly       LogCmdOnly mode will only capture the command
                               sequences in the command log file instead of
                               actually run it. It is false by default
  -l, --LoggingLevel           logging level: 0:No Logging, 1: Critical, 2: Error,
                               3:Warning, 4: Info, 5: Debug
  -p, --EncodePreset           EncodePreset: 0,1,2... for aom

Sample command for typical operations:
1.  python AV2CTCTest.py -f clean
    This command will clean up all intermediate files under ./test folder

2.  python AV2CTCTest.py -f encode -p 1
    This command will run the encoding test. It actually contains encode, decode,
    and quality metrics steps. Right now, only av2 encoding/decoding with libaom
    is supported. Encoded bitstreams are stored under ./test/bitstreams folder.
    Decoded YUV files are stored under ./test/decodedYUVs folder. Quality logs
    are stored under ./test/qualityLogs folder. Timing information logs are stored
    under ./test/perfLogs floder.

3.  python AV2CTCTest.py -f summary -p 1
    This command will summarize the quality metrics across all test sequences
    into an csv file stored under the ./analysis/summary folder.

4.  after getting the csv file for two tests (reference and target), content of
    the csv files can be directly copied into the excel template
    (AV2Template_Vx.xlsm under the bin folder), which can calculate the BDRATE
    of the two tests.
