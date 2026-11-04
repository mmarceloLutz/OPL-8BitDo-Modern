
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
/*
 * Converte um eixo Xbox 360 de 16 bits
 * para um eixo PS2 de 8 bits.
 */
static u8 xbox360usb_convert_axis(u8 low, u8 high)
{
    int value;

    /* Reconstruir valor signed de 16 bits */
    value = (short)((high << 8) | low);

    /* Converter de -32768..32767 para 0..255 */
    return (u8)((value + 32768) >> 8);
}

// #define DPRINTF(x...) printf(x)
#define DPRINTF(x...)

static int xbox360usb_probe(int devId);
static int xbox360usb_connect(int devId);
static int xbox360usb_disconnect(int devId);
static void xbox360usb_release(int pad);
static void xbox360usb_process_pending_disconnects(void);
/* Inicia uma leitura do relatorio USB do controle */
static int xbox360usb_start_read(int pad);
static void xbox360usb_config_set(
    int result,
    int count,
    void *arg
);

/* Callback de recepcao dos dados USB */
static void xbox360usb_read_callback(
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
    xbox360dev[pad].enabled &&
    !xbox360dev[pad].disconnect_pending)
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
    if (PollSema(dev->sema) == 0) {
        locked = 1;
    } else {
        /* Manter a liberacao pendente */
        dev->disconnect_pending = 1;
        return;
    }
}

/* Nao liberar endpoints durante uma leitura USB */
if (dev->read_pending) {
    dev->disconnect_pending = 1;

    if (locked)
        SignalSema(dev->sema);

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
    /* Limpeza concluida: permitir nova conexao */
    dev->disconnect_pending = 0;

    dev->data[0] = 0xFF;
    dev->data[1] = 0xFF;

    mips_memset(&dev->data[2], 0x80, 4);
    mips_memset(&dev->data[6], 0x00, 12);

    if (locked)
        SignalSema(dev->sema);
}

/*
 * Processa desconexoes que nao puderam
 * ser concluidas anteriormente.
 *
 * Deve ser chamada em um contexto seguro.
 */
static void xbox360usb_process_pending_disconnects(void)
{
    int pad;

    for (pad = 0; pad < XBOX360_MAX_PADS; pad++) {

        if (xbox360dev[pad].disconnect_pending) {
            xbox360usb_release(pad);
        }
    }
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
        xbox360dev[pad].read_pending = 0;
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

        mips_memset(&xbox360dev[pad].data[2], 0x80, 4);
        mips_memset(&xbox360dev[pad].data[6], 0x00, 12);
        /* Inicializar buffer de recepcao USB */
        mips_memset(
            xbox360dev[pad].usb_buffer,
            0x00,
            XBOX360_MAX_BUFFER_SIZE
        );

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
    /*
 * Solicitar a primeira leitura USB
 * apos configurar o dispositivo.
 */
xbox360usb_start_read(pad);
}

/*
 * Callback executado quando uma transferencia
 * USB de leitura e concluida.
 */
static void xbox360usb_read_callback(
    int result,
    int count,
    void *arg
)
{
    int pad = (int)arg;
    u8 new_data[18];

    if (pad < 0 || pad >= XBOX360_MAX_PADS)
        return;

/* Registrar o resultado da transferencia USB */
xbox360dev[pad].usb_resultcode = result;
/* Registrar que a desconexao pode ser finalizada */
/* Nao processar dados de dispositivos em desconexao */
if (xbox360dev[pad].disconnect_pending)
    goto read_done;

/* Ignorar dispositivos desconectados */
if (xbox360dev[pad].usb_id < 0)
    goto read_done;

/* Verificar resultado da transferencia */
if (result != USB_RC_OK)
    goto read_done;

/* Validar o tamanho do relatorio */
if (count < 20 || count > XBOX360_MAX_BUFFER_SIZE)
    goto read_done;

/* Validar o cabecalho XInput */
if (xbox360dev[pad].usb_buffer[0] != 0x00 ||
    xbox360dev[pad].usb_buffer[1] != 0x14)
    goto read_done;
/* Inicializar o relatorio temporario */
new_data[0] = 0xFF;
new_data[1] = 0xFF;

mips_memset(&new_data[2], 0x80, 4);
mips_memset(&new_data[6], 0x00, 12);

/* Converter D-Pad Xbox 360 para PS2 */

/* Cima */
if (xbox360dev[pad].usb_buffer[2] & 0x01)
    new_data[0] &= ~0x10;

/* Direita */
if (xbox360dev[pad].usb_buffer[2] & 0x08)
    new_data[0] &= ~0x20;

/* Baixo */
if (xbox360dev[pad].usb_buffer[2] & 0x02)
    new_data[0] &= ~0x40;

/* Esquerda */
if (xbox360dev[pad].usb_buffer[2] & 0x04)
    new_data[0] &= ~0x80;

   /* Converter botoes A, B, X e Y para PS2 */

/* A -> Cruz */
if (xbox360dev[pad].usb_buffer[3] & 0x10)
    new_data[1] &= ~0x40;

/* B -> Circulo */
if (xbox360dev[pad].usb_buffer[3] & 0x20)
    new_data[1] &= ~0x20;

/* X -> Quadrado */
if (xbox360dev[pad].usb_buffer[3] & 0x40)
    new_data[1] &= ~0x80;

/* Y -> Triangulo */
if (xbox360dev[pad].usb_buffer[3] & 0x80)
    new_data[1] &= ~0x10;
  /* Converter Start e Back */

/* Start -> Start */
if (xbox360dev[pad].usb_buffer[2] & 0x10)
    new_data[0] &= ~0x08;

/* Back -> Select */
if (xbox360dev[pad].usb_buffer[2] & 0x20)
    new_data[0] &= ~0x01;

/* Converter LB e RB */

/* LB -> L1 */
if (xbox360dev[pad].usb_buffer[3] & 0x01)
    new_data[1] &= ~0x04;

/* RB -> R1 */
if (xbox360dev[pad].usb_buffer[3] & 0x02)
    new_data[1] &= ~0x08;
    /* Converter os botoes dos analogicos */

/* LS -> L3 */
if (xbox360dev[pad].usb_buffer[2] & 0x40)
    new_data[0] &= ~0x02;

/* RS -> R3 */
if (xbox360dev[pad].usb_buffer[2] & 0x80)
    new_data[0] &= ~0x04;
   /* Converter gatilhos analogicos para botoes PS2 */

/* LT -> L2 */
if (xbox360dev[pad].usb_buffer[4] > 0x30)
    new_data[1] &= ~0x01;

/* RT -> R2 */
if (xbox360dev[pad].usb_buffer[5] > 0x30)
    new_data[1] &= ~0x02;
    /* Converter os eixos analogicos Xbox 360 para PS2 */

/* Analogico direito - eixo X */
new_data[2] =
    xbox360usb_convert_axis(
        xbox360dev[pad].usb_buffer[10],
        xbox360dev[pad].usb_buffer[11]
    );

/* Analogico direito - eixo Y */
new_data[3] =
    255 - xbox360usb_convert_axis(
        xbox360dev[pad].usb_buffer[12],
        xbox360dev[pad].usb_buffer[13]
    );

/* Analogico esquerdo - eixo X */
new_data[4] =
    xbox360usb_convert_axis(
        xbox360dev[pad].usb_buffer[6],
        xbox360dev[pad].usb_buffer[7]
    );

/* Analogico esquerdo - eixo Y */
new_data[5] =
    255 - xbox360usb_convert_axis(
        xbox360dev[pad].usb_buffer[8],
        xbox360dev[pad].usb_buffer[9]
    );
    /*
     * Futuramente:
     * 1. Validar o relatorio XInput.
     * 2. Converter os botoes para o formato PS2.
     * 3. Solicitar a proxima leitura USB.
     */
    read_done:
    /*
     * A transferencia terminou.
     * Permitir uma nova leitura ou finalizar
     * uma desconexao pendente.
     */
    xbox360dev[pad].read_pending = 0;
}
/*
 * Inicia a leitura USB de um controle Xbox 360.
 */
static int xbox360usb_start_read(int pad)
{
    xbox360usb_device *dev;
    int ret;

    if (pad < 0 || pad >= XBOX360_MAX_PADS)
    return 0;

/*
 * Tentar finalizar desconexoes anteriores
 * antes de iniciar uma nova leitura.
 */
xbox360usb_process_pending_disconnects();

dev = &xbox360dev[pad];

     /* Adquirir o semaforo do dispositivo */
    if (dev->sema < 0 || PollSema(dev->sema) != 0)
        return 0;

    /* Verificar se o controle esta disponivel */
    if (!dev->enabled ||
        dev->usb_id < 0 ||
        dev->disconnect_pending ||
        !(dev->status & XBOX360USB_STATE_CONFIGURED) ||
        dev->interruptEndp < 0 ||
        dev->read_pending) {

        SignalSema(dev->sema);
        return 0;
    }

    /* Marcar a leitura como pendente */
dev->read_pending = 1;
dev->usb_resultcode = 1;

/* Solicitar o relatorio USB */
ret = UsbInterruptTransfer(
    dev->interruptEndp,
    dev->usb_buffer,
    XBOX360_MAX_BUFFER_SIZE,
    xbox360usb_read_callback,
    (void *)pad
);

/* Verificar se a transferencia foi aceita */
if (ret != USB_RC_OK) {
    /*
     * A solicitacao falhou.
     * Nenhuma transferencia foi iniciada.
     */
    dev->usb_resultcode = ret;
    dev->read_pending = 0;

       SignalSema(dev->sema);
    return 0;
}

SignalSema(dev->sema);
return 1;
}
/*
 * Retorna o estado de conexao de um controle Xbox 360.
 */
int xbox360usb_get_status(int port)
{
    xbox360usb_device *dev;

    if (port < 0 || port >= XBOX360_MAX_PADS)
        return 0;

    dev = &xbox360dev[port];

    if (!dev->enabled ||
        dev->usb_id < 0 ||
        dev->disconnect_pending)
        return 0;

    if (!(dev->status & XBOX360USB_STATE_CONFIGURED))
        return 0;

    return 1;
}
/*
 * Retorna o modelo de controle emulado.
 */
int xbox360usb_get_model(int port)
{
    if (!xbox360usb_get_status(port))
        return 0;

    return XBOX360_MODEL_PS2;
}
/*
 * Copia os dados do controle Xbox 360
 * convertidos para o formato PS2.
 */
/*
 * Copia os dados do controle Xbox 360
 * convertidos para o formato PS2.
 */
int xbox360usb_get_data(u8 *dst, int size, int port)
{
    xbox360usb_device *dev;

    if (dst == NULL || size <= 0 || size > 18)
        return 0;

    if (port < 0 || port >= XBOX360_MAX_PADS)
        return 0;

    dev = &xbox360dev[port];

    if (!xbox360usb_get_status(port)) {
        mips_memset(dst, 0x00, size);

        dst[0] = 0xFF;

        if (size > 1)
            dst[1] = 0xFF;

        if (size > 2)
            mips_memset(&dst[2], 0x80,
                        (size - 2 < 4) ? size - 2 : 4);

        return 0;
}
/*
 * Solicitar nova leitura USB caso
 * nao exista transferencia pendente.
 */
if (!dev->read_pending)
    xbox360usb_start_read(port);

mips_memcpy(dst, dev->data, size);

return dev->analog_btn & 1;
}
/*
 * Configura o modo analogico do controle.
 */
void xbox360usb_set_mode(int mode, int lock, int port)
{
    if (port < 0 || port >= XBOX360_MAX_PADS)
        return;

    if (lock == 3)
        xbox360dev[port].analog_btn = 3;
    else
        xbox360dev[port].analog_btn = mode;
}
/*
 * Armazena os comandos de vibracao enviados pelo PADEMU.
 */
void xbox360usb_set_rumble(u8 lrum, u8 rrum, int port)
{
    xbox360usb_device *dev;

    if (port < 0 || port >= XBOX360_MAX_PADS)
        return;

    dev = &xbox360dev[port];

    if (dev->sema < 0)
        return;

    if (PollSema(dev->sema) != 0)
        return;

    dev->lrum = lrum;
    dev->rrum = rrum;
    dev->update_rum = 1;

    SignalSema(dev->sema);
}
/*
 * Reinicializa os dados e comandos pendentes
 * dos controles Xbox 360.
 */
void xbox360usb_reset(void)
{
    int pad;
    xbox360usb_device *dev;

    for (pad = 0; pad < XBOX360_MAX_PADS; pad++) {
        dev = &xbox360dev[pad];

        if (dev->sema < 0)
            continue;

        if (PollSema(dev->sema) != 0)
            continue;

        dev->data[0] = 0xFF;
        dev->data[1] = 0xFF;

        mips_memset(&dev->data[2], 0x80, 4);
        mips_memset(&dev->data[6], 0x00, 12);

        dev->lrum = 0;
        dev->rrum = 0;
        dev->update_rum = 1;
        dev->analog_btn = 0;
        dev->btn_delay = 0;

        SignalSema(dev->sema);
    }
}
