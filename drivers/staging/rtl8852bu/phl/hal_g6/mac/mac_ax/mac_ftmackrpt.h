#ifndef _MAC_FTMACKRPT_H_
#define _MAC_FTMACKRPT_H_

/* dword0 */
#define FTMACKRPT_RPT_SEL_SH        0
#define FTMACKRPT_RPT_SEL_MSK       0x1f
#define FTMACKRPT_POLLUTED      BIT(5)
#define FTMACKRPT_ADDR_CAM_INDEX_SH     8
#define FTMACKRPT_ADDR_CAM_INDEX_MSK        0xff

/* dword1 */

/* dword2 */
#define FTMACKRPT_DATA_BW_SH        12
#define FTMACKRPT_DATA_BW_MSK       0x3

/* dword3 */
#define FTMACKRPT_TOTAL_PKT_NUM_SH      0
#define FTMACKRPT_TOTAL_PKT_NUM_MSK     0xff

/* dword4 */
#define FTMACKRPT_MACID_VALID       BIT(7)
#define FTMACKRPT_STOP_SC_SH        8
#define FTMACKRPT_STOP_SC_MSK       0xf
#define FTMACKRPT_TRIG_OFDM     BIT(12)
#define FTMACKRPT_STOP_OFDM     BIT(13)
#define FTMACKRPT_FTM_ERROR_STATUS_SH       14
#define FTMACKRPT_FTM_ERROR_STATUS_MSK      0x3
#define FTMACKRPT_SEQUENCE_NUMBER_SH        16
#define FTMACKRPT_SEQUENCE_NUMBER_MSK       0xfff

/* dword5 */
#define FTMACKRPT_R2T_T23_SH        0
#define FTMACKRPT_R2T_T23_MSK       0xffffffff

/**
 * @struct _FTMACKRPT_
 * @brief _FTMACKRPT_
 *
 * @var _FTMACKRPT_::RPT_SEL
 * Please Place Description here.
 * @var _FTMACKRPT_::POLLUTED
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD0
 * Please Place Description here.
 * @var _FTMACKRPT_::ADDR_CAM_INDEX
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD1
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD2
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD3
 * Please Place Description here.
 * @var _FTMACKRPT_::DATA_BW
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD4
 * Please Place Description here.
 * @var _FTMACKRPT_::TOTAL_PKT_NUM
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD5
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD6
 * Please Place Description here.
 * @var _FTMACKRPT_::MACID_VALID
 * Please Place Description here.
 * @var _FTMACKRPT_::STOP_SC
 * Please Place Description here.
 * @var _FTMACKRPT_::TRIG_OFDM
 * Please Place Description here.
 * @var _FTMACKRPT_::STOP_OFDM
 * Please Place Description here.
 * @var _FTMACKRPT_::FTM_ERROR_STATUS
 * Please Place Description here.
 * @var _FTMACKRPT_::SEQUENCE_NUMBER
 * Please Place Description here.
 * @var _FTMACKRPT_::RSVD7
 * Please Place Description here.
 * @var _FTMACKRPT_::R2T_T23
 * Please Place Description here.
 */
typedef struct _FTMACKRPT_ {
	/* dword 0 */
	u32 RPT_SEL: 5;
	u32 POLLUTED: 1;
	u32 RSVD0: 2;
	u32 ADDR_CAM_INDEX: 8;
	u32 RSVD1: 16;
	/* dword 1 */
	u32 RSVD2: 32;
	/* dword 2 */
	u32 RSVD3: 12;
	u32 DATA_BW: 2;
	u32 RSVD4: 18;
	/* dword 3 */
	u32 TOTAL_PKT_NUM: 8;
	u32 RSVD5: 24;
	/* dword 4 */
	u32 RSVD6: 7;
	u32 MACID_VALID: 1;
	u32 STOP_SC: 4;
	u32 TRIG_OFDM: 1;
	u32 STOP_OFDM: 1;
	u32 FTM_ERROR_STATUS: 2;
	u32 SEQUENCE_NUMBER: 12;
	u32 RSVD7: 4;
	/* dword 5 */
	u32 R2T_T23: 32;
} FTMACKRPT, *PFTMACKRPT;
#endif
