#!/usr/bin/env python
## Copyright (c) 2021, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
__author__ = "maggie.sun@intel.com, ryanlei@meta.com"

import os
import platform
import AV2CTCVideo
from AV2CTCVideo import CTC_VERSION

#TEST_CONFIGURATIONS = ["RA","LD", "AS"]
TEST_CONFIGURATIONS = ["LD", "RA", "AI", "STILL"]

######################################
# configuration settings
######################################
RootPath = ".."
BinPath = os.path.join(RootPath, 'bin')
WorkPath = os.path.join(RootPath, 'test')
SMOKE_TEST = False  # override some parameters to do a quick smoke test
FrameNum = {
    "LD" : 130,
    "RA" : 130,
    "AI" : 30,
    "AS" : 130,
    "STILL" : 1,
}
EnableTimingInfo = True
UsePerfUtil = False
EnableMD5 = True
EnableOpenGOP = False
EnableTemporalFilter = False
Platform = platform.system()
PSNR_Y_WEIGHT = 14.0
PSNR_U_WEIGHT = 1.0
PSNR_V_WEIGHT = 1.0
APSNR_Y_WEIGHT = 4.0
APSNR_U_WEIGHT = 1.0
APSNR_V_WEIGHT = 1.0

if CTC_VERSION in ['5.0', '6.0']:
    CTC_RegularXLSTemplate = os.path.join(BinPath, 'AOM_CWG_Regular_CTCv4_v7.4.5.xlsm')
    CTC_ASXLSTemplate = os.path.join(BinPath, 'AOM_CWG_AS_CTC_v10.0.xlsm')
elif CTC_VERSION == '4.0':
    CTC_RegularXLSTemplate = os.path.join(BinPath, 'AOM_CWG_Regular_CTCv4_v7.3.2.xlsm')
    CTC_ASXLSTemplate = os.path.join(BinPath, 'AOM_CWG_AS_CTC_v9.7.1.xlsm')
elif CTC_VERSION == '3.0':
    CTC_RegularXLSTemplate = os.path.join(BinPath, 'AOM_CWG_Regular_CTC_v7.2.xlsm')
    CTC_ASXLSTemplate = os.path.join(BinPath, 'AOM_CWG_AS_CTC_v9.7.xlsm')
elif CTC_VERSION == '2.0':
    CTC_RegularXLSTemplate = os.path.join(BinPath, 'AOM_CWG_Regular_CTC_v7.1.xlsm')
    CTC_ASXLSTemplate = os.path.join(BinPath, 'AOM_CWG_AS_CTC_v9.7.xlsm')
else:
    CTC_RegularXLSTemplate = os.path.join(BinPath, 'AOM_CWG_Regular_CTC_v6.1.xlsm')
    CTC_ASXLSTemplate = os.path.join(BinPath, 'AOM_CWG_AS_CTC_v9.6.xlsm')

############ test contents #######################################
ContentPath = "D://YUVs//AV2-CTC"
############## Scaling settings ############################################
# down scaling ratio
DnScaleRatio = [1.0, 1.5, 2.0, 3.0, 4.0, 6.0]   # downscale ratio
#down and up scaling algorithm, the 2 lists should be of same size
DnScalingAlgos = ['lanczos'] #['bicubic', 'bilinear', 'gauss', 'lanczos', 'sinc']
UpScalingAlgos = ['lanczos'] #['bicubic', 'bilinear', 'gauss', 'lanczos', 'sinc']

ScaleMethods = ['hdrtool', 'ffmpeg', 'aom']

if SMOKE_TEST:
    DnScalingAlgos = ['bicubic', 'lanczos', 'sinc']
    UpScalingAlgos = ['bicubic', 'lanczos', 'sinc']
HDRToolsConfigFileTemplate = os.path.join(BinPath, 'HDRConvScalerY4MFile.cfg')
HDRConvert = os.path.join(BinPath, 'HDRConvert')
AOMScaler = os.path.join(BinPath, 'lanczos_resample_y4m')

##################### Encode Config ########################################
EncodeMethods = ["aom", "svt", "hm"]
CodecNames = ["av1", "av2", "hevc"]
SUFFIX = {"av1": ".obu", "av2": ".obu", "hevc":".265"}
FFMPEG = os.path.join(BinPath, 'ffmpeg')
AOMENC = os.path.join(BinPath, 'aomenc')
SVTAV1 = os.path.join(BinPath, 'SvtAv1EncApp')
AOMDEC = os.path.join(BinPath, 'aomdec')
AV1ENC = os.path.join(BinPath, 'av1enc')
AV1DEC = os.path.join(BinPath, 'av1dec')
HMENC = os.path.join(BinPath, "TAppEncoderStatic")
VMAF = os.path.join(BinPath, 'vmaf')
HEVCCfgFile = os.path.join(BinPath, "s2-hm-01.cfg")

if CTC_VERSION in ["2.0", "3.0", "4.0", "5.0", "6.0"]:
    QPs = {
        "LD": [110, 135, 160, 185, 210, 235],
        "RA": [110, 135, 160, 185, 210, 235],
        "AI": [85, 110, 135, 160, 185, 210],
        "AS": [110, 135, 160, 185, 210, 235],
        "STILL": [60, 85, 110, 135, 160, 185],
    }
else:
    QPs = {
        "LD": [23, 31, 39, 47, 55, 63],
        "RA": [23, 31, 39, 47, 55, 63],
        "AI": [15, 23, 31, 39, 47, 55],
        "AS": [23, 31, 39, 47, 55, 63],
        "STILL": [15, 23, 31, 39, 47, 55],
    }

HEVC_QPs = {
    "LD": [22, 27, 32, 37, 42, 47],
    "RA": [22, 27, 32, 37, 42, 47],
    "AI": [22, 27, 32, 37, 42, 47],
    "AS": [22, 27, 32, 37, 42, 47],
    "STILL": [22, 27, 32, 37, 42, 47],
}
MIN_GOP_LENGTH = 16
SUB_GOP_SIZE = 16
GOP_SIZE = 65
AS_DOWNSCALE_ON_THE_FLY = False

######################## quality evalution config #############################
QualityList = ['PSNR_Y','PSNR_U','PSNR_V','SSIM_Y(dB)','MS-SSIM_Y(dB)','VMAF_Y',
               'VMAF_Y-NEG','PSNR-HVS','CIEDE2000','APSNR_Y','APSNR_U','APSNR_V','CAMBI']

CalcBDRateInExcel = True
EnablePreInterpolation = True
UsePCHIPInterpolation = False
#InterpolatePieces - 1 is the number of interpolated points generated between two qp points.
InterpolatePieces = 8

######################## config for exporting data to excel  #################
#https://xlsxwriter.readthedocs.io/working_with_colors.html#colors
# line color used, number of colors >= len(DnScaledRes)
LineColors = ['blue', 'red', 'green', 'orange', 'pink', 'yellow']
ConvexHullColor = 'white'
Int_ConvexHullColor = 'cyan'

# find out QP/Resolution with specified qty metrics
TargetQtyMetrics = {'VMAF_Y': [60, 70, 80, 90],
                    'PSNR_Y': [30, 35, 38, 40, 41]}

# format for exported excel of convexhull test
# if to modify below 3 variables, need to modify function
# SaveConvexHullResultsToExcel accordingly
CvxH_startCol = 1; CvxH_startRow = 2; CvxH_colInterval = 2
CvxH_WtCols = [(CvxH_colInterval + 1 + len(QualityList)) * i + CvxH_startCol
               for i in range(len(DnScaleRatio))]
CvxH_WtRows = [CvxH_startRow + i for i in range(len(QPs['AS']))]
CvxH_WtLastCol = CvxH_WtCols[-1] + len(QualityList)
CvxH_WtLastRow = CvxH_WtRows[-1]

# format for writing convexhull curve data
CvxHDataStartRow = CvxH_WtRows[-1] + 2; CvxHDataStartCol = 0
CvxHDataNum = 7  # qty, bitrate, qp, resolution, int_qty, int_bitrate, 1 empty row as internal
CvxHDataRows = [CvxHDataStartRow + 1 + CvxHDataNum * i for i in range(len(QualityList))]

######################## post analysis #########################################
PostAnalysis_Path = os.path.join(RootPath, 'analysis')
Path_RDResults = os.path.join(PostAnalysis_Path, 'rdresult')
SummaryOutPath = os.path.join(PostAnalysis_Path, 'summary')
Path_ScalingResults = os.path.join(PostAnalysis_Path, 'scalingresult')
# vba file needed when to calculate bdrate
#VbaBinFile = os.path.join(BinPath, 'vbaProject_JVET-L0242.bin')
VbaBinFile = os.path.join(BinPath, 'vbaProject-AV2.bin')

# format for exported excel of scaling quality test
# if to modify below 3 variables, need to modify function SaveScalingResultsToExcel
# accordingly
ScalQty_startCol = 6; ScalQty_startRow = 2; ScalQty_colInterval = 1
ScalSumQty_startCol = 7
ScalQty_WtCols = [(ScalQty_colInterval +
                   len(QualityList)) * i + ScalQty_startCol
                  for i in range(len(DnScalingAlgos))]
ScalSumQty_WtCols = [(ScalQty_colInterval +
                      len(QualityList)) * i + ScalQty_startCol + 1
                     for i in range(len(DnScalingAlgos))]
######################## logging #########################################
LoggerName = "AV2CTC"
LogLevels = ['NONE', 'CRITICAL', 'ERROR', 'WARNING', 'INFO', 'DEBUG']
