#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "lcd.h"
#include "st77xx.h"
#include "st77xx_params.h"

#include "lsm6dsxx.h"
#include "lsm6dsxx_params.h"

#include "mineplex.h"
#include "ztimer.h"

#define BTN_PIN GPIO_PIN(0, 2)
#define BTN_MODE GPIO_IN_PU

int main(void){
    lcd_t dev;
    dev.driver = &lcd_st77xx_driver;

    lsm6dsxx_t devv;
    lsm6dsxx_3d_data_t acc_val;

    srand(444);

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

    gpio_init(BTN_PIN, BTN_MODE);

    uint16_t max_screen_x = dev.params->lines;
    uint16_t max_screen_y = dev.params->rgb_channels - 1;

    uint16_t x_start;
    uint16_t x_end;
    uint16_t old_x_start = 0;
    uint16_t old_x_end = dev.params->lines;

    uint16_t y_start;
    uint16_t y_end;
    uint16_t old_y_start = 0;
    uint16_t old_y_end = dev.params->rgb_channels;

    int x_smooth = 0;
    int y_smooth = 0;
    
    int score = 0;
    bool in_target = true;

    int random_num_x = (rand() % (3 * max_screen_x / 5)) + max_screen_x / 5;
    int random_num_y = (rand() % (3 * max_screen_y / 5)) + max_screen_y / 5;
    double multiplier = 0.4;
    uint32_t countdown = 10000;
    
    int area_start_x = random_num_x + 3;
    int area_end_x = random_num_x + max_screen_x * multiplier - 3;
    int area_start_y = random_num_y + 3;
    int area_end_y = random_num_y + max_screen_y * multiplier - 3;

    lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x0000);

    uint32_t start = ztimer_now(ZTIMER_MSEC);

    while(1){
        if (lsm6dsxx_read_acc(&devv, &acc_val) == LSM6DSXX_OK){
            //printf("Accelerometer x: %i y: %i z: %i\n", acc_val.x, acc_val.y, acc_val.z);

            x_smooth = 0.6 * x_smooth + 0.4 * acc_val.x;
            y_smooth = 0.6 * y_smooth + 0.4 * acc_val.y;

            if (x_smooth > 700) x_smooth = 700;
            if (x_smooth < -700) x_smooth = -700;

            if (y_smooth > 700) y_smooth = 700;
            if (y_smooth < -700) y_smooth = -700;
            
            x_start = ((x_smooth - 700) * (-max_screen_x)) / 1540;
            x_end = x_start + (max_screen_x / 10);   

            y_start = ((y_smooth - 700) * (-max_screen_y)) / 1540;
            y_end = y_start + (max_screen_x / 10);   
            
            if(gpio_read(BTN_PIN) == 0){
                lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x0000);
                lcd_fill(&dev, 0.25 * max_screen_x, 0.4 *max_screen_x, 0.2 * max_screen_y, 0.8 *max_screen_y, 0xFFFF);
                lcd_fill(&dev, 0.6 * max_screen_x, 0.75 *max_screen_x, 0.2 * max_screen_y, 0.8 *max_screen_y, 0xFFFF); 
                puts("Pause");

                while(1) {
                    if(gpio_read(BTN_PIN) == 0){
                        lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x0000);
                        puts("Resume");
                        start = ztimer_now(ZTIMER_MSEC);
                        break;
                    }
                    else continue;
                }
            }

            //deleting pixels with white
            if (x_start < old_x_start) lcd_fill(&dev, x_end, old_x_end, old_y_start, old_y_end, 0x0000);  
            else lcd_fill(&dev, old_x_start, x_start, old_y_start, old_y_end, 0x0000);

            if (y_start < old_y_start) lcd_fill(&dev, old_x_start, old_x_end, y_end, old_y_end, 0x0000); 
            else lcd_fill(&dev, old_x_start, old_x_end, old_y_start, y_start, 0x0000); 

            lcd_fill(&dev, x_start, x_end, y_start, y_end, 0xFF00);  
            
            old_x_start = x_start;
            old_x_end = x_end;
            old_y_start = y_start;
            old_y_end = y_end;


            if (x_end <= area_end_x && x_start <= area_end_x && x_end >= area_start_x && x_start >= area_start_x){
                if (y_end <= area_end_y && y_start <= area_end_y && y_end >= area_start_y && y_start >= area_start_y) {
                    in_target = true;
                    score += 1;
                    printf("Current Score: %d\n", score);
                    lcd_fill(&dev, random_num_x, random_num_x + max_screen_x * multiplier, random_num_y, random_num_y + max_screen_y * multiplier, 0x0000);
                    start = ztimer_now(ZTIMER_MSEC);
                }
            }

            if (in_target == true){
                in_target = false;
                if (score % 10 == 0 && multiplier >= 0.21) multiplier -= 0.01;
                if (score % 5 == 0 && countdown >= 3000) countdown -= 50;
                int mult = multiplier * 100;

                random_num_x = (rand() % ((100 - mult) * max_screen_x) / 100);
                random_num_y = (rand() % ((100 - mult) * max_screen_y) / 100);

                area_end_x = random_num_x + max_screen_x * multiplier - 2;
                area_end_y = random_num_y + max_screen_y * multiplier - 2;
                area_start_x = random_num_x + 2;
                area_start_y = random_num_y + 2;
            }

            lcd_fill(&dev, area_start_x - 2, area_start_x, area_start_y - 2, area_end_y + 2, 0x000F);
            lcd_fill(&dev, area_end_x, area_end_x + 2, area_start_y - 2, area_end_y + 2, 0x000F);
            lcd_fill(&dev, area_start_x - 2, area_end_x + 2, area_start_y - 2, area_start_y, 0x000F);
            lcd_fill(&dev, area_start_x - 2, area_end_x + 2, area_end_y, area_end_y + 2, 0x000F);
            
            if (ztimer_now(ZTIMER_MSEC) - start >= countdown){
                lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x00F0);
                puts("Game Over - Press Button to play again");
                while(1) {
                    if(gpio_read(BTN_PIN) == 0){
                        score = 0;
                        multiplier = 0.4;
                        in_target = true;
                        puts("New Game");
                        start = ztimer_now(ZTIMER_MSEC);
                        break;
                    }
                    else continue;
                }
            }
        }
    }
    return 0;
}