#ifndef _MAC_FTMRPT_H_
#define _MAC_FTMRPT_H_

/* dword0 */
#define FTMRPT_RPT_SEL_SH       0
#define FTMRPT_RPT_SEL_MSK      0x1f
#define FTMRPT_POLLUTED     BIT(5)
#define FTMRPT_TX_STATE_SH      6
#define FTMRPT_TX_STATE_MSK     0x3
#define FTMRPT_SW_DEFINE_SH     8
#define FTMRPT_SW_DEFINE_MSK        0xf
#define FTMRPT_MACID_SH     16
#define FTMRPT_MACID_MSK        0x7f
#define FTMRPT_QSEL_SH      24
#define FTMRPT_QSEL_MSK     0x3f

/* dword1 */
#define FTMRPT_QUEUE_TIME_SH        0
#define FTMRPT_QUEUE_TIME_MSK       0xffff
#define FTMRPT_ACCTXTIME_SH     16
#define FTMRPT_ACCTXTIME_MSK        0xff
#define FTMRPT_BMC      BIT(29)
#define FTMRPT_BITMAP_SHORT_SH      30
#define FTMRPT_BITMAP_SHORT_MSK     0x3

/* dword2 */
#define FTMRPT_FINAL_RATE_SH        0
#define FTMRPT_FINAL_RATE_MSK       0x1ff
#define FTMRPT_FINAL_GI_LTF_SH      9
#define FTMRPT_FINAL_GI_LTF_MSK     0x7
#define FTMRPT_DATA_BW_SH       12
#define FTMRPT_DATA_BW_MSK      0x3
#define FTMRPT_COLLISION_HEAD       BIT(30)
#define FTMRPT_COLLISION_TAIL       BIT(31)

/* dword3 */
#define FTMRPT_TOTAL_PKT_NUM_SH     0
#define FTMRPT_TOTAL_PKT_NUM_MSK        0xff
#define FTMRPT_DATA_TX_CNT_SH       8
#define FTMRPT_DATA_TX_CNT_MSK      0x3f
#define FTMRPT_PKT_OK_NUM_SH        16
#define FTMRPT_PKT_OK_NUM_MSK       0xff

/* dword4 */
#define FTMRPT_STOP_SC_SH       8
#define FTMRPT_STOP_SC_MSK      0xf
#define FTMRPT_TRIG_OFDM        BIT(12)
#define FTMRPT_STOP_OFDM        BIT(13)
#define FTMRPT_FTM_ERROR_STATUS_SH      14
#define FTMRPT_FTM_ERROR_STATUS_MSK     0x3
#define FTMRPT_SEQUENCE_NUMBER_SH       16
#define FTMRPT_SEQUENCE_NUMBER_MSK      0xfff

/* dword5 */
#define FTMRPT_T2R_T14_SH       0
#define FTMRPT_T2R_T14_MSK      0xffffffff

/**
 * @struct _FTMRPT_
 * @brief _FTMRPT_
 *
 * @var _FTMRPT_::RPT_SEL
 * Please Place Description here.
 * @var _FTMRPT_::POLLUTED
 * Please Place Description here.
 * @var _FTMRPT_::TX_STATE
 * Please Place Description here.
 * @var _FTMRPT_::SW_DEFINE
 * Please Place Description here.
 * @var _FTMRPT_::RSVD0
 * Please Place Description here.
 * @var _FTMRPT_::MACID
 * Please Place Description here.
 * @var _FTMRPT_::RSVD1
 * Please Place Description here.
 * @var _FTMRPT_::QSEL
 * Please Place Description here.
 * @var _FTMRPT_::RSVD2
 * Please Place Description here.
 * @var _FTMRPT_::QUEUE_TIME
 * Please Place Description here.
 * @var _FTMRPT_::ACCTXTIME
 * Please Place Description here.
 * @var _FTMRPT_::RSVD3
 * Please Place Description here.
 * @var _FTMRPT_::BMC
 * Please Place Description here.
 * @var _FTMRPT_::BITMAP_SHORT
 * Please Place Description here.
 * @var _FTMRPT_::FINAL_RATE
 * Please Place Description here.
 * @var _FTMRPT_::FINAL_GI_LTF
 * Please Place Description here.
 * @var _FTMRPT_::DATA_BW
 * Please Place Description here.
 * @var _FTMRPT_::RSVD4
 * Please Place Description here.
 * @var _FTMRPT_::COLLISION_HEAD
 * Please Place Description here.
 * @var _FTMRPT_::COLLISION_TAIL
 * Please Place Description here.
 * @var _FTMRPT_::TOTAL_PKT_NUM
 * Please Place Description here.
 * @var _FTMRPT_::DATA_TX_CNT
 * Please Place Description here.
 * @var _FTMRPT_::RSVD5
 * Please Place Description here.
 * @var _FTMRPT_::PKT_OK_NUM
 * Please Place Description here.
 * @var _FTMRPT_::RSVD6
 * Please Place Description here.
 * @var _FTMRPT_::RSVD7
 * Please Place Description here.
 * @var _FTMRPT_::STOP_SC
 * Please Place Description here.
 * @var _FTMRPT_::TRIG_OFDM
 * Please Place Description here.
 * @var _FTMRPT_::STOP_OFDM
 * Please Place Description here.
 * @var _FTMRPT_::FTM_ERROR_STATUS
 * Please Place Description here.
 * @var _FTMRPT_::SEQUENCE_NUMBER
 * Please Place Description here.
 * @var _FTMRPT_::RSVD8
 * Please Place Description here.
 * @var _FTMRPT_::T2R_T14
 * Please Place Description here.
 */
typedef struct _FTMRPT_ {
	/* dword 0 */
	u32 RPT_SEL: 5;
	u32 POLLUTED: 1;
	u32 RSVD: 2;
	u32 SW_DEFINE: 4;
	u32 RSVD0: 4;
	u32 MACID: 7;
	u32 RSVD1: 1;
	u32 QSEL: 6;
	u32 RSVD2: 2;
	/* dword 1 */
	u32 QUEUE_TIME: 16;
	u32 ACCTXTIME: 8;
	u32 RSVD3: 5;
	u32 BMC: 1;
	u32 BITMAP_SHORT: 2;
	/* dword 2 */
	u32 FINAL_RATE: 9;
	u32 FINAL_GI_LTF: 3;
	u32 DATA_BW: 2;
	u32 RSVD4: 16;
	u32 COLLISION_HEAD: 1;
	u32 COLLISION_TAIL: 1;
	/* dword 3 */
	u32 TOTAL_PKT_NUM: 8;
	u32 DATA_TX_CNT: 6;
	u32 RSVD5: 2;
	u32 PKT_OK_NUM: 8;
	u32 RSVD6: 8;
	/* dword 4 */
	u32 RSVD7: 8;
	u32 STOP_SC: 4;
	u32 TRIG_OFDM: 1;
	u32 STOP_OFDM: 1;
	u32 FTM_ERROR_STATUS: 2;
	u32 SEQUENCE_NUMBER: 12;
	u32 RSVD8: 4;
	/* dword 5 */
	u32 T2R_T14: 32;
} FTMRPT, *PFTMRPT;
#endif

