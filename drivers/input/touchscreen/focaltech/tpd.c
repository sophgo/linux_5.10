#include "tpd.h"
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include "focaltech_common.h"

struct fts_device_t tpd = {0};
struct fts_device_t* gp_tpd = &tpd;

struct tpd_dts_data_t tpd_dts_data = {0};

int tpd_gpio_output(int gpio_num, int value){
    gpio_request(gpio_num, NULL);
    gpio_direction_output(gpio_num, value);
    gpio_free(gpio_num);
    return 0;
}

int tpd_gpio_as_int(int gpio_num){
    int irq_num = 0;
    gpio_request(gpio_num, "tpd-int");
    gpio_direction_input(gpio_num);
    irq_num = gpio_to_irq(gpio_num);
    if (irq_num < 0) {
        FTS_ERROR("gpio_to_irq failed: %d", irq_num);
    } else {
        FTS_INFO("GPIO %d maps to IRQ %d", gpio_num, irq_num);
    }
    return irq_num;
}

int tpd_button_setting(int key_num, struct key_point_t *key_local, struct key_point_t *key_dim_local){
    return 0;
}

int tpd_driver_add(struct tpd_driver_t *tpd_driver){
    FTS_INFO("tpd_driver_add");
    if (tpd_driver != NULL) {
        FTS_INFO("tpd_driver is not NULL");
        if (tpd_driver->tpd_local_init != NULL) {
            FTS_ERROR("tpd_driver->tpd_local_init is not NULL");
            tpd_driver->tpd_local_init();
        }
        else {
            FTS_ERROR("tpd_driver->tpd_init is NULL");
        }
    }
    else {
        FTS_ERROR("tpd_driver is NULL");
    }
    return 0;
}

int tpd_driver_remove(struct tpd_driver_t *tpd_driver){
    return 0;
}

