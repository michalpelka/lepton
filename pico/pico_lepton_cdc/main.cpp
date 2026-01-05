#define CFG_TUD_CDC_RX_BUFSIZE 16*2048
#define CFG_TUD_CDC_EP_BUFSIZE 16*2048

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"


#include "hardware/sync.h"

#include <LEPTON_OEM.h>
#include <LEPTON_SDK.h>
#include <LEPTON_SYS.h>
#include <LEPTON_Types.h>
#include <stdio.h>
#include "voisp.h"
#include "base64.h"
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "crc16.h"
#include "base64.h"
#include "class/cdc/cdc_device.h"
#include "tusb.h"
LEP_CAMERA_PORT_DESC_T m_lepPort; //! i2c port for Lepton camera
const uint GPIO_Vsync_PIN = 14;

#define SPI_INSTANCE spi0

#define LEPTON_ENABLE_TELEMETRY 1
const uint GPIO_SPI_CS = 17;
const uint GPIO_SPI_MOSI_TX = 19;
const uint GPIO_SPI_MISO_RX = 16;
const uint GPIO_SPI_SCK = 18;
const uint GPIO_DBG = 15;
const uint GPIO_LEPTON_RST = 21;
const uint GPIO_LEPTON_PWDN = 20;

const size_t VOSPI_FRAME_SIZE(164);
const size_t BUFFER_VOSPI_FRAMES_VALID = LEPTON_ENABLE_TELEMETRY ? 61 : 60;
const size_t BUFFER_VOSPI_FRAMES = 62;
const size_t LEP_SPI_BUFFER = VOSPI_FRAME_SIZE * BUFFER_VOSPI_FRAMES;
const size_t VOSPI_FRAME_SIZE_B64 = VOSPI_FRAME_SIZE * 2; // base64 encoded size with some headroom
const size_t CDC_HEADER_SIZE=128;



// Critical section
static uint dma_tx = 255;
static uint dma_rx = 255;
static TaskHandle_t spiTaskHandle = NULL;
static TaskHandle_t USBSendTaskHandle = NULL;
static TaskHandle_t TinyUSBTaskHandle = NULL;

static SemaphoreHandle_t counterMutex;
using SegmentData = std::array<uint8_t, LEP_SPI_BUFFER>;
std::array<SegmentData, 4> segments;

bool isFrameReadyToSend = false;
std::array<SegmentData, 4> segmentsReady;
std::array<SegmentData, 4> segmentsBeingSend;



void CheckLepResult(LEP_RESULT result, const char* msg) {
    if (result != LEP_OK) {

        for (int i =0; i < 10; i++) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(25);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            sleep_ms(25);
            printf("%s failed\n", msg);
        }
        watchdog_reboot(0,0,0);
    }
    else {
        //printf("%s succeeded\n", msg);
    }
}

void gpio_callback(uint gpio, uint32_t events) {
    if (gpio != GPIO_Vsync_PIN) return;
    //if (!(events & GPIO_IRQ_EDGE_FALL)) return;
    if (spiTaskHandle) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(spiTaskHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

static uint8_t tx_dummy[LEP_SPI_BUFFER] = {0};
static uint8_t rx_buf[LEP_SPI_BUFFER];
static uint8_t encoded[VOSPI_FRAME_SIZE_B64];
static  uint8_t cdc_header[CDC_HEADER_SIZE];
static int frameNo = 0;

void start_spi_dma(const uint dma_rx, const uint dma_tx) {
    dma_channel_config c;

    // TX
    c = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(SPI_INSTANCE, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    dma_channel_configure(
        dma_tx, &c,
        &spi_get_hw(SPI_INSTANCE)->dr,
        tx_dummy,
        LEP_SPI_BUFFER,
        false
    );

    // RX
    c = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(SPI_INSTANCE, false));
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);

    dma_channel_configure(
        dma_rx, &c,
        rx_buf,
        &spi_get_hw(SPI_INSTANCE)->dr,
        LEP_SPI_BUFFER,
        false
    );

}

void TinyUSBTask(void *pv) {
    //tud_init(0);
    for (;;) {
        tud_task();                 // poll TinyUSB stack from task context
        vTaskDelay(1);
    }
}

static bool usb_cdc_send_all(const uint8_t *buf, size_t len)
{
    size_t written = 0;

    while (written < len)
    {
        tud_task();   // <-- keep USB running even if FIFO is full

        uint32_t avail = tud_cdc_write_available();
        if (avail == 0) {
            taskYIELD(); // allow TinyUSBTask to process
            continue;
        }

        uint32_t n = tud_cdc_write(buf + written,
                                   MIN(len - written, avail));

        written += n;

        tud_cdc_write_flush();   // queue packet to HW
    }

    return true;
}

void USBSendTask(void *pvParameters) {

    for (;;) {
        tud_task();
        if (tud_cdc_connected()) {
            xSemaphoreTake(counterMutex, portMAX_DELAY);
            auto isFrameReadyToSendLocal = isFrameReadyToSend;
            auto frameNoLocal = frameNo;
            if (isFrameReadyToSendLocal) {
                std::swap(segmentsBeingSend, segmentsReady);
                isFrameReadyToSend = false;
            }
            xSemaphoreGive(counterMutex);
            if (isFrameReadyToSendLocal) {
    #define USE_RAW_DATA
    #ifdef USE_RAW_DATA
                gpio_put(GPIO_DBG, 1);

                const size_t hSize = sniprintf((char*)cdc_header, CDC_HEADER_SIZE, "LEPTON %d %d\n", frameNoLocal, 4*BUFFER_VOSPI_FRAMES_VALID * VOSPI_FRAME_SIZE);
                usb_cdc_send_all(cdc_header, hSize);
                for (size_t seg = 0; seg < 4; seg++) {
                    usb_cdc_send_all(segmentsBeingSend[seg].data(), BUFFER_VOSPI_FRAMES_VALID * VOSPI_FRAME_SIZE);
                }
                gpio_put(GPIO_DBG, 0);
    #endif
    #ifdef USE_B64_ENCODED_DATA
                // send base64 encoded data
                for (size_t seg = 0; seg < 4; seg++) {

                    for (size_t pack = 0; pack < BUFFER_VOSPI_FRAMES_VALID ; pack++) {
                        const uint8_t* packetPtr = segmentsReady[seg].data() + pack * VOSPI_FRAME_SIZE;

                        const size_t encoded_len = b64_encode(packetPtr, VOSPI_FRAME_SIZE, encoded);
                        const size_t hSize = sniprintf((char*)cdc_header, CDC_HEADER_SIZE, "%d %d %d ", frameNoLocal, seg, pack);
                        usb_cdc_send_all(cdc_header, hSize);
                        usb_cdc_send_all(encoded, encoded_len);
                        usb_cdc_send_all((uint8_t*)"\n", 1);
                    }
                }
    #endif
        }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

void WatchdogTask(void *pv) {
    // enable once from a single place
    vTaskDelay(pdMS_TO_TICKS(10));

    for (;;) {
        watchdog_update();
        vTaskDelay(pdMS_TO_TICKS(5));

    }
}

void SPITask(void *pvParameters)
{
      //watchdog_enable(4000, 1);
      static int32_t segmentCount = -1;
      bool frameReady = false;
      for (;;) {
        //  watchdog_update();
        // Wait for VSYNC interrupt
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        sleep_us(200);

        start_spi_dma(dma_rx, dma_tx);
        gpio_put(GPIO_SPI_CS, 0);
        sleep_us(1);
        dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));

          // SPI transmission takes about 5 Milliseconds, wait little while.
        vTaskDelay(pdMS_TO_TICKS(4));
        // encode and send over
        while (dma_channel_is_busy(dma_rx) || dma_channel_is_busy(dma_tx)) {
              taskYIELD();
        }
        //spi_read_blocking(SPI_INSTANCE, 0x00, rx_buf, LEP_SPI_BUFFER);
        sleep_us(1);
        gpio_put(GPIO_SPI_CS, 1);

        uint16_t numberOfPacketsOk = 0;
        uint16_t numberOfPacketDiscard = 0;
        std::optional<uint8_t> segment;
        for (int i = 0; i < BUFFER_VOSPI_FRAMES; i++)
        {
            const auto *packetPtr = rx_buf + i * VOSPI_FRAME_SIZE;

            const auto crcA = VoISP::packet_crc(packetPtr);
            const auto crcC = VoISP::computeCRC(packetPtr, VOSPI_FRAME_SIZE);
            const auto header = VoISP::packet_id(packetPtr);
            bool isDiscard = VoISP::is_discard_packet(header);
            if (isDiscard) {
                numberOfPacketDiscard++;
                continue;
            }
            auto thisSemgent = VoISP::getSegmentNumber(header);
            if (thisSemgent.has_value()) {
                segment = thisSemgent;
            }
            if (crcA == crcC) {
                numberOfPacketsOk++;
            }

        }

        if (numberOfPacketsOk >= 60  && segment.has_value()) {

            if (segmentCount == -1 && segment.value() == 0) {
                segmentCount = 0;
            }

            if (segmentCount != -1 && segment.value() == segmentCount + 1) {
                memcpy(segments[segmentCount].data(),rx_buf, LEP_SPI_BUFFER);
                segmentCount ++;
            }
            if (segmentCount == 4) {
                segmentCount = -1;
                // flash led on frame from SPI
                bool state = gpio_get(PICO_DEFAULT_LED_PIN);
                gpio_put(PICO_DEFAULT_LED_PIN, !state);
                xSemaphoreTake(counterMutex, portMAX_DELAY);
                frameNo++;
                if (!isFrameReadyToSend) {
                    isFrameReadyToSend = true;
                    std::swap(segments, segmentsReady);

                }
                xSemaphoreGive(counterMutex);
            }
            else
            {

            }

        }
        else
        {
            segmentCount = -1; // unitilize segment counter
        }

    }
}

int main() {


    stdio_init_all();
    setvbuf(stdout, NULL, _IONBF, 0);
    // led
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);


    // set SPI
    spi_init(SPI_INSTANCE, 16 * 1000 * 1000);

    spi_set_format(
        SPI_INSTANCE,
        8,              // bits
        SPI_CPOL_1,     // CPOL
        SPI_CPHA_1,     // CPHA
        SPI_MSB_FIRST
    );


    gpio_set_function(GPIO_SPI_MOSI_TX, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI_MISO_RX, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI_SCK, GPIO_FUNC_SPI);
    gpio_init(GPIO_SPI_CS);
    gpio_set_dir(GPIO_SPI_CS, true);

    gpio_init(GPIO_LEPTON_PWDN);
    gpio_init(GPIO_LEPTON_RST);
    gpio_set_dir(GPIO_LEPTON_RST, GPIO_OUT);
    gpio_set_dir(GPIO_LEPTON_PWDN, GPIO_OUT);

    gpio_init(GPIO_DBG);
    gpio_set_dir(GPIO_DBG, true);

    gpio_put(GPIO_LEPTON_RST, false);
    gpio_put(GPIO_LEPTON_PWDN, false);

    // dma
    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);

    sleep_ms(1000);
    printf("Starting Lepton\n");
    printf("reason = 0x%08lx\n", watchdog_hw->reason);
    printf("RP2040-Lepton hello\n");
    sleep_ms(200);
    printf("Deaserting reset");
    gpio_put(GPIO_LEPTON_RST, true);
    gpio_put(GPIO_LEPTON_PWDN, true);
    sleep_ms(1000);

    LEP_RESULT result = LEP_OpenPort(1, LEP_CCI_TWI, 400, &m_lepPort);
    CheckLepResult(result, "LEP_OpenPort");

    // check camera status
    LEP_SDK_VERSION_T lepVersion ;
    result = LEP_GetSDKVersion(&m_lepPort, &lepVersion);
    CheckLepResult(result, "LEP_GetSDKVersion");
    printf("Lepton SDK version %d.%d.%d\n", lepVersion.major, lepVersion.minor, lepVersion.build);

    // check camera boot status
    LEP_SDK_BOOT_STATUS_E bootStatus = LEP_BOOT_STATUS_NOT_BOOTED;

    while  (bootStatus == LEP_BOOT_STATUS_NOT_BOOTED){
        result = LEP_GetCameraBootStatus(&m_lepPort, &bootStatus);
        CheckLepResult(result, "LEP_GetCameraBootStatus");
        printf("Lepton boot status = %d\n", bootStatus);
    }

    // get part number
    LEP_OEM_PART_NUMBER_T part_number;
    result = LEP_GetOemFlirPartNumber(&m_lepPort, &part_number);
    CheckLepResult(result, "LEP_GetOemFlirPartNumber");
    sniprintf(part_number.value, LEP_OEM_MAX_PART_NUMBER_CHAR_SIZE, "%s", part_number.value);
    printf("Part number = %s \n", part_number.value);

    // get OEM software
    LEP_OEM_SW_VERSION_T sw_version;
    result = LEP_GetOemSoftwareVersion(&m_lepPort, &sw_version);
    CheckLepResult(result, "LEP_GetOemSoftwareVersion");
    printf("OEM Software version = %d.%d.%d (DSP %d.%d.%d) \n", sw_version.gpp_major, sw_version.gpp_minor, sw_version.gpp_build,
           sw_version.dsp_major, sw_version.dsp_minor, sw_version.dsp_build);

    // check uptime
    LEP_UINT32 uptime = 0;
    result = LEP_GetSysCameraUpTime(&m_lepPort, &uptime);
    CheckLepResult(result, "LEP_GetSysCameraUpTime");
    printf("uptime = %d ms \n", uptime);


    // set vsync mode
    result = LEP_SetOemGpioVsyncPhaseDelay(&m_lepPort, LEP_OEM_VSYNC_DELAY_NONE);
    CheckLepResult(result, "LEP_SetOemGpioVsyncPhaseDelay");

    // set vsync mode
    result = LEP_SetOemGpioMode(&m_lepPort, LEP_OEM_GPIO_MODE_VSYNC);
    CheckLepResult(result, "LEP_SetOemGpioMode");

    if (LEPTON_ENABLE_TELEMETRY) {
        printf("Enabling telemetry\n");
        // enable telemetry data
        LEP_SYS_TELEMETRY_ENABLE_STATE_E telemetry_enable_state_e;
        telemetry_enable_state_e = LEP_TELEMETRY_ENABLED;
        result = LEP_SetSysTelemetryEnableState(&m_lepPort, telemetry_enable_state_e);
        CheckLepResult(result, "LEP_SetSysTelemetryEnableState");
    }


    stdio_deinit_all();


    // set gpio for ISR
    gpio_init(GPIO_Vsync_PIN);
    gpio_set_dir(GPIO_Vsync_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(GPIO_Vsync_PIN, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    // rtos
    counterMutex = xSemaphoreCreateMutex();
    xTaskCreate(SPITask, "SPITask", 256, NULL, configMAX_PRIORITIES - 1, &spiTaskHandle);
    xTaskCreate(USBSendTask, "USBSendTask", 256, NULL, 1, &USBSendTaskHandle);
    xTaskCreate(TinyUSBTask, "TinyUSBTask", 256, NULL, 2, &TinyUSBTaskHandle);
    // Core masks: bit0 = core0, bit1 = core1
    const UBaseType_t CORE0 = (1 << 0);
    const UBaseType_t CORE1 = (1 << 1);
    vTaskCoreAffinitySet(spiTaskHandle, CORE0);
    vTaskCoreAffinitySet(USBSendTaskHandle, CORE1);
    vTaskCoreAffinitySet(TinyUSBTaskHandle, CORE1);

    vTaskStartScheduler();

}

