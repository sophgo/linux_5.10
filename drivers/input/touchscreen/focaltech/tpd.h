#ifndef __TPD_H
#define __TPD_H


#define RTPM_PRIO_TPD 10
#define TPD_DEVICE "ft3x07"
#define TPD_KEY_NUM 3
#define TPD_RES_X 640
#define TPD_RES_Y 480

#define SKIP_IC_CHECK 1

struct sched_param{
    int sched_priority;
};

struct fts_device_t{
    struct device *tpd_dev;
};

struct key_point_t{
    int key_x;
    int key_y;
};

struct tpd_dts_data_t{
    int use_tpd_button;
    int tpd_key_num;
    int touch_max_num;
    struct key_point_t tpd_key_local[TPD_KEY_NUM];
    struct key_point_t tpd_key_dim_local[TPD_KEY_NUM];
};

struct tpd_driver_t{
    char *tpd_device_name;
    void (*suspend)(struct device *dev);
    void (*resume)(struct device *dev);
    int (*tpd_local_init)(void);
};

extern struct fts_device_t* gp_tpd;
extern struct tpd_dts_data_t tpd_dts_data;
int tpd_gpio_output(int gpio_num, int value);
int tpd_gpio_as_int(int gpio_num);
int tpd_button_setting(int key_num, struct key_point_t *key_local, struct key_point_t *key_dim_local);
int tpd_driver_add(struct tpd_driver_t *tpd_driver);
int tpd_driver_remove(struct tpd_driver_t *tpd_driver);

#endif