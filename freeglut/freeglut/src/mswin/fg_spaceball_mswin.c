/*
 * fg_spaceball_mswin.c
 *
 * Spaceball support for Windows
 *
 * Copyright (c) 2012 Stephen J. Baker. All Rights Reserved.
 * Written by Evan Felix <karcaw at gmail.com>
 * Creation date: Sat Feb 4, 2012
 *
 * Copyright (c) 2014 Jinrong Xie. All Rights Reserved.
 * Written by Jinrong Xie <stonexjr at gmail.com>
 * Modification date: Wed Dec 24, 2014
 *
 * Copyright (c) 2020 Shane Saxon. All Rights Reserved.
 * Written by Shane Saxon <saxon at openantz.com>
 * Modification date: Fri Dec 4, 2020
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PAWEL W. OLSZTA BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Modified by Jinrong Xie <stonexjr at gmail.com> 12/24/2014
 * for Space Navigator support on Windows.
 * This code is enhanced by at least supporting 3Dconnexion's
 * six degree of freedom navigator.
 *
 * Modified by Shane Saxon 2020-12-07, to support (new) VID=0x256f devices
 * and enhanced original VID=0x046d support. Tested on Space Navigator,
 * Space Mouse Pro (original), SpaceMouse Wireless and SpaceMouse Enterprise.
 */

#if(_WIN32_WINNT >= 0x0501)

#include <GL/freeglut.h>
#include <stdlib.h>
#include "../fg_internal.h"

/* VID=0x046d devices have separate events for motion (translate) and rotate.
 * VID=0x256f devices combine translate and rotate into a single motion event.
 * All devices have the BITWISE button event (Report ID 0x03), the other button
 * events are device specific and a bit intertwined (caution).
 */
#define VENDOR_LOGI 0x046d		/* original (3DX) Logitech VID */
#define VENDOR_3DX  0x256f		/* new 3Dconnexion VID */

/* prefix specifies which VID's apply (L = Logitech, V = 3DX, A = All) */
#define A3DX_EVENT_MOTION     0x01	/* translate (and rotate for VID=0x256f) */
#define L3DX_EVENT_ROTATE     0x02	/* rotate (only for VID=046d devices) */
#define A3DX_EVENT_BUTTON     0x03	/* bitwise buttons (all devices) */
#define A3DX_EVENT_BUTTON_BDF 0x16	/* bitwise dual function (SM Pro's) */
#define V3DX_EVENT_BUTTON_X   0x1c	/* integer key codes (SM Enterprise) */
#define V3DX_EVENT_BUTTON_XDF 0x1d	/* integer dual function (SM Enterprise) */

#define A3DX_DEFAULT_BUTTON_BITS 16	/* default valid bitwise button bits */
#define V3DX_MAX_KEYS 0xff			/* max value of integer key codes */

uint16_t fg_sball_buttons = 0;		/* number of buttons */
uint16_t fg_sball_bitwise_bits = 0;	/* bitwise event 0x03 data size in bits*/

struct SFG_SpaceballConfig {
    uint16_t VID;
    uint16_t PID;
    uint8_t bits;		/* device specific valid button data bits */
    uint8_t buttons;	/* number of buttons (physical + virtual) */
};
typedef struct SFG_SpaceballConfig SFG_SpaceballConfig;

extern int fg_sball_initialized;

/* Multi-axis Controller is 0x01, 0x08 and RIDEV_DEVNOTIFY is arrival/removal */
RAWINPUTDEVICE __fgSpaceball = { 0x01, 0x08, RIDEV_DEVNOTIFY, 0x00 };

/* Spaceball device list with: VID, PID, bits and buttons */
static SFG_SpaceballConfig spaceballList[] = {
    {0x046d, 0xc603, 16, 10},   /* spacemouse plus XT - different HID schema? */
    {0x046d, 0xc605, 16, 4},    /* cadman */
    {0x046d, 0xc606, 16, 8},    /* spacemouse classic */
    {0x046d, 0xc621, 16, 12},   /* spaceball 5000 */
    {0x046d, 0xc623, 16, 8},    /* space traveller */
    {0x046d, 0xc625, 24, 21},   /* space pilot */
    {0x046d, 0xc626, 16, 2},    /* space navigator */
    {0x046d, 0xc627, 16, 15},   /* space explorer */
    {0x046d, 0xc628, 16, 2},    /* space navigator for notebooks */
    {0x046d, 0xc629, 32, 31},   /* space pilot pro */
    {0x046d, 0xc62b, 32, 19},   /* space mouse pro, 15+4 */
    {0x046d, 0xc640, 16, 4},    /* nulooq            - different HID schema? */
    {0x256f, 0xc62e, 16, 2},    /* spacemouse wireless (USB cable) 15+4 */
    {0x256f, 0xc62f, 16, 2},    /* spacemouse wireless  receiver,  15+4 */
    {0x256f, 0xc631, 32, 19},   /* spacemouse pro wireless */
    {0x256f, 0xc632, 32, 19},   /* spacemouse pro wireless receiver */
    {0x256f, 0xc633, 32, 51},   /* spacemouse enterprise 31+20 */
    {0x256f, 0xc635, 16, 2},    /* spacemouse compact */
    {0x256f, 0xc636, 16, 2},    /* spacemouse module */
    {0x256f, 0xC641, 16, 2},    /* spacemouse module (other) */

    {0, 0, 0, 0}
};

/* sets the fg_sball_bitwise_bits and fg_sball_buttons based on VID and PID */
void fgSpaceballUpdateDeviceList(void);

void fgPlatformInitializeSpaceball(void)
{
    if( fg_sball_initialized == 1 )
        return;

    if (!fgStructure.CurrentWindow)
    {
        fg_sball_initialized = 0;
        return;
    }

    __fgSpaceball.hwndTarget = fgStructure.CurrentWindow->Window.Handle;

    if( RegisterRawInputDevices(&__fgSpaceball, 1, sizeof(__fgSpaceball)) == 0 )
    {
        __fgSpaceball.hwndTarget = NULL;
        fg_sball_initialized = 0;
        return;
    }
        
    fgSpaceballUpdateDeviceList();
    
    fg_sball_initialized = 1;
}

/* NOT CALLED */
void fgPlatformSpaceballClose(void)
{
    return;
}

int fgPlatformHasSpaceball(void)
{
    if( !fg_sball_initialized )
        fgPlatformInitializeSpaceball();

    if( !fg_sball_buttons )
        return FALSE;

    return TRUE;
}

int fgPlatformSpaceballNumButtons(void)
{
    if( !fg_sball_initialized )
        fgPlatformInitializeSpaceball();

    return fg_sball_buttons;
}

/* NOT CALLED */
void fgPlatformSpaceballSetWindow(SFG_Window *window)
{
    return;
}

/* NOT IN USE
int fgIsSpaceballWinEvent(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    return 0;
}
*/

void fgSpaceballDeviceChangeWinEvent( HWND hwnd, WPARAM wParam, LPARAM lParam )
{
    /* printf("DEVICE CHANGE: %d\n", wParam ); // GIDC_ARRIVAL ); */

    fgSpaceballUpdateDeviceList();
}

void fgSpaceballUpdateDeviceList(void)
{
    unsigned int i, numDevices;
    PRAWINPUTDEVICELIST pRawInputDeviceList;

    fg_sball_bitwise_bits = fg_sball_buttons = 0;

#ifdef _DEBUG
    printf("HID devices:\n");
#endif

    if( GetRawInputDeviceList( NULL, &numDevices, 
        sizeof(RAWINPUTDEVICELIST ) ) != 0 )
    {
        fgError( "spaceball numDevices" );
        return;
    }

    if( (pRawInputDeviceList = 
        malloc(sizeof(RAWINPUTDEVICELIST) * numDevices ) ) == NULL )
    {
        fgError( "out of memory - numDevices" );
        return;
    }

    if( GetRawInputDeviceList( pRawInputDeviceList, &numDevices, 
        sizeof(RAWINPUTDEVICELIST) ) == -1 )
    {
        fgError( "spaceball list" );
        free( pRawInputDeviceList );
        return;
    }

    for( i=0; i < numDevices; i++ )
    {
        RID_DEVICE_INFO devInfo;
        unsigned int data_size = 1023;

#ifdef _DEBUG
        char device_name[1024];
        printf("dev index: %d type: %d\n",i, pRawInputDeviceList[i].dwType);
        if (GetRawInputDeviceInfo(
                pRawInputDeviceList[i].hDevice,
                RIDI_DEVICENAME,
                &device_name, &data_size) == -1)
        {
            fgError( "HID name" );
            continue;
        }
        printf("    name: %s\n", device_name);
#endif

        devInfo.cbSize = sizeof(RID_DEVICE_INFO);
        data_size = sizeof(RID_DEVICE_INFO);
        if( GetRawInputDeviceInfo( pRawInputDeviceList[i].hDevice,
                RIDI_DEVICEINFO, &devInfo, &data_size ) == -1 ) 
        {
            fgError( "spaceball info");
            continue;
        }

        if( devInfo.hid.dwVendorId == VENDOR_LOGI
            || devInfo.hid.dwVendorId == VENDOR_3DX )
        {
            int j;
            
            /* found a multi-axis device with a known VID */
            /* set defaults for potential unknown device support */
            fg_sball_buttons = fg_sball_bitwise_bits = A3DX_DEFAULT_BUTTON_BITS;

            /* set the number of valid bits and buttons based on PID */
            for( j=0; spaceballList[j].VID > 0; j++ )
                if( spaceballList[j].PID == devInfo.hid.dwProductId )
                {
                    fg_sball_bitwise_bits = spaceballList[j].bits;
                    fg_sball_buttons = spaceballList[j].buttons;
                }
#ifdef _DEBUG
            printf( "VID: 0x%04x  PID: 0x%04x  bits: %d  buttons: %d\n",
                devInfo.hid.dwVendorId, devInfo.hid.dwProductId, 
                fg_sball_bitwise_bits, fg_sball_buttons );
#endif
        }
    }
    free( pRawInputDeviceList );
}

void fgSpaceballHandleWinEvent(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    SFG_Window* window;
    HRAWINPUT hRawInput = (HRAWINPUT)lParam;
    UINT size;
    BYTE *rawInputBuffer;
    PRAWINPUT pRawInput;
    RID_DEVICE_INFO sRidDeviceInfo;

    if (!fg_sball_initialized)
    {
        fgPlatformInitializeSpaceball();
        if (!fg_sball_initialized)
        {
            return;
        }
    }

    if( GetRawInputData( hRawInput, RID_INPUT, NULL, &size,
            sizeof(RAWINPUTHEADER) ) == -1 )
        return;

    rawInputBuffer = malloc( size );
    if( !rawInputBuffer )
    {
        fgError( "out of memory - rawInputBuffer");
        return;
    }

    pRawInput = (PRAWINPUT)rawInputBuffer;
    if( GetRawInputData( hRawInput, RID_INPUT, pRawInput, &size,
            sizeof(RAWINPUTHEADER) ) == -1 )
        goto cleanUp;

    if( pRawInput->header.dwType != RIM_TYPEHID )
        goto cleanUp;

    sRidDeviceInfo.cbSize = sizeof(RID_DEVICE_INFO);
    if( GetRawInputDeviceInfo( pRawInput->header.hDevice, RIDI_DEVICEINFO,
            &sRidDeviceInfo, &sRidDeviceInfo.cbSize ) == -1 )
        goto cleanUp;
    
    /* exit if unknown VID */
    if( sRidDeviceInfo.hid.dwVendorId != VENDOR_LOGI
            && sRidDeviceInfo.hid.dwVendorId != VENDOR_3DX )
        goto cleanUp;

    if( (window = fgWindowByHandle(hwnd)) == NULL )
        goto cleanUp;

    /*
        if( pRawInput->data.hid.dwSizeHid == 7 )
            printf ("PID: 0x%04x sz: %2dB event: %2d raw: %04x %04x %04x\n",
                sRidDeviceInfo.hid.dwProductId,
                pRawInput->data.hid.dwSizeHid,
                pRawInput->data.hid.bRawData[0],
                *(uint16_t*)(&pRawInput->data.hid.bRawData[1]),
                *(uint16_t*)(&pRawInput->data.hid.bRawData[3]),
                *(uint16_t*)(&pRawInput->data.hid.bRawData[5]) );
        else
            printf ("PID: %04x sz: %2d event: %2d raw: %08x  %08x  %08x\n",
                sRidDeviceInfo.hid.dwProductId,
                pRawInput->data.hid.dwSizeHid,
                pRawInput->data.hid.bRawData[0],
                *(uint32_t*)(&pRawInput->data.hid.bRawData[1]),
                *(uint32_t*)(&pRawInput->data.hid.bRawData[5]),
                *(uint32_t*)(&pRawInput->data.hid.bRawData[9]) );
    */

    /* event processing for all (supported) device types */ 
    if( pRawInput->data.hid.bRawData[0] == A3DX_EVENT_MOTION )
    {	
        int16_t* pnData = (int16_t*)(&pRawInput->data.hid.bRawData[1]);

        if( FETCH_WCB(*window, SpaceMotion) )
        {
            /* translate data is always part of the first 7 bytes */
            int16_t x = pnData[0]; 
            int16_t y = -pnData[2]; /* -axis to retain Right Handed */
            int16_t z = pnData[1];  /* swap Z-Y to re-orient device */
            INVOKE_WCB( *window, SpaceMotion, (x, y, z) );
        }

        if( FETCH_WCB(*window, SpaceRotation) )
        {
            /* VID 0x256f devices include rotate data with translate */
            /* we detect this by the 13 byte packet size (vs 7 byte) */
            if( pRawInput->data.hid.dwSizeHid == 13 )
            {
                int16_t rx = pnData[3];
                int16_t ry = -pnData[5];    /* swap Z-Y to re-orient*/
                int16_t rz = pnData[4];
                INVOKE_WCB( *window, SpaceRotation, (rx, ry, rz) );
            }
        }
    }
    else if( pRawInput->data.hid.bRawData[0] == L3DX_EVENT_ROTATE )
    {
        if( FETCH_WCB(*window, SpaceRotation) )
        {
            /* VID 0x046d devices have independent 7 byte rotate event */
            int16_t* pnData = (int16_t*)(&pRawInput->data.hid.bRawData[1]);
            int16_t rx = pnData[0];
            int16_t ry = -pnData[2];        /* swap Z-Y to re-orient*/
            int16_t rz = pnData[1];
            INVOKE_WCB( *window, SpaceRotation, (rx, ry, rz) );
        }
    }
    else if( pRawInput->data.hid.bRawData[0] == A3DX_EVENT_BUTTON )
    {
        /* BITWISE buttons are used by all devices, but vary with 16, 24
         * or 32 valid bits, packet size = 3, 4 or 5 bytes. The valid data
         * bits are stored in fg_sball_bitwise_bits which is set by
         * fgSpaceballUpdateDeviceList().
         *
         * Note that hid.dwSizeHid does NOT report correct data bit size!
         */
        static uint32_t prevBits = 0;
        uint32_t nowBits = *(uint32_t*)(&pRawInput->data.hid.bRawData[1]);

        if( FETCH_WCB(*window, SpaceButton) )
        {
            /* loop through each bit, if changed invoke callback */
            /* button number based on the bit position = i */
            int i;
            for( i = 0; i < fg_sball_bitwise_bits; i++ )
            {
                /* safe bitwise method to get the i'th bit state */
                bool stateBefore = (prevBits >> i) & 1;
                bool stateNow = (nowBits >> i) & 1;

                if( stateBefore && !stateNow ) /* button number = i+1 */
                    INVOKE_WCB( *window, SpaceButton, (i+1, GLUT_UP) );
                                
                if( !stateBefore && stateNow )
                    INVOKE_WCB( *window, SpaceButton, (i+1, GLUT_DOWN) );
            }
        }
        prevBits = nowBits;	/* store the button states */
    }
    else if( pRawInput->data.hid.bRawData[0] == A3DX_EVENT_BUTTON_BDF )
    {
        /* Bitwise Dual Function keys (eg. SM Pro's keys 1-4) are simply
         * passed through with the same button number as event 0x03.
         */
        uint32_t prevBDF = 0;
        uint32_t nowBDF = *(uint32_t*)(&pRawInput->data.hid.bRawData[1]);

        if( FETCH_WCB(*window, SpaceButton) )
        {
            int i;
            for( i=0; i < 32; i++ )
            {
                int stateBefore = (prevBDF >> i) & 1;
                int stateNow = (nowBDF >> i) & 1;

                if( stateBefore && !stateNow )
                    INVOKE_WCB( *window, SpaceButton, (i+1, GLUT_UP) );
                                
                if( !stateBefore && stateNow )
                    INVOKE_WCB( *window, SpaceButton, (i+1, GLUT_DOWN) );
            }
        }
        prevBDF = nowBDF;
    }
    else if( pRawInput->data.hid.bRawData[0] == V3DX_EVENT_BUTTON_X )
    {
        /* This event is generated by the SM Enterprise for any key press.
         * It can contain up to 6 (integer) key codes that are represented as
         * 2 byte words, and includes both bitwise and non-bitwise buttons.
         *
         * Non-bitwise keys: ENTER, DEL, 11, 12, V1, V2, V3, TAB, and SPACE.
         */
        if( FETCH_WCB(*window, SpaceButton) )
        {
            static uint16_t prevX[6] = {0,0,0,0,0,0}; /* key codes are 1 byte */
            uint16_t nowX[6];
            int i, j;

            for( i=0; i < 6; i++ )
            {             /* event report ID [1 byte] + [2 byte] word spacing */
                nowX[i] = *(uint16_t*)(&pRawInput->data.hid.bRawData[1+(i*2)]);

                /* ignore keys that are redundant to bitwise event 0x03 */
                if( nowX[i] <= 32 )
                    nowX[i] = 0;

                for( j=0; j < 6; j++ ) 
                {
                    if( nowX[i] == prevX[j] )       /* no change to key state */
                        break;

                    if( j == 5 )  /* key down if not present in prevX[] array */
                        INVOKE_WCB( *window, SpaceButton, (nowX[i], GLUT_DOWN));
                }
            }

            for( i=0; i < 6; i++ )
            {
                for( j=0; j < 6; j++ )
                {
                    if( prevX[i] == nowX[j] )
                        break;

                    if( j == 5 )
                        INVOKE_WCB( *window, SpaceButton, (prevX[i], GLUT_UP) );
                }
                prevX[i] = nowX[i];
            }
        }
    }
    else if( pRawInput->data.hid.bRawData[0] == V3DX_EVENT_BUTTON_XDF )
    {
        /* Dual Function (DF) keys are specific to the SM Enterprise
         * and ONLY when the driver configures it to generate this 
         * special (key held) event. The keys that can be DF keys
         * are labeled: 1-12, Roll CW/CCW, T/Bk, F/B, R/L, ISO1/2, 
         * V1/SAVE_V1, V2/SAVE_V2, V3/SAVE_V3.
         *
         * Pressing two or more DF keys generates alternating events,
         * one every 700ms (HW default) with each having a different
         * subset of the DF keys, with up to 6 keys in each event, akin
         * to event 0x1c. This is complex to parse!
         *
         * Hence, we only support one DF key at a time, which will work
         * in combos with the non-DF keys. Pressing 2 DF keys will result
         * in alternating UP/DOWN events for both keys.
         *
         * If you don't use the vendor driver or stop the service and
         * reboot the HW, you ONLY get (instant) events 0x03 and 0x1c.
         */
        static uint16_t prevXD = 0;
        uint16_t nowXD =  *(uint16_t*)(&pRawInput->data.hid.bRawData[1]);

        if( FETCH_WCB(*window, SpaceButton) )
        {
            if( nowXD == 0 || (prevXD && nowXD != prevXD) )
                INVOKE_WCB( *window, SpaceButton, (prevXD, GLUT_UP) );

            if( nowXD )
                INVOKE_WCB( *window, SpaceButton, (nowXD, GLUT_DOWN) );
        }
        prevXD = nowXD;
    }

cleanUp :
    free(rawInputBuffer);
}

/* ---------------------------------- */
/* ---------------------------------- */
/* ---------------------------------- */

/* HID button ID key code list applies from Space Pilot Pro (all buttons 1-31)
 * to the Spacemouse Enterprise which has gaps and goes past the 32bit range.
 * Prior to Space Pilot Pro the button ID mappings do not match the modern
 * schema's labels, but they are contiguous.
*/
/*
enum logi_3dx_buttons {
   L3DX_NULL        = 0,
   L3DX_MENU        = 1,
   L3DX_FIT         = 2,
   L3DX_TOP         = 3,
   L3DX_LEFT        = 4,
   L3DX_RIGHT       = 5,
   L3DX_FRONT       = 6,
   L3DX_BOTTOM      = 7,
   L3DX_BACK        = 8,
   L3DX_ROLL_CW     = 9,
   L3DX_ROLL_CCW    = 10,
   L3DX_ISO1        = 11,
   L3DX_ISO2        = 12,
   L3DX_1           = 13,
   L3DX_2           = 14,
   L3DX_3           = 15,
   L3DX_4           = 16,
   L3DX_5           = 17,
   L3DX_6           = 18,
   L3DX_7           = 19,
   L3DX_8           = 20,
   L3DX_9           = 21,
   L3DX_10          = 22,
   L3DX_ESC         = 23,
   L3DX_ALT         = 24,
   L3DX_SHIFT       = 25,
   L3DX_CTRL        = 26,
   L3DX_ROTATE      = 27,
   L3DX_PANZOOM     = 28,
   L3DX_DOMINANT    = 29,
   L3DX_PLUS        = 30,
   L3DX_MINUS       = 31,    // last button belonging to bitwise event ID 0x03

   L3DX_SPIN_CW     = 32,    // start button integer >= 32 (SM Enterprise)
   L3DX_SPIN_CCW    = 33,
   L3DX_TILT_CW     = 34,
   L3DX_TILT_CCW    = 35,
   L3DX_ENTER       = 36,
   L3DX_DELETE      = 37,

   L3DX_11          = 77,
   L3DX_12          = 78,
   L3DX_VIEW_1      = 103,
   L3DX_VIEW_2      = 104,
   L3DX_VIEW_3      = 105,
   L3DX_SAVE_VIEW_1 = 139,
   L3DX_SAVE_VIEW_2 = 140,
   L3DX_SAVE_VIEW_3 = 141,
   L3DX_TAB         = 175,
   L3DX_SPACE       = 176,
};
*/

/* The Dual Function key labels match the Space Pilot Pro (1-31), but differ
 * in that they are not directly generated by the device. This keymap matches
 * the labels of SM Enterprise DF key codes (event 0x1d) to the vendors modern
 * schema (and is also in agreement with the Space Pilot Pro button ID's).
 * We ignore the DF keys 1-12, since they are associated with the VIRTUALLCD.
*/
/*
static uint8_t dual_func_key_map[][2] = {
    {L3DX_RIGHT,    L3DX_LEFT},
    {L3DX_TOP,      L3DX_BOTTOM},
    {L3DX_FRONT,    L3DX_BACK},
    {L3DX_ROLL_CW,  L3DX_ROLL_CCW},
    {L3DX_ISO1,     L3DX_ISO2},

    {L3DX_VIEW_1,   L3DX_SAVE_VIEW_1},
    {L3DX_VIEW_2,   L3DX_SAVE_VIEW_2},
    {L3DX_VIEW_3,   L3DX_SAVE_VIEW_3},

    {0, 0}
};
*/

#endif
