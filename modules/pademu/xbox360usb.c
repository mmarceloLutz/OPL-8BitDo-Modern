
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
static void xbox360usb_release(int pad);
static void xbox360usb_config_set(
    int result,
    int count,
    void *arg
);

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
    int pad;
    int epCount;
    UsbDeviceDescriptor *device;
    UsbConfigDescriptor *config;
    UsbInterfaceDescriptor *interface;
    UsbEndpointDescriptor *endpoint;
    char *descriptorEnd;
    char *descriptorPtr;

    for (pad = 0; pad < XBOX360_MAX_PADS; pad++) {
        if (xbox360dev[pad].usb_id == -1 &&
            xbox360dev[pad].enabled)
            break;
    }

    if (pad >= XBOX360_MAX_PADS)
        return 1;

    device = (UsbDeviceDescriptor *)
        UsbGetDeviceStaticDescriptor(
            devId, NULL, USB_DT_DEVICE);

    if (device == NULL)
        return 1;

    config = (UsbConfigDescriptor *)
        UsbGetDeviceStaticDescriptor(
            devId, device, USB_DT_CONFIG);

    if (config == NULL)
        return 1;

   
    if (config->bLength < sizeof(UsbConfigDescriptor) ||
        config->wTotalLength <
            config->bLength + sizeof(UsbInterfaceDescriptor)) {
        return 1;
    }

    interface = (UsbInterfaceDescriptor *)
        ((char *)config + config->bLength);

    if (interface->bDescriptorType != USB_DT_INTERFACE ||
        interface->bLength < sizeof(UsbInterfaceDescriptor)) {
        return 1;
    }

    xbox360dev[pad].usb_id = devId;
    xbox360dev[pad].status =
        XBOX360USB_STATE_AUTHORIZED;

    xbox360dev[pad].controlEndp =
        UsbOpenEndpoint(devId, NULL);

    if (xbox360dev[pad].controlEndp < 0) {
        xbox360usb_release(pad);
        return 1;
    }

    
    endpoint = (UsbEndpointDescriptor *)
        UsbGetDeviceStaticDescriptor(
            devId, NULL, USB_DT_ENDPOINT);

    descriptorEnd = (char *)config +
        config->wTotalLength;

    descriptorPtr = (char *)endpoint;

    epCount = interface->bNumEndpoints;

    while (epCount-- > 0 && endpoint != NULL) {

               if (descriptorPtr < (char *)config ||
            descriptorPtr >= descriptorEnd)
            break;

        if (descriptorEnd - descriptorPtr <
            sizeof(UsbEndpointDescriptor))
            break;

        if (endpoint->bLength <
            sizeof(UsbEndpointDescriptor))
            break;
        
        if (descriptorEnd - descriptorPtr <
            endpoint->bLength)
            break;

        if ((endpoint->bmAttributes & 0x03) ==
            USB_ENDPOINT_XFER_INT) {

            if ((endpoint->bEndpointAddress &
                 USB_ENDPOINT_DIR_MASK) == USB_DIR_IN &&
                xbox360dev[pad].interruptEndp < 0) {

                xbox360dev[pad].interruptEndp =
                    UsbOpenEndpointAligned(
                        devId, endpoint);
            }
        }

       descriptorPtr += endpoint->bLength;
        endpoint = (UsbEndpointDescriptor *)
            descriptorPtr;
    }

    if (xbox360dev[pad].interruptEndp < 0) {
        xbox360usb_release(pad);
        return 1;
    }

    xbox360dev[pad].status |=
        XBOX360USB_STATE_CONNECTED;

    /*
     * Configura o dispositivo USB.
     * O estado RUNNING sera definido
     * somente apos a confirmacao.
     */
    if (UsbSetDeviceConfiguration(
            xbox360dev[pad].controlEndp,
            config->bConfigurationValue,
            xbox360usb_config_set,
            (void *)pad) != USB_RC_OK) {
        xbox360usb_release(pad);
        return 1;
    }

    return 0;
}
/*
 * A desconexao sera implementada junto
 * ao gerenciamento dos dispositivos.
 */


static int xbox360usb_disconnect(int devId)
{
    int pad;

    for (pad = 0; pad < XBOX360_MAX_PADS; pad++) {

        if (xbox360dev[pad].usb_id == devId) {

            /* Registrar desconexao pendente */
            xbox360dev[pad].disconnect_pending = 1;

            /* Impedir que o controle continue ativo */
            xbox360dev[pad].status &=
                ~XBOX360USB_STATE_RUNNING;

            /* Tentar liberar os recursos USB */
            xbox360usb_release(pad);

            break;
        }
    }

    return 0;
}

static void xbox360usb_release(int pad)
{
    xbox360usb_device *dev;
    int locked = 0;

    if (pad < 0 || pad >= XBOX360_MAX_PADS)
        return;

    dev = &xbox360dev[pad];

    if (dev->sema >= 0) {
        if (PollSema(dev->sema) == 0)
            locked = 1;
        else
            return;
    }

    dev->status = XBOX360USB_STATE_DISCONNECTED;

    if (dev->interruptEndp >= 0) {
        UsbCloseEndpoint(dev->interruptEndp);
        dev->interruptEndp = -1;
    }

    if (dev->controlEndp >= 0) {
        UsbCloseEndpoint(dev->controlEndp);
        dev->controlEndp = -1;
    }

    dev->usb_id = -1;
    dev->usb_resultcode = 1;
    dev->update_rum = 0;

    dev->data[0] = 0xFF;
    dev->data[1] = 0xFF;

    mips_memset(&dev->data[2], 0x7F, 4);
    mips_memset(&dev->data[6], 0x00, 12);

    if (locked)
        SignalSema(dev->sema);
}

int xbox360usb_init(u8 pads, u8 options)
{
    int pad;

    (void)options;

    for (pad = 0; pad < XBOX360_MAX_PADS; pad++) {
        xbox360dev[pad].usb_id = -1;
        xbox360dev[pad].sema = -1;
        xbox360dev[pad].cmd_sema = -1;
        xbox360dev[pad].controlEndp = -1;
        xbox360dev[pad].interruptEndp = -1;

        xbox360dev[pad].enabled = (pads >> pad) & 1;
        xbox360dev[pad].status =
            XBOX360USB_STATE_DISCONNECTED;

        xbox360dev[pad].usb_resultcode = 1;
        xbox360dev[pad].disconnect_pending = 0;
        xbox360dev[pad].update_rum = 0;
        xbox360dev[pad].lrum = 0;
        xbox360dev[pad].rrum = 0;
        xbox360dev[pad].analog_btn = 0;
        xbox360dev[pad].btn_delay = 0;

        xbox360dev[pad].oldled[0] = 0;
        xbox360dev[pad].oldled[1] = 0;

        xbox360dev[pad].data[0] = 0xFF;
        xbox360dev[pad].data[1] = 0xFF;

        mips_memset(&xbox360dev[pad].data[2], 0x7F, 4);
        mips_memset(&xbox360dev[pad].data[6], 0x00, 12);

        xbox360dev[pad].sema =
            CreateMutex(IOP_MUTEX_UNLOCKED);

        xbox360dev[pad].cmd_sema =
            CreateMutex(IOP_MUTEX_UNLOCKED);

        if (xbox360dev[pad].sema < 0 ||
            xbox360dev[pad].cmd_sema < 0) {
            return 0;
        }
    }

    if (UsbRegisterDriver(&xbox360_usb_driver) != USB_RC_OK) {
        return 0;
    }

    return 1;
}


static void xbox360usb_config_set(
    int result,
    int count,
    void *arg
)
{
    int pad = (int)arg;

    (void)count;

    if (pad < 0 || pad >= XBOX360_MAX_PADS)
        return;

    if (result != USB_RC_OK)
        return;

    if (xbox360dev[pad].usb_id < 0)
    return;

/* Nao reativar dispositivo em desconexao */
if (xbox360dev[pad].disconnect_pending)
    return;

if (!(xbox360dev[pad].status &
      XBOX360USB_STATE_CONNECTED))
    return;

    xbox360dev[pad].status |=
        XBOX360USB_STATE_CONFIGURED;

    xbox360dev[pad].status |=
        XBOX360USB_STATE_RUNNING;
}
