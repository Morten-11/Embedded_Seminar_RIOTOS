#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lcd.h"
#include "st77xx.h"
#include "st77xx_params.h"

#include "lsm6dsxx.h"
#include "lsm6dsxx_params.h"

#include "mineplex.h"
#include "ztimer.h"
#include "busy_wait.h"

#include "unistd.h"
#include "fcntl.h"

#define BTN_PIN GPIO_PIN(0, 2)
#define BTN_MODE GPIO_IN_PU

static int _tee(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: %s <file> <str>\n", argv[0]);
        return 1;
    }

#if defined(MODULE_NEWLIB) || defined(MODULE_PICOLIBC)
    FILE *f = fopen(argv[1], "w+");
    if (f == NULL) {
        printf("error while trying to create %s\n", argv[1]);
        return 1;
    }
    if (fwrite(argv[2], 1, strlen(argv[2]), f) != strlen(argv[2])) {
        puts("Error while writing");
    }
    fclose(f);
#else
    int fd = open(argv[1], O_RDWR | O_CREAT, 00777);
    if (fd < 0) {
        printf("error while trying to create %s\n", argv[1]);
        return 1;
    }
    if (write(fd, argv[2], strlen(argv[2])) != (ssize_t)strlen(argv[2])) {
        puts("Error while writing");
    }
    close(fd);
#endif
    return 0;
}

void mineplex_draw(lcd_t *dev, const char Character, uint16_t x_start, uint16_t y_start, int scale){
    const uint8_t *bmp = mineplex_char(Character);
    uint16_t pix1[25];
    uint16_t pix2[5 * scale * 5 * scale];

    for (int y = 0; y < 5; y++){
        for (int x = 0; x < 5; x++){
            pix1[y * 5 + x] = (bmp[y] & (1 << x)) ? 0xFFFF : 0x0000;

            for (int s = 0; s < scale; s++){
                for (int t = 0; t < scale; t++){
                    int sx = x * scale + t;
                    int sy = y * scale + s;

                    pix2[sy * 5 * scale + sx] = pix1[y * 5 + x];
                }
            }
        }
    }

    lcd_pixmap(dev, x_start, x_start + 5 * scale - 1, y_start, y_start + 5 * scale - 1, pix2);   
}

void draw_score(lcd_t *dev, int number, uint16_t x_start, uint16_t y_start){
    int first_digit = (number/ 1000) % 10;
    int second_digit = (number / 100) % 10;
    int third_digit = (number / 10) % 10;
    int fourth_digit = number % 10;
            
    const char fo_dig_ch = fourth_digit + '0';
    mineplex_draw(dev, fo_dig_ch, x_start + 33, y_start, 2);

    if(third_digit != 0 || second_digit != 0 || first_digit != 0){
        const char th_dig_ch = third_digit + '0';
        mineplex_draw(dev, th_dig_ch, x_start + 22, y_start, 2);
    }
    
    if(second_digit != 0 || first_digit != 0){
        const char se_dig_ch = second_digit + '0';
        mineplex_draw(dev, se_dig_ch, x_start + 11, y_start, 2);
    }

    if(first_digit != 0){
        const char fi_dig_ch = first_digit + '0';
        mineplex_draw(dev, fi_dig_ch, x_start, y_start, 2);
    }
}

//TODO:file reading and writing implementation
void draw_word(lcd_t *dev, const char *word, uint16_t x_start, uint16_t y_start, int scale){
    char c;
    for (unsigned int i = 0; i < strlen(word); i++){
        c = word[i];
        mineplex_draw(dev, c, x_start + 5 * scale * i + 1 * i, y_start, scale);
    }
}

int main(void){
    /*int res = vfs_mount(&const_mount);
    if (res < 0) {
        puts("Error while mounting constfs");
    }
    else {
        puts("constfs mounted successfully");
    }*/

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

    int highscore_1 = 0;
    int highscore_2 = 0;
    int highscore_3 = 0;
    int highscore_4 = 0;
    int highscore_5 = 0;

    int pot_highscore = 0;

    lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x0000);

    uint32_t start = ztimer_now(ZTIMER_MSEC);
    
    
    int fd = open("/nvm0/highscore.txt", O_RDONLY);
    char buffer[40];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

    if (bytesRead > 0) {
        sscanf(buffer, "%d-%d-%d-%d-%d", &highscore_1, &highscore_2, &highscore_3, &highscore_4, &highscore_5);
    }
    close(fd);

    while(1){
        int time = countdown - (ztimer_now(ZTIMER_MSEC) - start);
        mineplex_draw(&dev, time / 1000 + '0', 5, 5, 2);
        mineplex_draw(&dev, ',', 16, 5, 2);
        draw_score(&dev, time % 1000, 16, 5);

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
                start = ztimer_now(ZTIMER_MSEC) - start;
                lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x0000);
                draw_word(&dev, "Pause", max_screen_x / 2 - 27, 12, 2);
                draw_word(&dev, "Highscore:", max_screen_x / 2 - 55, 26, 2);  

                draw_word(&dev, "1 - ", max_screen_x / 2 - 38, 40, 2);
                draw_score(&dev, highscore_1, max_screen_x / 2 + 6, 40);
                draw_word(&dev, "2 - ", max_screen_x / 2 - 38, 54, 2);
                draw_score(&dev, highscore_2, max_screen_x / 2 + 6, 54);
                draw_word(&dev, "3 - ", max_screen_x / 2 - 38, 68, 2);
                draw_score(&dev, highscore_3, max_screen_x / 2 + 6, 68);
                draw_word(&dev, "4 - ", max_screen_x / 2 - 38, 82, 2);
                draw_score(&dev, highscore_4, max_screen_x / 2 + 6, 82);
                draw_word(&dev, "5 - ", max_screen_x / 2 - 38, 96, 2);
                draw_score(&dev, highscore_5, max_screen_x / 2 + 6, 96);
                puts("Pause");

                while(1) {
                    if(gpio_read(BTN_PIN) == 0){
                        lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x0000);
                        puts("Resume");
                        start = ztimer_now(ZTIMER_MSEC) - start;
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
                    score += 15 - time / 1000;
                    printf("Current Score: %d\n", score);
                    lcd_fill(&dev, random_num_x, random_num_x + max_screen_x * multiplier, random_num_y, random_num_y + max_screen_y * multiplier, 0x0000);
                    start = ztimer_now(ZTIMER_MSEC);
                }
            }

            if (in_target == true){
                in_target = false;
                if (score % 4 == 0 && multiplier >= 0.21) multiplier -= 0.01;
                if (countdown >= 2500) countdown -= 125;
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
            
            if (ztimer_now(ZTIMER_MSEC) - start >= countdown || score >= 9999){
                pot_highscore = score;
                lcd_fill(&dev, 0, max_screen_x, 0, max_screen_y, 0x00F0);
                if (score >= 9999) draw_word(&dev, "You won!", max_screen_x / 2 - 44, 12, 2);
                else draw_word(&dev, "Game Over", max_screen_x / 2 - 49, 12, 2);
                draw_word(&dev, "Press button", max_screen_x / 2 - 66, 26, 2);
                draw_word(&dev, "to play again", max_screen_x / 2 - 71, 40, 2);
                if (pot_highscore > highscore_5) draw_word(&dev, "New Highscore:", max_screen_x / 2 - 77, 60, 2);
                else draw_word(&dev, "Your score:", max_screen_x / 2 - 60, 60, 2);
                draw_score(&dev, score, max_screen_x / 2 - 22, 74);
                puts("Game Over - Press Button to play again");

                if (pot_highscore > highscore_1) {
                    highscore_5 = highscore_4;
                    highscore_4 = highscore_3;
                    highscore_3 = highscore_2;
                    highscore_2 = highscore_1;
                    highscore_1 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_2) {
                    highscore_5 = highscore_4;
                    highscore_4 = highscore_3;
                    highscore_3 = highscore_2;
                    highscore_2 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_3){
                    highscore_5 = highscore_4;
                    highscore_4 = highscore_3;
                    highscore_3 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_4) {
                    highscore_5 = highscore_4;
                    highscore_4 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_5) {
                    highscore_5 = pot_highscore;
                    pot_highscore = 0;
                }

                char output_str[40];

                sprintf(output_str, "%d-%d-%d-%d-%d", highscore_1, highscore_2, highscore_3, highscore_4, highscore_5);

                char *input[3] = {"tee", "/nvm0/highscore.txt", output_str};
                _tee(3, input);

                while(1) {
                    if(gpio_read(BTN_PIN) == 0){
                        score = 0;
                        multiplier = 0.4;
                        countdown = 10000;
                        in_target = true;
                        puts("New Game");
                        start = ztimer_now(ZTIMER_MSEC);
                        break;
                    }
                    else continue;
                }
            }
            draw_score(&dev, score, max_screen_x - 50, 5);
        }
    }
    return 0;
}