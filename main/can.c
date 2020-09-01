#include "can.h"

#include "util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/can.h"

#define CAN_TX_GPIO_NUM GPIO_NUM_21
#define CAN_RX_GPIO_NUM GPIO_NUM_22
#define CAN_RX_TASK_PRIO 1

static const char *TAG = "CAN";

// Lock queue: empty = CAN closed; not empty = CAN open
static QueueHandle_t canOpenLockQueue;
static uint8_t canOpenLockDummy;

static can_general_config_t *canGeneralConfig;

static void canRxTask(void *arg)
{
    while (1)
    {
        // Block task while CAN is not open
        if (xQueuePeek(canOpenLockQueue, &canOpenLockDummy, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "rx task unblocked");
            can_message_t msg;

            // Try receiving for 100ms max
            if (can_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK)
                xQueueSendToBack(canRxQueue, &msg, portMAX_DELAY);
        }
    }
}

void canInit(void)
{
    canRxQueue = xQueueCreate(8, sizeof(can_message_t));
    canOpenLockQueue = xQueueCreate(1, sizeof(canOpenLockDummy));

    xTaskCreate(canRxTask, "CAN RX", 4096, NULL, CAN_RX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "init completed");
}

bool canIsOpen(void)
{
    return xQueuePeek(canOpenLockQueue, &canOpenLockDummy, 0) == pdTRUE;
}

can_mode_t canGetMode(void)
{
    return (canGeneralConfig != NULL) ? canGeneralConfig->mode : -1;
}

esp_err_t canOpen(can_mode_t mode, can_timing_config_t *timingConfig)
{
    if (canIsOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "opening");

    canGeneralConfig = &(can_general_config_t)CAN_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO_NUM, CAN_RX_GPIO_NUM, mode);
    const can_filter_config_t filterConfig = CAN_FILTER_CONFIG_ACCEPT_ALL();

    UTIL_CHECK_RETURN(can_driver_install(canGeneralConfig, timingConfig, &filterConfig), ESP_FAIL);
    UTIL_CHECK_RETURN(can_start(), ESP_FAIL);

    xQueueSendToBack(canOpenLockQueue, &(uint8_t){1}, portMAX_DELAY);

    ESP_LOGI(TAG, "opened");
    return ESP_OK;
}

esp_err_t canClose(void)
{
    if (!canIsOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "closing");
    xQueueReceive(canOpenLockQueue, &canOpenLockDummy, portMAX_DELAY);

    can_stop();
    can_driver_uninstall();

    ESP_LOGI(TAG, "closed");
    return ESP_OK;
}
