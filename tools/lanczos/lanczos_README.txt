/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License and the
 * Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
 * not distributed with this source code in the LICENSE file, you can obtain it
 * at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
 * License 1.0 was not distributed with this source code in the PATENTS file, you
 * can obtain it at aomedia.org/license/patent-license/.
 */

The Lanczos resampling library and utilities are developed by Google
initially and contributed to the Alliance for open Media for experiments
related to super-resolution and Adaptive Streaming use cases.

For questions and technical support, please contact debargha@google.com

LIBRARY
#######

The main library source and header files are:
lanczos_resample.c
lanczos_resample.h


UTILITIES
#########

Two utilities are provided to resample y4m and yuv videos respectively:
lanczos_resample_y4m (built using source lanczos_resample_y4m.c)
lanczos_resample_y4m (built using source lanczos_resample_yuv.c)

Another utility simply returns the filter parameters for a given
resampling configuration:
lanczos_resample_filter (built using source lanczos_resample_filter.c)

lanczos_resample_y4m
--------------------
The usage of lanczos_resample_y4m is:
  lanczos_resample_y4m
      [<Options>]
      <y4m_input>
      <num_frames>
      <horz_resampling_config>
      <vert_resampling_config>
      <y4m_output>
      [<outwidth>x<outheight>]

  Notes:
      <Options> are optional switches prefixed by '-' as follows:
          -bit:<n>        - providing bits for filter taps
                                 [default: 14]
          -ieb:<n>        - providing intermediate extra bits of
                            prec between horz and vert filtering
                            clamped to maximum of (15 - bitdepth)
                                 [default: 2]
          -ext:<ext_type> - providing the extension type
               <ext_type> is one of:
                    'r' or 'rep' (Repeat)
                    's' or 'sym' (Symmetric)
                    'f' or 'ref' (Reflect/Mirror-whole)
                    'g' or 'gra' (Grafient preserving)
                                 [default: 'r']
          -win:<win_type> - providing the windowing function type
               <win_type> is one of:
                    'lanczos'     (Repeat)
                    'lanczos_dil' (Symmetric)
                    'gaussian'    (Gaussian)
                    'gengaussian' (Generalized Gaussian)
                    'cosine'      (Cosine)
                    'hamming      (Hamming)
                    'blackman     (Blackman)
                    'kaiser       (Kaiser)
                                  [default: 'lanczos']

      <y4m_input> is the input video in Y4M format
      <y4m_output> is the output video in Y4M format
      <num_frames> is number of frames to be processed
      <horz_resampling_config> and <vert_resampling_config>
              are of the form:
          <p>:<q>:<Lanczos_a>[:<x0>] where:
              <p>/<q> gives the resampling ratio.
              <Lanczos_a> is Lanczos parameter.
              <x0> is the optional initial offset
                                 [default: centered]
                  If used, it can be a number in (-1, 1),
                  or 'c' meaning centered.
                      which is a shortcut for x0 = (q-p)/(2p)
                  or 'd' meaning co-sited chroma with centered
                      luma for use only on sub-sampled chroma,
                      which is a shortcut for x0 = (q-p)/(4p)
                  The field can be prefixed by 'i' meaning
                      using the inverse of the number provided,
          If it is desired to provide different config parameters
          for luma and chroma, the <Lanczos_a> and <x0> fields
          could be optionally converted to a pair of
          comma-separated parameters as follows:
          <p>:<q>:<Lanczos_al>,<lanczos_ac>[:<x0l>,<x0c>]
              where <Lanczos_al> and <lanczos_ac> are
                        luma and chroma lanczos parameters
                    <x0l> and <x0c> are
                        luma and chroma initial offsets
      <outwidth>x<outheight> is output video dimensions
                             only needed in case of upsampling
      Resampling config of 1:1:1:0 horizontally or vertically
          is regarded as a no-op in that direction

Example usages:

1a. Downsample 10 frames from Boat_1920x1080_60fps_10bit_420.y4m by
    ratio 3/2 horizontally and 4/3 vertically with Lanczos parameter
    6 for both and centered sampling.

    lanczos_resample_y4m Boat_1920x1080_60fps_10bit_420.y4m 10 \
                         2:3:6 3:4:6 /tmp/down.y4m

1b. Reverse the process in 1a. That is, upsample 10 frames from
    /tmp/boat.y4m by ratio 3/2 horizontally and 4/3 vertically with
    Lanczos parameter 6 for both and centered sampling. The output
    dimension desired is specified to be 1920x1080.

    lanczos_resample_y4m /tmp/down.y4m 10 3:2:6 4:3:6 /tmp/downup.y4m 1920x1080

2a. Similar to 1a, except using left-aligned sampling

    lanczos_resample_y4m Boat_1920x1080_60fps_10bit_420.y4m 10 \
                         2:3:6:0 3:4:6:0 /tmp/down.y4m

2b. Reversing the process in 2a.

    lanczos_resample_y4m /tmp/down.y4m 10 3:2:6:0 4:3:6:0 /tmp/downup.y4m \
                         1920x1080

3a. Similar to 1a, except using a specified initial offset of 0.125 horizontally
    and center aligned sampling vertically

    lanczos_resample_y4m Boat_1920x1080_60fps_10bit_420.y4m 10 \
                         2:3:6:0.125 3:4:6 /tmp/down.y4m

3b. Reversing the process in 3a.

    lanczos_resample_y4m /tmp/down.y4m 10 3:2:6:i0.125 4:3:6 /tmp/downup.y4m \
                         1920x1080

4a. Similar to 3a, except using symmetric border extension instead of default
    repeat.

    lanczos_resample_y4m -ext:sym Boat_1920x1080_60fps_10bit_420.y4m 10 \
                         2:3:6:0.125 3:4:6:c /tmp/down.y4m

4b. Reversing the process in 4a.

    lanczos_resample_y4m -ext:sym /tmp/down.y4m 10 3:2:6:i0.125 4:3:6:c \
                         /tmp/downup.y4m 1920x1080

5a. Downsample 10 frames from Boat_1920x1080_60fps_10bit_420.y4m by
    ratio 3/2 horizontally and vertically with Lanczos parameter
    6 for both, centered sampling for luma, co-sited chroma horizontally
    and centered chroma vertically. Note this is the most common use
    case encountered in practice.

    lanczos_resample_y4m Boat_1920x1080_60fps_10bit_420.y4m 10 \
                         2:3:6:c,d 2:3:6 /tmp/down.y4m

5b. Reversing the process in 5a.

    lanczos_resample_y4m /tmp/down.y4m 10 3:2:6:ic,id 3:2:6 \
                         /tmp/downup.y4m 1920x1080

6a. Similar to 5a except using symmetric border extension, 12-bit
    filters, 3 bit intermediate extra precision, and hamming windowing.

    lanczos_resample_y4m -ext:sym -bit:12 -ieb:3 -win:hamming \
                         Boat_1920x1080_60fps_10bit_420.y4m 10 \
                         2:3:6:c,d 2:3:6 /tmp/down.y4m

6b. Reversing the process in 6a.

    lanczos_resample_y4m -ext:sym -bit:12 -ieb:3 -win:hamming \
                         /tmp/down.y4m 10 3:2:6:ic,id 3:2:6 \
                         /tmp/downup.y4m 1920x1080
lanczos_resample_yuv
--------------------
The usage of lanczos_resample_yuv is similar but with two extra
arguments to specify the input format:
    lanczos_resample_yuv
      [<Options>]
      <yuv_input>
      <width>x<height>
      <pix_format>
      <num_frames>
      <horz_resampling_config>
      <vert_resampling_config>
      <yuv_output>
      [<outwidth>x<outheight>]
  Notes:
      <yuv_input> is the input video in raw YUV format
      <yuv_output> is the output video in raw YUV format
      <width>x<height> is input video dimensions.
      <pix_format> is one of { yuv420p, yuv420p10, yuv420p12,
                               yuv422p, yuv422p10, yuv422p12,
                               yuv444p, yuv444p10, yuv444p12 }
      All other parameters are the same as the ones used for
      lanczos_resample_y4m described above.


lanczos_resample_filter
-----------------------
Utility to show the filter parameters used for both luma and chroma.
The usage of lanczos_resample_filter is:
  lanczos_resample_filter
      [<Options>]
      <resampling_config>
  Notes:
      <resampling_config> is in the same format as one specified
          in lanczos_resample_y4m above for <horz_resampling_config>
	  or <vert_resampling_config>.

Example usages:

7a. lanczos_resample_filter -bit:14 2:3:6
        (shows 14-bit a = 6 filter for 2/3 resampling, x0 = centered)
7b. lanczos_resample_filter -bit:12 1:2:5:0.125
        (shows 12-bit a = 5 filter for 1/2 resampling, x0 = 0.125)
7c. lanczos_resample_filter -bit:12 1:2:5:c,d 12
        (shows 12-bit a = 5 filter for 1/2 resampling,
	 x0 = centered for luma, co-sited for chroma)
7d. lanczos_resample_filter -bit:12 3:2:6:i0.125 12
        (shows 12-bit a = 5 filter for 3/2 resampling, x0 = inverse of 0.125)


SCRIPTS
#######

In addition to these utilities, two convenience scripts are provided:
lanczos_downup.sh
lanczos_downcompup.sh
They can be invoked from the build directory directly after a build that
produces lanczos_resample_y4m, avmenc and avmdec applications.

lanczos_downup.sh
-----------------
The script lanczos_downup.sh resamples a video with specified parameters
and then reverses the process using two lanczos_resample_y4m commands.
The usage for the script is:

  Usage:
    lanczos_downup.sh [<Options>]
                      <input_y4m>
		      <num_frames>
                      <horz_resampling_config>
                      <vert_resampling_config>
                      <downup_y4m>
                      [<down_y4m>]

  Notes:
      <Options> are optional switches similar to what is used by
          lanczos_resample_y4m utility
      <y4m_input> is input y4m video
      <num_frames> is number of frames to process
      <horz_resampling_config> and <vert_resampling_config> are in format:
              <p>:<q>:<Lanczos_a_str>[:<x0>]
          similar to what is used by lanczos_resample_y4m utility, with the
          enhancement that for <Lanczos_a_str> optionally
          two '^'-separated strings for 'a' could be provided instead
          of one for down and up-sampling operations respectively if different.
          Note each string separated by '^' could have two values for luma
          and chroma separated by ','.
          So <Lanczos_a_str> could be of the form:
              <a_luma_down>,<a_chroma_down>^<a_luma_up>,<a_chroma_up>
            if down and up operations use different parameters, or
              <a_luma_down>,<a_chroma_down>
            if down and up operations use the same parameters.
      <downup_y4m> is the output y4m video.
      <down_y4m> provides the intermediate resampled file as an
          optional parameter. If skipped the intermediate resampled
          file is deleted.

Example usages:
8. Similar to combined use cases 1a and 1b above including both
down and inverse up steps.

8a. From build directory run:
    /path/to/script/lanczos_downup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:6 3:4:6 /tmp/downup.y4m
            [Here the intermediate resampled files is not stored]

8b. From build directory run:
    /path/to/script/lanczos_downup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:6 3:4:6 /tmp/downup.y4m \
        /tmp/down.y4m
            [Here the intermediate resampled file is stored in /tmp/down.y4m]

8c. From build directory run:
    /path/to/script/lanczos_downup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:8^4 3:4:8^4 /tmp/downup.y4m
            [Here Lanczos parameters 8 and 4 are used for down and upscaling
             respectively]

8d. From build directory run:
    /path/to/script/lanczos_downup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:6,4^6,4:c,d 3:4:6,4^6,4 /tmp/downup.y4m
            [Here Lanczos parameters 6 and 4 are used for luma and chroma
	     respectively for both down and upscaling. Further centered
	     luma and co-sited chroma are used horizontally, while centered
	     luma and chroma sampling are used vertically.]

lanczos_downcompup.sh
---------------------
The script lanczos_downcompup.sh resamples a video with specified parameters
using lanczos_resample_y4m, then comprsses and decompresses using avmenc and
avmdec respectively, and finally reverse resamples the decompressed video to
the source resolution using another lanczos_resample_y4m command.
The usage for the script is:

  Usage:
    lanczos_downcompup.sh [<Options>]
                          <input_y4m>
			  <num_frames>
                          <horz_resampling_config>
                          <vert_resampling_config>
                          <cq_level>[:<cpu_used>]
                          <downcompup_y4m>
                          [[<down_y4m>]:[<downcomp_bit>]:[<downcomp_y4m]]

    Notes:
        <Options> are optional switches similar to what is used by
            lanczos_resample_y4m utility
        <y4m_input> is input y4m video
        <num_frames> is number of frames to process
        <horz_resampling_config> and <vert_resampling_config> are in format:
                <p>:<q>:<Lanczos_a_str>[:<x0>]
            similar to what is used by lanczos_resample_y4m utility, with the
            enhancement that for <Lanczos_a_str> optionally
            two '^'-separated strings for 'a' could be provided instead
            of one for down and up-sampling operations respectively if different.
            Note each string separated by '^' could have two values for luma
            and chroma separated by ','.
            So <Lanczos_a_str> could be of the form:
                <a_luma_down>,<a_chroma_down>^<a_luma_up>,<a_chroma_up>
              if down and up operations use different parameters, or
                <a_luma_down>,<a_chroma_down>
              if down and up operations use the same parameters.
        <cq_level>[:<cpu_used>] provides the cq_level parameter of
            compression along with an optional cpu_used parameter.
        <downcompup_y4m> is the output y4m video.
        The last param [[<down_y4m>]:[<downcomp_bit>]:[<downcomp_y4m]]
            provides names of intermediate files where:
                <down_y4m> is the resampled source
                <downcomp_bit> is the compressed resampled bitstream
                <downcomp_y4m> is the reconstructed bitstream.
            This parameter string is entirely optional.
            Besides if provided, each of <down_y4m>, <downcomp_bit> and
            <downcomp_y4m> are optional by themselves where each can be
            either provided or empty. If empty the corresponding
            intermediate file is deleted.

Example usages:
9. Similar to use cases 8 above with a compression step in between.

9a. From build directory run:
    /path/to/script/lanczos_downcompup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:6 3:4:6 40:5 /tmp/downup.y4m
            [Here no intermediate files are stored.
	     Compression is at cq_level 40 and cpu_used=5]

9b. From build directory run:
    /path/to/script/lanczos_downcompup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:6 3:4:6 40:5 /tmp/downup.y4m \
        /tmp/down.y4m::/tmp/downrec.y4m
            [Here the resampled source and its compressed reconstruction are
             stoted in /tmp/down.y4m and /tmp/downrec.y4m respectively.
	     Compression is at cq_level 40 and cpu_used=5].

9c. From build directory run:
    /path/to/script/lanczos_downcompup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:6 3:4:6 40:5 /tmp/downup.y4m \
        /tmp/down.y4m:/tmp/down.bit:/tmp/downrec.y4m
            [Here the resampled source, its compressed bitstream, and the
             conpressed reconstruction are stoted in /tmp/down.y4m,
             /tmp/downcomp.bit and /tmp/downrec.y4m respectively.
	     Compression is at cq_level 40 and cpu_used=5].

9d. From build directory run:
    /path/to/script/lanczos_downcompup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:8^4 3:4:8^4 40:5 /tmp/downup.y4m
            [Here Lanczos parameters 8 and 4 are used for down and upscaling
             respectively.
	     Compression is at cq_level 40 and cpu_used=5].

9e. From build directory run:
    /path/to/script/lanczos_downcompup.sh Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:6,4^6,4:c,d 3:4:6,4^6,4 40:5 /tmp/downup.y4m
            [Here Lanczos parameters 6 and 4 are used for luma and chroma
	     respectively for both down and upscaling. Further centered
	     luma sampling and co-sited chroma sampling are respectively used
	     in the horizontal direction, while centered sampling is used for
	     both luma and chroma vertically.
	     Compression is at cq_level 40 and cpu_used=5].

9f. From build directory run:
    /path/to/script/lanczos_downcompup.sh -win:kaiser \
        Boat_1920x1080_60fps_10bit_420.y4m \
        10 2:3:5:c,d 3:4:5 40:5 /tmp/downup.y4m
            [Here Lanczos parameter 5 is used for luma and chroma both
	     horizontally and vertically, Further, centered luma
	     sampling and co-sited chroma sampling are used respectively
	     in the horizontal direction, while centered sampling is used
	     for both luma and chroma vertically.
	     Compression is at cq_level 40 and cpu_used=5].
