#include <stdio.h>

#include "lcd.h"
#include "st77xx.h"
#include "st77xx_params.h"

#include "lsm6dsxx.h"
#include "lsm6dsxx_params.h"

int main(void){
    lcd_t dev;
    dev.driver = &lcd_st77xx_driver;

    lsm6dsxx_t devv;
    lsm6dsxx_3d_data_t acc_val;

    if (lcd_init(&dev, &st77xx_params[0]) != 0){
        puts("initialization failed");
        return 1;
    }

    if (lsm6dsxx_init(&devv, lsm6dsxx_params) != LSM6DSXX_OK){
        puts("initialization failed");
        return 1;
    }

    if (lsm6dsxx_acc_power_down(&devv) != LSM6DSXX_OK){
        puts("power down failed");
        return 1;
    }

    if (lsm6dsxx_acc_power_up(&devv) != LSM6DSXX_OK){
        puts("power up failed");
        return 1;
    }

    uint16_t x_start = 0;
    uint16_t x_end = dev.params->lines;
    uint16_t max_screen = dev.params->lines;
    uint16_t old_x_start = 0;
    uint16_t old_x_end = max_screen;
    int x_smooth = 0;

    while(1){
        if (lsm6dsxx_read_acc(&devv, &acc_val) == LSM6DSXX_OK){
            printf("Accelerometer x: %i y: %i z: %i\n", acc_val.x, acc_val.y, acc_val.z);
            
            x_smooth = 0.65 * x_smooth + 0.35 * acc_val.x;  //smooth out values

            if (x_smooth > 900) x_smooth = 900;
            if (x_smooth < -900) x_smooth = -900;
            
            lcd_fill(&dev, old_x_start, old_x_end, 0, dev.params->rgb_channels - 1, 0xFFFF);  //refresh pixels with white

            x_start = ((x_smooth - 900) * (-max_screen)) / 2000;
            x_end = x_start + (max_screen / 10);    
            
            lcd_fill(&dev, x_start, x_end, 0, dev.params->rgb_channels - 1, 0x0000);
            
            old_x_start = x_start;
            old_x_end = x_end;
        }
    }
    return 0;
}
