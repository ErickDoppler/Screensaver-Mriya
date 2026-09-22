/* Resource IDs shared by the .rc script and platform_win32.c */
#ifndef MR_RESOURCE_H
#define MR_RESOURCE_H

#define IDS_DESCRIPTION   1      /* Windows shows string 1 as the screensaver name */
#define IDI_APP           1      /* first icon: Explorer and the screensaver list */
#define IDD_CONFIG        101

#define IDC_STATIC        -1

/* Input */
#define IDC_MOUSEROT      1001
#define IDC_EXITMOUSE     1002
#define IDC_SENS_LBL      1003
#define IDC_SENS          1004
#define IDC_SENS_VAL      1005
#define IDC_EXITKEY       1006

/* Flight */
#define IDC_MANUAL        1010
#define IDC_APRES_LBL     1011
#define IDC_APRES         1012
#define IDC_APRES_VAL     1013
#define IDC_LEG           1014
#define IDC_LEG_VAL       1015
#define IDC_TURN          1016
#define IDC_TURN_VAL      1017
#define IDC_ALTCHG        1018
#define IDC_ALTCHG_VAL    1019
#define IDC_TIMESCALE     1020
#define IDC_TIMESCALE_VAL 1021
#define IDC_MASS          1022
#define IDC_MASS_VAL      1023

/* Scenery */
#define IDC_WXMIN         1030
#define IDC_WXMIN_VAL     1031
#define IDC_CAMMIN        1032
#define IDC_CAMMIN_VAL    1033
#define IDC_HUD_OFF       1034
#define IDC_HUD_CAP       1035
#define IDC_HUD_FULL      1036
#define IDC_UNITS_M       1037
#define IDC_UNITS_IMP     1038
#define IDC_NAVLIGHTS     1039
#define IDC_LENS          1040

/* Picture */
#define IDC_FOV           1050
#define IDC_FOV_VAL       1051
#define IDC_BLOOM         1052
#define IDC_BLOOM_VAL     1053
#define IDC_QUALITY       1054
#define IDC_QUALITY_VAL   1055
#define IDC_FPS           1056
#define IDC_FPS_VAL       1057

/* The hidden section, revealed by Ctrl+Alt+S inside the dialog:
 * one checkbox per weather scenario and per camera. */
#define IDC_SCENE_BOX     1100
#define IDC_SCENE_FIRST   1101
#define IDC_SCENE_LAST    (IDC_SCENE_FIRST + 26)
#define IDC_CAM_BOX       1140
#define IDC_CAM_FIRST     1141
#define IDC_CAM_LAST      (IDC_CAM_FIRST + 12)
#define IDC_SCENE_ALL     1160
#define IDC_SCENE_NONE    1161

/* Real time */
#define IDC_RT            1060
#define IDC_RT_CITY       1061
#define IDC_RT_FIND       1062
#define IDC_RT_PLACE      1063

/* Joystick */
#define IDC_JOY_NAME      1064
#define IDC_JOY_INV_ROLL  1065
#define IDC_JOY_INV_PITCH 1066
#define IDC_JOY_INV_THR   1067
#define IDC_JOY_INV_RUD   1068

#define IDC_DEFAULTS      1170
#define IDC_PREVIEW       1171

#endif
