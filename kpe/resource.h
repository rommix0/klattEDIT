/* resource.h - menu, accelerator and dialog identifiers shared by kpe.rc and the C files. */
#ifndef KPE_RESOURCE_H
#define KPE_RESOURCE_H

#define IDR_MENU          100
#define IDR_ACCEL         101

#define IDM_NEW           1001
#define IDM_OPEN          1002
#define IDM_SAVE          1003
#define IDM_SAVEAS        1004
#define IDM_EXPORTWAV     1005
#define IDM_EXIT          1006
#define IDM_IMPORTWAV     1007
#define IDM_UNDO          1101
#define IDM_REDO          1102
#define IDM_CUT           1103
#define IDM_COPY          1104
#define IDM_PASTE         1105
#define IDM_DELETE        1106
#define IDM_INSERT        1107
#define IDM_SELECTALL     1108
#define IDM_PLAY          1301
#define IDM_PLAYSEL       1302
#define IDM_GLOBALS       1401
#define IDM_OPTIMIZE      1402
#define IDM_TABLE         1501
#define IDM_NARROW        1502
#define IDM_FREQRANGE     1503
#define IDM_ZOOMIN        1504
#define IDM_ZOOMOUT       1505
#define IDM_ZOOMFIT       1506
#define IDM_PARAMREF      1601
#define IDM_ABOUT         1602

#define IDD_GLOBALS       200 /* also used by File > New */
#define IDD_OPTIMIZE      201
#define IDD_PARAMREF      202
#define IDD_ASKNUM        203
#define IDD_IMPORT        204

#define IDC_DURATION_LBL  2001
#define IDC_DURATION      2002
#define IDC_FIELDS        2003 /* placeholder frame for the generated global fields */
#define IDC_NOTE          2004
#define IDC_TRACKS        2101
#define IDC_RANGE_SEL     2102
#define IDC_RANGE_ALL     2103
#define IDC_GRID          2104
#define IDC_TOL_HZ        2105
#define IDC_TOL_F0        2106
#define IDC_TOL_DB        2107
#define IDC_PREVIEW       2108
#define IDC_SUMMARY       2109
#define IDC_TOL_RD        2110
#define IDC_TOL_PCT       2111
#define IDC_LIST          2201
#define IDC_ASK_LABEL     2301
#define IDC_ASK_VALUE     2302
#define IDC_VOICE         2401
#define IDC_TARGET_SR     2402
#define IDC_SIMPLIFY      2403

#define IDC_FIELD_BASE    3000 /* generated global fields: IDC_FIELD_BASE + global index */
#define IDC_CHECK_BASE    4000 /* panel checkboxes: IDC_CHECK_BASE + parameter index */

#endif
