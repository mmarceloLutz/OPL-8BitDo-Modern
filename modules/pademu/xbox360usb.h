
#ifndef _XBOX360USB_H_
#define _XBOX360USB_H_

#include "irx.h"

/* Identificadores USB */
#define XBOX_VID       0x045E
#define MADCATZ_VID    0x1BAD
#define JOYTECH_VID    0x162E
#define GAMESTOP_VID   0x0E6F

#define XBOX_WIRED_PID       0x028E
#define MADCATZ_WIRED_PID    0xF016
#define JOYTECH_WIRED_PID    0xBEEF
#define GAMESTOP_WIRED_PID   0x0401
#define AFTERGLOW_WIRED_PID  0x0213

/* 8BitDo Ultimate C 2.4G em modo XInput */
#define EIGHTBITDO_XINPUT_VID 0x2DC8
#define EIGHTBITDO_XINPUT_PID 0x3106

#define XBOX360_MAX_BUFFER_SIZE 64
#define XBOX360_MODEL_PS2 3

/* Estados de conexao */
enum eXbox360USBStatus {
    XBOX360USB_STATE_DISCONNECTED = 0x00,
    XBOX360USB_STATE_AUTHORIZED   = 0x01,
    XBOX360USB_STATE_CONFIGURED   = 0x02,
    XBOX360USB_STATE_CONNECTED    = 0x04,
    XBOX360USB_STATE_RUNNING      = 0x08
};

/* Dispositivo USB Xbox 360 */
typedef struct _xbox360usb_device
{
    int usb_id;
    int sema;
    int cmd_sema;
    int controlEndp;
    int interruptEndp;
    int usb_resultcode;

    u8 enabled;
    u8 status;

    u8 oldled[2];
    u8 lrum;
    u8 rrum;
    u8 update_rum;

    u8 data[18];
    u8 analog_btn;
    u8 btn_delay;

} xbox360usb_device;

/* Relatorio de entrada Xbox 360 */
typedef struct
{
    u8 ReportID;
    u8 Length;

    union
    {
        u8 ButtonStateL;
        struct
        {
            u8 Up    : 1;
            u8 Down  : 1;
            u8 Left  : 1;
            u8 Right : 1;
            u8 Start : 1;
            u8 Back  : 1;
            u8 LS    : 1;
            u8 RS    : 1;
        };
    };

    union
    {
        u8 ButtonStateH;
        struct
        {
            u8 LB     : 1;
            u8 RB     : 1;
            u8 XBOX   : 1;
            u8 Dummy1 : 1;
            u8 A      : 1;
            u8 B      : 1;
            u8 X      : 1;
            u8 Y      : 1;
        };
    };

    u8 LeftTrigger;
    u8 RightTrigger;

    union
    {
        u16 LeftStickX;
        struct
        {
            u8 LeftStickXL;
            u8 LeftStickXH;
        };
    };

    union
    {
        u16 LeftStickY;
        struct
        {
            u8 LeftStickYL;
            u8 LeftStickYH;
        };
    };

    union
    {
        u16 RightStickX;
        struct
        {
            u8 RightStickXL;
            u8 RightStickXH;
        };
    };

    union
    {
        u16 RightStickY;
        struct
        {
            u8 RightStickYL;
            u8 RightStickYH;
        };
    };

} __attribute__((packed)) xbox360report_t;

/* Interface com o PADEMU moderno */
int xbox360usb_init(u8 pads, u8 options);
int xbox360usb_get_status(int port);
int xbox360usb_get_model(int port);
void xbox360usb_reset(void);
int xbox360usb_get_data(u8 *dst, int size, int port);
void xbox360usb_set_rumble(u8 lrum, u8 rrum, int port);
void xbox360usb_set_mode(int mode, int lock, int port);

#endif
