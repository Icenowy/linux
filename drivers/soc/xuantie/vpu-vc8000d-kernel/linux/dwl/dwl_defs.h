/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2021 VERISILICON
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2021 VERISILICON
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/

#ifndef SOFTWARE_LINUX_DWL_DWL_DEFS_H_
#define SOFTWARE_LINUX_DWL_DWL_DEFS_H_

#define DWL_MPEG2_E      31 /* 1 bit  */
#define DWL_VC1_E        29 /* 2 bits */
#define DWL_JPEG_E       28 /* 1 bit  */
#define DWL_HJPEG_E      17 /* 1 bit  */
#define DWL_AV1_E        16 /* 1 bit  */
#define DWL_MPEG4_E      26 /* 2 bits */
#define DWL_H264_E       24 /* 2 bits */
#define DWL_H264HIGH10_E 20 /* 1 bits */
#define DWL_AVS2_E       18 /* 2 bits */
#define DWL_VP6_E        23 /* 1 bit  */
#define DWL_RV_E         26 /* 2 bits */
#define DWL_VP8_E        23 /* 1 bit  */
#define DWL_VP7_E        24 /* 1 bit  */
#define DWL_WEBP_E       19 /* 1 bit  */
#define DWL_AVS_E        22 /* 1 bit  */
#define DWL_G1_PP_E      16 /* 1 bit  */
#define DWL_G2_PP_E      31 /* 1 bit  */
#define DWL_PP_E         31 /* 1 bit  */
#define DWL_HEVC_E       26 /* 3 bits */
#define DWL_VP9_E        29 /* 3 bits */

#define DWL_H264_PIPELINE_E 31 /* 1 bit */
#define DWL_JPEG_PIPELINE_E 30 /* 1 bit */

#define DWL_G2_HEVC_E    0  /* 1 bits */
#define DWL_G2_VP9_E     1  /* 1 bits */
#define DWL_G2_RFC_E        2  /* 1 bits */
#define DWL_RFC_E        17  /* 2 bits */
#define DWL_G2_DS_E         3  /* 1 bits */
#define DWL_DS_E         28  /* 3 bits */
#define DWL_HEVC_VER     8  /* 4 bits */
#define DWL_VP9_PROFILE  12 /* 3 bits */
#define DWL_RING_E       16 /* 1 bits */

#define HANTRODEC_IRQ_STAT_DEC       1
#define HANTRODEC_IRQ_STAT_DEC_OFF   (HANTRODEC_IRQ_STAT_DEC * 4)
#define BIGOCEAN_IRQ_STAT_DEC       2
#define BIGOCEAN_IRQ_STAT_DEC_OFF   (HANTRODEC_IRQ_STAT_DEC * 4)

#define HANTRODECPP_SYNTH_CFG        60
#define HANTRODECPP_SYNTH_CFG_OFF    (HANTRODECPP_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG          50
#define HANTRODEC_SYNTH_CFG_OFF      (HANTRODEC_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG_2        54
#define HANTRODEC_SYNTH_CFG_2_OFF    (HANTRODEC_SYNTH_CFG_2 * 4)
#define HANTRODEC_SYNTH_CFG_3        56
#define HANTRODEC_SYNTH_CFG_3_OFF    (HANTRODEC_SYNTH_CFG_3 * 4)
#define HANTRODEC_CFG_STAT           23
#define HANTRODEC_CFG_STAT_OFF       (HANTRODEC_CFG_STAT * 4)
#define HANTRODECPP_CFG_STAT         260
#define HANTRODECPP_CFG_STAT_OFF     (HANTRODECPP_CFG_STAT * 4)


#define HANTRODEC_DEC_E              0x01
#define HANTRODEC_PP_E               0x01
#define HANTRODEC_DEC_ABORT          0x20
#define HANTRODEC_DEC_IRQ_DISABLE    0x10
#define HANTRODEC_DEC_IRQ            0x100

/* Legacy from G1 */
#define HANTRO_IRQ_STAT_DEC          1
#define HANTRO_IRQ_STAT_DEC_OFF      (HANTRO_IRQ_STAT_DEC * 4)
#define HANTRO_IRQ_STAT_PP           60
#define HANTRO_IRQ_STAT_PP_OFF       (HANTRO_IRQ_STAT_PP * 4)

#define HANTROPP_SYNTH_CFG           100
#define HANTROPP_SYNTH_CFG_OFF       (HANTROPP_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG          50
#define HANTRODEC_SYNTH_CFG_OFF      (HANTRODEC_SYNTH_CFG * 4)
#define HANTRODEC_SYNTH_CFG_2        54
#define HANTRODEC_SYNTH_CFG_2_OFF    (HANTRODEC_SYNTH_CFG_2 * 4)

/* VC8000D HW build id */
#define HANTRODEC_HW_BUILD_ID        309
#define HANTRODEC_HW_BUILD_ID_OFF    (HANTRODEC_HW_BUILD_ID * 4)

#define HANTRO_DEC_E                 0x01
#define HANTRO_PP_E                  0x01
#define HANTRO_DEC_ABORT             0x20
#define HANTRO_DEC_IRQ_DISABLE       0x10
#define HANTRO_PP_IRQ_DISABLE        0x10
#define HANTRO_DEC_IRQ               0x100
#define HANTRO_PP_IRQ                0x100

#endif /* SOFTWARE_LINUX_DWL_DWL_DEFS_H_ */
