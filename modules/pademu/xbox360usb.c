
#include "types.h"
#include "loadcore.h"
#include "stdio.h"
#include "sysclib.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thsemap.h"

#include "xbox360usb.h"

/*
 * Driver Xbox 360 USB para OPL moderno.
 *
 * Baseado na implementacao do RoloDeOvo.
 *
 * Etapa 1:
 * - Identificacao de dispositivos USB
 * - Estrutura de registro do driver
 * - Preparacao para integracao ao PADEMU
 */

#define XBOX360_MAX_PADS 4

// #define DPRINTF(x...) printf(x)
#define DPRINTF(x...)

static int xbox360usb_probe(int devId);
static int xbox360usb_connect(int devId);
static int xbox360usb_disconnect(int devId);

/*
 * Nome e callbacks exclusivos para evitar
 * conflitos com o driver ds34usb.
 */
static UsbDriver xbox360_usb_driver = {
    NULL,
    NULL,
    "xbox360usb",
    xbox360usb_probe,
    xbox360usb_connect,
    xbox360usb_disconnect
};

static xbox360usb_device xbox360dev[XBOX360_MAX_PADS];

/*
 * Identifica dispositivos compativeis.
 */
static int xbox360usb_probe(int devId)
{
    UsbDeviceDescriptor *device;

    device = (UsbDeviceDescriptor *)
        UsbGetDeviceStaticDescriptor(
            devId,
            NULL,
            USB_DT_DEVICE
        );

    if (device == NULL)
        return 0;

    /* 8BitDo Ultimate C 2.4G - XInput */
    if (device->idVendor == EIGHTBITDO_XINPUT_VID &&
        device->idProduct == EIGHTBITDO_XINPUT_PID)
        return 1;

    /* Xbox 360 oficial */
    if (device->idVendor == XBOX_VID &&
        device->idProduct == XBOX_WIRED_PID)
        return 1;

    /* Controles de terceiros */
    if (device->idVendor == MADCATZ_VID &&
        device->idProduct == MADCATZ_WIRED_PID)
        return 1;

    if (device->idVendor == JOYTECH_VID &&
        device->idProduct == JOYTECH_WIRED_PID)
        return 1;

    if (device->idVendor == GAMESTOP_VID &&
        (device->idProduct == GAMESTOP_WIRED_PID ||
         device->idProduct == AFTERGLOW_WIRED_PID))
        return 1;

    return 0;
}

/*
 * A conexao sera implementada na proxima etapa.
 */
static int xbox360usb_connect(int devId)
{
    (void)devId;
    return 1;
}

/*
 * A desconexao sera implementada junto
 * ao gerenciamento dos dispositivos.
 */
static int xbox360usb_disconnect(int devId)
{
    (void)devId;
    return 0;
}
