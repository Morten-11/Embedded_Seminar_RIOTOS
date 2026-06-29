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
#include "random.h" 

#define BTN_PIN GPIO_PIN(0, 2)
#define BTN_MODE GPIO_IN_PU

#define MAX_OBSTACLES 8

typedef struct {
    int lane;
    int y_start;
    bool active;
} Obstacle;

int colours[4] = {0x0000, 0x00F0, 0x0FF0, 0x000F};

static const char *questions[] = {
    "Berlin is in Germany.",
    "The Moon is a planet.",
    "Water freezes at 0 C.",
    "The Sun is a star.",
    "Cats can fly.",
    "Earth has one moon.",
    "Fire is cold.",
    "H2O is water.",
    "A week has 7 days.",
    "Fish can breathe air.",
    "Light is faster than sound.",
    "An octagon has 8 sides.",
    "Gold is denser than wood.",
    "Humans have 3 hearts.",
    "A byte has 8 bits.",
    "Penguins can fly.",
    "Paris is in France.",
    "The Earth is flat.",
    "Bees make honey.",
    "A triangle has 3 sides."
};

static const bool true_false[] = {
    1, 0, 1, 1, 0, 1, 0, 1, 1, 0,
    1, 1, 1, 0, 1, 0, 1, 0, 1, 1
};



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

void mineplex_draw(lcd_t *dev, const char Character, uint16_t x_start, uint16_t y_start, int scale, int c){
    const uint8_t *bmp = mineplex_char(Character);
    uint16_t pix1[25];
    uint16_t pix2[5 * scale * 5 * scale];

    for (int y = 0; y < 5; y++){
        for (int x = 0; x < 5; x++){
            pix1[y * 5 + x] = (bmp[y] & (1 << x)) ? 0xFFFF : colours[c];

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

void draw_score(lcd_t *dev, int number, uint16_t x_start, uint16_t y_start, int colour){
    int first_digit = (number/ 1000) % 10;
    int second_digit = (number / 100) % 10;
    int third_digit = (number / 10) % 10;
    int fourth_digit = number % 10;
            
    const char fo_dig_ch = fourth_digit + '0';
    mineplex_draw(dev, fo_dig_ch, x_start + 33, y_start, 2, colour);

    if(third_digit != 0 || second_digit != 0 || first_digit != 0){
        const char th_dig_ch = third_digit + '0';
        mineplex_draw(dev, th_dig_ch, x_start + 22, y_start, 2, colour);
    }
    
    if(second_digit != 0 || first_digit != 0){
        const char se_dig_ch = second_digit + '0';
        mineplex_draw(dev, se_dig_ch, x_start + 11, y_start, 2, colour);
    }

    if(first_digit != 0){
        const char fi_dig_ch = first_digit + '0';
        mineplex_draw(dev, fi_dig_ch, x_start, y_start, 2, colour);
    }
}

void draw_string(lcd_t *dev, const char *word, uint16_t x_start, uint16_t y_start, int scale, int d){
    char c;
    for (unsigned int i = 0; i < strlen(word); i++){
        c = word[i];
        mineplex_draw(dev, c, x_start + 5 * scale * i + 1 * i, y_start, scale, d);
    }
}

void boxgame(lcd_t *dev, lsm6dsxx_t *devv, lsm6dsxx_3d_data_t *acc_val, int background){
    uint16_t max_screen_x = (*dev).params->lines;
    uint16_t max_screen_y = (*dev).params->rgb_channels - 1;

    uint16_t x_start;
    uint16_t x_end;
    uint16_t old_x_start = 0;
    uint16_t old_x_end = (*dev).params->lines;

    uint16_t y_start;
    uint16_t y_end;
    uint16_t old_y_start = 0;
    uint16_t old_y_end = (*dev).params->rgb_channels;

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
    int pot_highscore = 0;

    int fd = open("/nvm0/highscore_box.txt", O_RDONLY);
    char buffer[40];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

    if (bytesRead > 0) {
        sscanf(buffer, "%d-%d-%d", &highscore_1, &highscore_2, &highscore_3);
    }
    close(fd);

    lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]);

    uint32_t start = ztimer_now(ZTIMER_MSEC);

    while(1){
        int time = countdown - (ztimer_now(ZTIMER_MSEC) - start);
        mineplex_draw(dev, time / 1000 + '0', 5, 5, 2, background);
        mineplex_draw(dev, ',', 16, 5, 2, background);
        draw_score(dev, time % 1000, 16, 5, background);

        if (score >= 9999) score = 9999;

        if (lsm6dsxx_read_acc(devv, acc_val) == LSM6DSXX_OK){
            //printf("Accelerometer x: %i y: %i z: %i\n", acc_val.x, acc_val.y, acc_val.z);

            x_smooth = 0.6 * x_smooth + 0.4 * (*acc_val).x;
            y_smooth = 0.6 * y_smooth + 0.4 * (*acc_val).y;

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
                lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]);
                draw_string(dev, "Pause", max_screen_x / 2 - 27, 12, 2, background);
                draw_string(dev, "Highscore:", max_screen_x / 2 - 55, 26, 2, background);  
                
                draw_string(dev, "1 - ", max_screen_x / 2 - 38, 40, 2, background);
                draw_score(dev, highscore_1, max_screen_x / 2 + 6, 40, background);
                draw_string(dev, "2 - ", max_screen_x / 2 - 38, 54, 2, background);
                draw_score(dev, highscore_2, max_screen_x / 2 + 6, 54, background);
                draw_string(dev, "3 - ", max_screen_x / 2 - 38, 68, 2, background);
                draw_score(dev, highscore_3, max_screen_x / 2 + 6, 68, background);
                puts("Pause");

                bool last_button = true;
                int action = 0;
                int last_action = 0;
                int start2 = ztimer_now(ZTIMER_MSEC);

                while(1) {
                    bool current_button = gpio_read(BTN_PIN);

                    if(last_button == true && current_button == false) {
                        last_action = action;
                        action = (action + 1) % 3;
                    }
                    last_button = current_button;

                    if (action == 1){
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, 3);
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, background);
                    }
                    else if (action == 2){
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, 3);
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, background);
                    }
                    else {
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, background);
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, background);
                    }

                    if (action != last_action) {
                        start2 = ztimer_now(ZTIMER_MSEC);
                        last_action = action;
                    }

                    if (ztimer_now(ZTIMER_MSEC) - start2 >= 1500 && action > 0) {
                        if (action == 1) {
                            lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]); 
                            puts("resume");
                            start = ztimer_now(ZTIMER_MSEC) - start;
                            break;
                        }
                        else return;
                    }
                }
            }

            if (x_start < old_x_start) lcd_fill(dev, x_end, old_x_end, old_y_start, old_y_end, colours[background]);  
            else lcd_fill(dev, old_x_start, x_start, old_y_start, old_y_end, colours[background]);

            if (y_start < old_y_start) lcd_fill(dev, old_x_start, old_x_end, y_end, old_y_end, colours[background]); 
            else lcd_fill(dev, old_x_start, old_x_end, old_y_start, y_start, colours[background]); 

            lcd_fill(dev, x_start, x_end, y_start, y_end, 0xFF00);  
            
            old_x_start = x_start;
            old_x_end = x_end;
            old_y_start = y_start;
            old_y_end = y_end;


            if (x_end <= area_end_x && x_start <= area_end_x && x_end >= area_start_x && x_start >= area_start_x){
                if (y_end <= area_end_y && y_start <= area_end_y && y_end >= area_start_y && y_start >= area_start_y) {
                    in_target = true;
                    score += 15 - time / 1000;
                    printf("Current Score: %d\n", score);
                    lcd_fill(dev, random_num_x, random_num_x + max_screen_x * multiplier, random_num_y, random_num_y + max_screen_y * multiplier, colours[background]);
                    start = ztimer_now(ZTIMER_MSEC);
                }
            }

            if (in_target == true){
                in_target = false;
                if (score % 4 == 0 && multiplier >= 0.21) multiplier -= 0.01;
                if (countdown >= 2500) countdown -= 125;
                int mult = multiplier * 100;

                random_num_x = (random_uint32() % ((100 - mult) * max_screen_x) / 100);
                random_num_y = (random_uint32() % ((100 - mult) * max_screen_y) / 100);

                area_end_x = random_num_x + max_screen_x * multiplier - 2;
                area_end_y = random_num_y + max_screen_y * multiplier - 2;
                area_start_x = random_num_x + 2;
                area_start_y = random_num_y + 2;
            }

            lcd_fill(dev, area_start_x - 2, area_start_x, area_start_y - 2, area_end_y + 2, 0x000F);
            lcd_fill(dev, area_end_x, area_end_x + 2, area_start_y - 2, area_end_y + 2, 0x000F);
            lcd_fill(dev, area_start_x - 2, area_end_x + 2, area_start_y - 2, area_start_y, 0x000F);
            lcd_fill(dev, area_start_x - 2, area_end_x + 2, area_end_y, area_end_y + 2, 0x000F);
            
            if (ztimer_now(ZTIMER_MSEC) - start >= countdown){
                pot_highscore = score;
                lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, 0x00F0);
                if (score >= 9999) draw_string(dev, "You won!", max_screen_x / 2 - 44, 12, 2, background);
                draw_string(dev, "Game Over", max_screen_x / 2 - 49, 12, 2, background);
                draw_string(dev, "Press button", max_screen_x / 2 - 66, 26, 2, background);
                draw_string(dev, "to play again", max_screen_x / 2 - 71, 40, 2, background);
                if (pot_highscore > highscore_3) draw_string(dev, "New Highscore:", max_screen_x / 2 - 77, 60, 2, background);
                else draw_string(dev, "Your score:", max_screen_x / 2 - 60, 60, 2, background);
                draw_score(dev, score, max_screen_x / 2 - 22, 74, background);
                puts("Game Over - Press Button to play again");
                
                if (pot_highscore > highscore_1) {
                    highscore_3 = highscore_2;
                    highscore_2 = highscore_1;
                    highscore_1 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_2) {
                    highscore_3 = highscore_2;
                    highscore_2 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_3){
                    highscore_3 = pot_highscore;
                    pot_highscore = 0;
                }

                char output_str[40];

                sprintf(output_str, "%d-%d-%d", highscore_1, highscore_2, highscore_3);

                char *input[3] = {"tee", "/nvm0/highscore_box.txt", output_str};
                _tee(3, input);

                while(1) {
                    if(gpio_read(BTN_PIN) == 0){
                        countdown = 10000;
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
            draw_score(dev, score, max_screen_x - 50, 5, background);
        }
    }
}

void sortgame(lcd_t *dev, lsm6dsxx_t *devv, lsm6dsxx_3d_data_t *acc_val, int background){
    uint16_t max_screen_x = (*dev).params->lines;
    uint16_t max_screen_y = (*dev).params->rgb_channels - 1;

    uint16_t x_start;
    uint16_t x_end;
    uint16_t old_x_start = 0;
    uint16_t old_x_end = (*dev).params->lines;

    uint16_t y_start = 0;
    uint16_t y_end = max_screen_x / 10;

    int x_smooth = 0;

    int sort_colours[4] = {0x001F, 0x02F4, 0xFFC0, 0xFD20};

    int highscore_1 = 0;
    int highscore_2 = 0;
    int highscore_3 = 0;
    int pot_highscore = 0;

    int fd = open("/nvm0/highscore_sort.txt", O_RDONLY);
    char buffer[40];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

    if (bytesRead > 0) {
        sscanf(buffer, "%d-%d-%d", &highscore_1, &highscore_2, &highscore_3);
    }
    close(fd);

    int start = ztimer_now(ZTIMER_MSEC);
    unsigned int score = 0;
    unsigned int countdown = 20;

    bool right_color = true;
    int random = random_uint32() % 4;

    lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]);

    while(true){
        if (lsm6dsxx_read_acc(devv, acc_val) == LSM6DSXX_OK){
            x_smooth = 0.6 * x_smooth + 0.4 * (*acc_val).x;

            if (x_smooth > 700) x_smooth = 700;
            if (x_smooth < -700) x_smooth = -700;
            
            x_start = ((x_smooth - 700) * (-max_screen_x)) / 1540;
            x_end = x_start + (max_screen_x / 10);   


            if (x_start < old_x_start) lcd_fill(dev, x_end, old_x_end, y_start, y_end, colours[background]);  
            else lcd_fill(dev, old_x_start, x_start, y_start, y_end, colours[background]);

            lcd_fill(dev, x_start, x_end, y_start, y_end, sort_colours[random]);  
            
            old_x_start = x_start;
            old_x_end = x_end;
            
            if (y_start >= max_screen_y -(max_screen_x / 10 + 3)){
                if ((x_end <= max_screen_x / 4 && random == 0) || (x_start >= max_screen_x / 4 && x_end <= max_screen_x / 2 && random == 1) || (x_start >= max_screen_x / 2 && x_end <= 3 * max_screen_x / 4 && random == 2) || (x_start >= 3 * max_screen_x / 4 && x_end <= max_screen_x + 1 && random == 3)){
                    score += 21 - countdown;
                    lcd_fill(dev, x_start, x_end, y_start, y_end, colours[background]);
                    y_start = 0;
                    y_end = max_screen_x / 10;
                    random = random_uint32() % 4;
                    right_color = true;
                    if (countdown >= 2) countdown--;
                }
                else{
                    right_color = false;
                }
            }

            if (ztimer_now(ZTIMER_MSEC) - start >= countdown){
                y_start += 2; 
                y_end += 2;
                lcd_fill(dev, x_start, x_end, y_start - 2, y_start, colours[background]);
                start = ztimer_now(ZTIMER_MSEC);
            }

            lcd_fill(dev, 0, max_screen_x / 4, max_screen_y - (max_screen_x / 10 + 4),max_screen_y, sort_colours[0]);
            lcd_fill(dev, max_screen_x / 4, max_screen_x / 2, max_screen_y - (max_screen_x / 10 + 4),max_screen_y, sort_colours[1]);
            lcd_fill(dev, max_screen_x / 2, 3 * max_screen_x / 4, max_screen_y - (max_screen_x / 10 + 4),max_screen_y, sort_colours[2]);
            lcd_fill(dev, 3 * max_screen_x / 4, max_screen_x, max_screen_y - (max_screen_x / 10 + 4),max_screen_y, sort_colours[3]);

            draw_score(dev, score, max_screen_x - 50, 5, background);

            if(gpio_read(BTN_PIN) == 0){
                lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]);
                draw_string(dev, "Pause", max_screen_x / 2 - 27, 12, 2, background);
                draw_string(dev, "Highscore:", max_screen_x / 2 - 55, 26, 2, background);  
                
                draw_string(dev, "1 - ", max_screen_x / 2 - 38, 40, 2, background);
                draw_score(dev, highscore_1, max_screen_x / 2 + 6, 40, background);
                draw_string(dev, "2 - ", max_screen_x / 2 - 38, 54, 2, background);
                draw_score(dev, highscore_2, max_screen_x / 2 + 6, 54, background);
                draw_string(dev, "3 - ", max_screen_x / 2 - 38, 68, 2, background);
                draw_score(dev, highscore_3, max_screen_x / 2 + 6, 68, background);
                puts("Pause");

                bool last_button = true;
                int action = 0;
                int last_action = 0;
                int start2 = ztimer_now(ZTIMER_MSEC);

                while(1) {
                    bool current_button = gpio_read(BTN_PIN);

                    if(last_button == true && current_button == false) {
                        last_action = action;
                        action = (action + 1) % 3;
                    }
                    last_button = current_button;

                    if (action == 1){
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, 3);
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, background);
                    }
                    else if (action == 2){
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, 3);
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, background);
                    }
                    else {
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, background);
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, background);
                    }

                    if (action != last_action) {
                        start2 = ztimer_now(ZTIMER_MSEC);
                        last_action = action;
                    }

                    if (ztimer_now(ZTIMER_MSEC) - start2 >= 1500 && action > 0) {
                        if (action == 1) {
                            lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]); 
                            puts("resume");
                            break;
                        }
                        else return;
                    }
                }
            }

            if (!right_color){
                pot_highscore = score;
                lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, 0x00F0);
                if (score >= 9999) draw_string(dev, "You won!", max_screen_x / 2 - 44, 12, 2, background);
                draw_string(dev, "Game Over", max_screen_x / 2 - 49, 12, 2, background);
                draw_string(dev, "Press button", max_screen_x / 2 - 66, 26, 2, background);
                draw_string(dev, "to play again", max_screen_x / 2 - 71, 40, 2, background);
                if (pot_highscore > highscore_3) draw_string(dev, "New Highscore:", max_screen_x / 2 - 77, 60, 2, background);
                else draw_string(dev, "Your score:", max_screen_x / 2 - 60, 60, 2, background);
                draw_score(dev, score, max_screen_x / 2 - 22, 74, background);
                puts("Game Over - Press Button to play again");
                
                if (pot_highscore > highscore_1) {
                    highscore_3 = highscore_2;
                    highscore_2 = highscore_1;
                    highscore_1 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_2) {
                    highscore_3 = highscore_2;
                    highscore_2 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_3){
                    highscore_3 = pot_highscore;
                    pot_highscore = 0;
                }

                char output_str[40];

                sprintf(output_str, "%d-%d-%d", highscore_1, highscore_2, highscore_3);

                char *input[3] = {"tee", "/nvm0/highscore_sort.txt", output_str};
                _tee(3, input);

                while(1) {
                    if(gpio_read(BTN_PIN) == 0){
                        countdown = 20;
                        score = 0;
                        y_start = 0;
                        y_end = max_screen_x / 10;
                        right_color = true;
                        puts("New Game");
                        start = ztimer_now(ZTIMER_MSEC);
                        break;
                    }
                    else continue;
                }
            }
        }
    }
}

void true_false_game(lcd_t *dev, int background){
    lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
    int start = ztimer_now(ZTIMER_MSEC);
    int score = 0;
    int counter = 0;

    int counter_char = 0;
    int counter_line = 1;
    bool drew_question = false;

    bool last_button = true;
    int action = 0;
    int last_action = 0;
    int random = random_uint32() % 20;

    bool question_used[20];
    for (int i = 0; i < 20; i++) question_used[i] = 0;

    while (true){
        if (counter == 10){
            lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->lines - 1, colours[background]);
            draw_string(dev, "End of Game", (*dev).params->lines / 2 - 60, 20, 2, background);
            draw_score(dev, score, (*dev).params->lines / 2 - 38, 40, background);

            while (true){
                bool current_button = gpio_read(BTN_PIN);

                if(last_button == true && current_button == false) {
                    last_action = action;
                    action = (action + 1) % 3;
                }
                last_button = current_button;
                
                current_button = gpio_read(BTN_PIN);

                if (action == 1){
                    draw_string(dev, "again", (*dev).params->lines / 2 - 27, 70, 2, 3);
                    draw_string(dev, "quit", (*dev).params->lines / 2 - 22, 90, 2, background);
                }
                else if (action == 2){
                    draw_string(dev, "again", (*dev).params->lines / 2 - 27, 70, 2, background);
                    draw_string(dev, "quit", (*dev).params->lines / 2 - 22, 90, 2, 3);
                }
                else {
                    draw_string(dev, "again", (*dev).params->lines / 2 - 27, 70, 2, background);
                    draw_string(dev, "quit", (*dev).params->lines / 2 - 22, 90, 2, background);
                }
                
                if (action != last_action) {
                start = ztimer_now(ZTIMER_MSEC);
                last_action = action;
                }

                if (ztimer_now(ZTIMER_MSEC) - start >= 1500 && action > 0) {
                    if (action == 1) {
                        counter = 0;
                        action = 0;
                        score = 0;
                        counter_char = 0;
                        counter_line = 1;
                        drew_question = false;
                        for (int i = 0; i < 20; i++) question_used[i] = 0;
                        lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
                        break;
                    }
                    else if (action == 2) return;
                }
            }
        }

        draw_score(dev, score, (*dev).params->lines - 50, 5, background);
        if (!drew_question){  
            do {
                random = random_uint32() % 20;
            }while (question_used[random]);

            question_used[random] = 1;
            
            for (unsigned int i = 0; questions[random][i] != '\0'; i++){
                if (counter_char == 12 && questions[random][i - 1] == ' ') counter_char++;
                if (!(questions[random][i] == ' ') && counter_char == 12 && questions[random][i + 1] != ' ' && questions[random][i + 1] != '\0' && questions[random][i + 1] != '.'){
                     mineplex_draw(dev, '-', counter_char * 11, counter_line * 15, 2, background);
                     counter_char++;
                }
                if (counter_char == 13){
                    counter_char = 0;
                    counter_line++;
                }
                if (counter_char == 0 && questions[random][i] == ' ') i++;

                mineplex_draw(dev, questions[random][i], counter_char * 11, counter_line * 15, 2, background);
                counter_char++;
            }            
            drew_question = true;
            counter++;
        }

        bool current_button = gpio_read(BTN_PIN);

        if(last_button == true && current_button == false) {
            last_action = action;
            action = (action + 1) % 4;
        }
        last_button = current_button;

        current_button = gpio_read(BTN_PIN);

        if (action == 1){
            draw_string(dev, "true", 0, (*dev).params->rgb_channels - 15, 2, 3);
            draw_string(dev, "quit", 50, (*dev).params->rgb_channels - 15, 2, background);
            draw_string(dev, "false", 100, (*dev).params->rgb_channels - 15, 2, background);
        }
        else if (action == 2){
            draw_string(dev, "true", 0, (*dev).params->rgb_channels - 15, 2, background);
            draw_string(dev, "quit", 50, (*dev).params->rgb_channels - 15, 2, background);
            draw_string(dev, "false", 100, (*dev).params->rgb_channels - 15, 2, 3);
        }
        else if(action == 3){
            draw_string(dev, "true", 0, (*dev).params->rgb_channels - 15, 2, background);
            draw_string(dev, "quit", 50, (*dev).params->rgb_channels - 15, 2, 3);
            draw_string(dev, "false", 100, (*dev).params->rgb_channels - 15, 2, background);
        }
        else{
            draw_string(dev, "true", 0, (*dev).params->rgb_channels - 15, 2, background);
            draw_string(dev, "quit", 50, (*dev).params->rgb_channels - 15, 2, background);
            draw_string(dev, "false", 100, (*dev).params->rgb_channels - 15, 2, background);
        }

        if (action != last_action) {
            start = ztimer_now(ZTIMER_MSEC);
            last_action = action;
        }

        if (ztimer_now(ZTIMER_MSEC) - start >= 1500 && action > 0) {
            if (action == 1 && true_false[random]) score++;
            else if (action == 2 && !true_false[random]) score++;
            else if (action == 3) return; 

            counter_line = 1;
            counter_char = 0;
            action = 0;
            drew_question = false;
            lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
        }
    }
}

void blackjack(lcd_t *dev, int background){
    int cards_available[11];
    int pos_player = 0;
    int pos_opp = 0;
    int cards_player[6];
    int cards_opp[6];
    int sum_opp = 0;
    int sum_player = 0;

    int first_card = 0;

    uint32_t k;
    uint32_t j;
    int action = 0;
    int last_action = 0;
    bool last_button = true;
    bool draw_card = true;
    bool chose_action = true;
    bool not_drawn_opp = true;
    bool not_drawn_player = true;
    bool drew_scores = false;

    int start = ztimer_now(ZTIMER_MSEC);

    for (int i = 0; i < 11; i++){
        cards_available[i] = 1;
    }
    
    lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
    lcd_fill(dev, 0, (*dev).params->lines, ((*dev).params->rgb_channels - 1) / 2 - 2, ((*dev).params->rgb_channels - 1) / 2, 0xFFFF);

    while(true){
        bool current_button = gpio_read(BTN_PIN);

        if(last_button == true && current_button == false) {
            last_action = action;
            action = (action + 1) % 4;
        }
        last_button = current_button;

        if (sum_opp > 21 || sum_player > 21 || (sum_opp >= 17 && draw_card == false && chose_action == true)){
            printf("sum opp: %d\nsum player: %d\n", sum_opp, sum_player);
            lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
            if (sum_opp > 21 || (sum_player > sum_opp && sum_player <= 21)) draw_string(dev, "YOU WIN", (*dev).params->lines / 2 - 53, 35, 3, background);
            else if (sum_opp == sum_player) draw_string(dev, "DRAW", (*dev).params->lines / 2 - 30, 35, 3, background);
            else draw_string(dev, "YOU LOSE", (*dev).params->lines / 2 - 59, 35, 3, background);

            for (int i = 0; i < 11; i++){
                cards_available[i] = 1;
            }

            char buffer[60];

            draw_string(dev, "Opp: ", 0, 3, 2, background);
            sprintf(buffer, "%d", sum_opp);
            draw_string(dev, buffer, 45, 3, 2, background);
            draw_string(dev, "You: ", 95, 3, 2, background);
            sprintf(buffer, "%d", sum_player);
            draw_string(dev, buffer, 135, 3, 2, background);

            sum_opp = 0;
            sum_player = 0;
            chose_action = true;
            pos_opp = 0;
            pos_player = 0;
            draw_card = true;
            
            while (true){
                current_button = gpio_read(BTN_PIN);

                if(last_button == true && current_button == false) {
                    last_action = action;
                    action = (action + 1) % 3;
                }
                last_button = current_button;
                

                if (action == 1){
                    draw_string(dev, "new game", (*dev).params->lines / 2 - 44, 70, 2, 3);
                    draw_string(dev, "quit", (*dev).params->lines / 2 - 22, 95, 2, background);
                }
                else if (action == 2){
                    draw_string(dev, "new game", (*dev).params->lines / 2 - 44, 70, 2, background);
                    draw_string(dev, "quit", (*dev).params->lines / 2 - 22, 95, 2, 3);
                }
                else {
                    draw_string(dev, "new game", (*dev).params->lines / 2 - 44, 70, 2, background);
                    draw_string(dev, "quit", (*dev).params->lines / 2 - 22, 95, 2, background);
                }

                if (action != last_action) {
                    start = ztimer_now(ZTIMER_MSEC);
                    last_action = action;
                }
            

                if (ztimer_now(ZTIMER_MSEC) - start >= 1500 && action > 0) {
                    if (action == 1) {
                        lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
                        lcd_fill(dev, 0, (*dev).params->lines, ((*dev).params->rgb_channels - 1) / 2 - 2, ((*dev).params->rgb_channels - 1) / 2, 0xFFFF);
                        action = 0;
                        drew_scores = false;
                        break;
                    }
                    if (action == 2) {
                        action = 0;
                        return;
                    }
                }
            }
        }

        if (sum_opp < 17 && chose_action){
            do {
                k = random_uint32() % 11;
            } while (!cards_available[k]);
            cards_available[k] = 0;
            pos_opp += 1;
            cards_opp[pos_opp - 1] = k + 1;
            sum_opp += k + 1;
            not_drawn_opp = true;
        }
        
        if (draw_card == true){
            do {
                j = random_uint32() % 11;
            } while (!cards_available[j]);
            cards_available[j] = 0;
            pos_player += 1;
            cards_player[pos_player - 1] = j + 1;
            sum_player += j + 1;
            draw_card = false;
            chose_action = false;
            not_drawn_player = true;
        }

        char buffer[15];

        if (pos_opp == 1 && not_drawn_opp){
            lcd_fill(dev, 2 * pos_opp, ((*dev).params->lines / 5 - 6) * pos_opp, 28, ((*dev).params->rgb_channels - 1) / 2 - 10, 0xFFFF);
            lcd_fill(dev, 4 * pos_opp, ((*dev).params->lines / 5 - 8) * pos_opp, 30, ((*dev).params->rgb_channels - 1) / 2 - 12, colours[background]);
            mineplex_draw(dev, '?', 8 * pos_opp, ((*dev).params->rgb_channels - 1) / 2 - 27, 2, background);
            first_card = k + 1;
            not_drawn_opp = false;
        }
        else if (not_drawn_opp){
            lcd_fill(dev, 2 + (pos_opp - 1) * ((*dev).params->lines / 5), ((*dev).params->lines / 5 - 6) + (pos_opp - 1) * ((*dev).params->lines / 5), 28, ((*dev).params->rgb_channels - 1) / 2 - 10, 0xFFFF);
            lcd_fill(dev, 4 + (pos_opp - 1) * ((*dev).params->lines / 5), ((*dev).params->lines / 5 - 8) + (pos_opp - 1) * ((*dev).params->lines / 5), 30, ((*dev).params->rgb_channels - 1) / 2 - 12, colours[background]);
            sprintf(buffer, "%d", cards_opp[pos_opp - 1]);
            if (cards_opp[pos_opp - 1] > 9) draw_string(dev, buffer, 4 + (pos_opp - 1) * ((*dev).params->lines / 5), ((*dev).params->rgb_channels - 1) / 2 - 27, 2, background);
            else draw_string(dev, buffer, 8 + (pos_opp - 1) * ((*dev).params->lines / 5), ((*dev).params->rgb_channels - 1) / 2 - 27, 2, background);
            not_drawn_opp = false;
        }

        if (not_drawn_player){
            lcd_fill(dev, 2 + (pos_player - 1) * ((*dev).params->lines / 5), ((*dev).params->lines / 5 - 6) + (pos_player - 1) * ((*dev).params->lines / 5), 100, (*dev).params->rgb_channels - 1, 0xFFFF);
            lcd_fill(dev, 4 + (pos_player - 1) * ((*dev).params->lines / 5), ((*dev).params->lines / 5 - 8) + (pos_player - 1) * ((*dev).params->lines / 5), 102, (*dev).params->rgb_channels - 3, colours[background]);
            sprintf(buffer, "%d", cards_player[pos_player - 1]);
            if (cards_player[pos_player - 1] > 9) draw_string(dev, buffer, 4 + (pos_player - 1) * ((*dev).params->lines / 5), (*dev).params->rgb_channels - 15, 2, background);
            else draw_string(dev, buffer, 8 + (pos_player - 1) * ((*dev).params->lines / 5), (*dev).params->rgb_channels - 15, 2, background);
            not_drawn_player = false;
        }
        

        if (!drew_scores){
            draw_string(dev, "Opp: ", 0, 3, 2, background);
            sprintf(buffer, "%d", sum_opp - first_card);
            draw_string(dev, buffer, 45, 3, 2, background);
            draw_string(dev, "You: ", 95, 3, 2, background);
            sprintf(buffer, "%d", sum_player);
            draw_string(dev, buffer, 135, 3, 2, background);
            drew_scores = true;
        }
        

        if (action == 1){
            draw_string(dev, "stay", 0, (*dev).params->rgb_channels - 55, 2, 3);
            draw_string(dev, "draw", 110, (*dev).params->rgb_channels - 55, 2, background);
            draw_string(dev, "quit", 55, (*dev).params->rgb_channels - 55, 2, background);
        }
        else if (action == 2){
            draw_string(dev, "stay", 0, (*dev).params->rgb_channels - 55, 2, background);
            draw_string(dev, "draw", 110, (*dev).params->rgb_channels - 55, 2, 3);
            draw_string(dev, "quit", 55, (*dev).params->rgb_channels - 55, 2, background);
        }
        else if (action ==3){
            draw_string(dev, "quit", 55, (*dev).params->rgb_channels - 55, 2, 3);
            draw_string(dev, "stay", 0, (*dev).params->rgb_channels - 55, 2, background);
            draw_string(dev, "draw", 110, (*dev).params->rgb_channels - 55, 2, background);
        }
        else {
            draw_string(dev, "stay", 0, (*dev).params->rgb_channels - 55, 2, background);
            draw_string(dev, "draw", 110, (*dev).params->rgb_channels - 55, 2, background);
            draw_string(dev, "quit", 55, (*dev).params->rgb_channels - 55, 2, background);
        }


        if (action != last_action) {
            start = ztimer_now(ZTIMER_MSEC);
            last_action = action;
        }

        if (ztimer_now(ZTIMER_MSEC) - start >= 1500 && action > 0) {
            if (action == 1) {
                draw_card = false;
                chose_action = true;
            }
            if (action == 2) {
                draw_card = true;
                chose_action = true;
            }
            if (action == 3) return;
            action = 0;
            drew_scores = false;
        }
    }
}

void runner(lcd_t *dev, lsm6dsxx_t *devv, lsm6dsxx_3d_data_t *acc_val, int background){
    lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
    int player_position = 0;

    int highscore_1 = 0;
    int highscore_2 = 0;
    int highscore_3 = 0;

    int max_screen_x = dev->params->lines;
    int max_screen_y = dev->params->rgb_channels;

    uint16_t x_start;
    uint16_t x_end;
    uint16_t old_x_start = 0;
    uint16_t old_x_end = (*dev).params->lines;

    int x_smooth = 0;

    Obstacle obstacles[MAX_OBSTACLES];
    unsigned int spawn_timer = 3500;
    int lane[5] = {0, max_screen_x / 5, 2 * (max_screen_x / 5), 3 * (max_screen_x / 5), 4 * (max_screen_x / 5)};
    int spawn_start = ztimer_now(ZTIMER_MSEC);

    unsigned int move_timer = 200;
    int move_start = ztimer_now(ZTIMER_MSEC);

    for (int i = 0; i < MAX_OBSTACLES; i++) obstacles[i].active = false;
    int score = 0;
    int pot_highscore = 0;
    int start_score = ztimer_now(ZTIMER_MSEC);

    bool dead = false;
    bool begin_game = true;
    int speed = 4;

    int fd = open("/nvm0/highscore_runner.txt", O_RDONLY);
    char buffer[40];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

    if (bytesRead > 0) {
        sscanf(buffer, "%d-%d-%d", &highscore_1, &highscore_2, &highscore_3);
    }
    close(fd);
    
    while(true){
        if (lsm6dsxx_read_acc(devv, acc_val) == LSM6DSXX_OK){
            if (ztimer_now(ZTIMER_MSEC) - spawn_start >= spawn_timer || begin_game){
                begin_game = false;
                if (spawn_timer > 1500) spawn_timer -= 100;
                int random = random_uint32() % 5;
                int index = -1;
                do {
                    index++;
                }while (index < MAX_OBSTACLES && obstacles[index].active == true);
                if (index <= MAX_OBSTACLES){
                    obstacles[index].lane = random;
                    obstacles[index].y_start = 0;
                    obstacles[index].active = true;
                }
                spawn_start = ztimer_now(ZTIMER_MSEC);
            }

            if (ztimer_now(ZTIMER_MSEC) - move_start >= move_timer){
                for (int i = 0; i < MAX_OBSTACLES; i++){
                    if (obstacles[i].active){
                        lcd_fill(dev, lane[obstacles[i].lane], lane[obstacles[i].lane] + max_screen_x / 5, obstacles[i].y_start, obstacles[i].y_start + 20, 0xFFFF);
                        lcd_fill(dev, lane[obstacles[i].lane], lane[obstacles[i].lane] + max_screen_x / 5, obstacles[i].y_start - speed, obstacles[i].y_start, colours[background]);
                        obstacles[i].y_start += speed;
                    }
                    if (obstacles[i].active && obstacles[i].y_start + 20 >= max_screen_y - max_screen_x / 10 - 1 && obstacles[i].y_start <= max_screen_y - 2){
                        if (player_position == obstacles[i].lane + 1) dead = true;
                        else if (player_position == 6 && (obstacles[i].lane == 0 || obstacles[i].lane == 1)) dead = true;
                        else if (player_position == 7 && (obstacles[i].lane == 1 || obstacles[i].lane == 2)) dead = true;
                        else if (player_position == 8 && (obstacles[i].lane == 2 || obstacles[i].lane == 3)) dead = true;
                        else if (player_position == 9 && (obstacles[i].lane == 3 || obstacles[i].lane == 4)) dead = true;
                        else continue;
                    }
                    if (obstacles[i].active && obstacles[i].y_start >= max_screen_y - 1) {
                        lcd_fill(dev, lane[obstacles[i].lane], lane[obstacles[i].lane] + max_screen_x / 5, obstacles[i].y_start - speed, max_screen_y, colours[background]);
                        obstacles[i].active = false;
                    }
                }
                move_start = ztimer_now(ZTIMER_MSEC);
            }

            if (ztimer_now(ZTIMER_MSEC) - start_score >= 100){
                if (score < 9999) score++;
                else dead = true;
                if (score % 250 == 0) speed++;
                start_score = ztimer_now(ZTIMER_MSEC);
            }

            x_smooth = 0.6 * x_smooth + 0.4 * (*acc_val).x;

            if (x_smooth > 700) x_smooth = 700;
            if (x_smooth < -700) x_smooth = -700;
            
            x_start = ((x_smooth - 700) * (-max_screen_x)) / 1540;
            x_end = x_start + (max_screen_x / 10);   


            if (x_start < old_x_start) lcd_fill(dev, x_end, old_x_end, max_screen_y - max_screen_y / 10, max_screen_y, colours[background]);  
            else lcd_fill(dev, old_x_start, x_start, max_screen_y - max_screen_y / 10, max_screen_y, colours[background]);

            lcd_fill(dev, x_start, x_end, max_screen_y - max_screen_y / 10, max_screen_y, 0x00FF);  
            
            old_x_start = x_start;
            old_x_end = x_end;
            
            if (x_end <= lane[2] && x_start >= lane[1]) player_position = 2; //second lane
            else if (x_end <= lane[3] && x_start >= lane[2]) player_position = 3; //third lane
            else if (x_end <= lane[4] && x_start >= lane[3]) player_position = 4; //fourth lane
            else if (x_end <= max_screen_x && x_start >= lane[4]) player_position = 5; //fifth lane
            else if (x_start >= lane[1] - (max_screen_x / 10 + 1) && x_start <= lane[1] + (max_screen_x / 10 - 1)) player_position = 6; //between 1 and 2
            else if (x_start >= lane[2] - (max_screen_x / 10 + 1) && x_start <= lane[2] + (max_screen_x / 10 - 1)) player_position = 7; //between 2 and 3
            else if (x_start >= lane[3] - (max_screen_x / 10 + 1) && x_start <= lane[2] + (max_screen_x / 10 - 1)) player_position = 8; //between 3 and 4
            else if (x_start >= lane[4] - (max_screen_x / 10 + 1) && x_start <= lane[2] + (max_screen_x / 10 - 1)) player_position = 9; //between 2 and 3
            else if (x_end <= lane[1]) player_position = 1; //first lane
            draw_score(dev, score, max_screen_x - 50, 5, background);

            if(gpio_read(BTN_PIN) == 0){
                lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]);
                draw_string(dev, "Pause", max_screen_x / 2 - 27, 12, 2, background);
                draw_string(dev, "Highscore:", max_screen_x / 2 - 55, 26, 2, background);  
                
                draw_string(dev, "1 - ", max_screen_x / 2 - 38, 40, 2, background);
                draw_score(dev, highscore_1, max_screen_x / 2 + 6, 40, background);
                draw_string(dev, "2 - ", max_screen_x / 2 - 38, 54, 2, background);
                draw_score(dev, highscore_2, max_screen_x / 2 + 6, 54, background);
                draw_string(dev, "3 - ", max_screen_x / 2 - 38, 68, 2, background);
                draw_score(dev, highscore_3, max_screen_x / 2 + 6, 68, background);
                puts("Pause");

                bool last_button = true;
                int action = 0;
                int last_action = 0;
                int start2 = ztimer_now(ZTIMER_MSEC);

                while(1) {
                    bool current_button = gpio_read(BTN_PIN);

                    if(last_button == true && current_button == false) {
                        last_action = action;
                        action = (action + 1) % 3;
                    }
                    last_button = current_button;

                    if (action == 1){
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, 3);
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, background);
                    }
                    else if (action == 2){
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, 3);
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, background);
                    }
                    else {
                        draw_string(dev, "Resume", (*dev).params->lines / 2 - 33, 90, 2, background);
                        draw_string(dev, "Quit unsaved", (*dev).params->lines / 2 - 66, 110, 2, background);
                    }

                    if (action != last_action) {
                        start2 = ztimer_now(ZTIMER_MSEC);
                        last_action = action;
                    }

                    if (ztimer_now(ZTIMER_MSEC) - start2 >= 1500 && action > 0) {
                        if (action == 1) {
                            lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, colours[background]); 
                            puts("resume");
                            break;
                        }
                        else return;
                    }
                }
            }

            if (dead){
                pot_highscore = score;
                lcd_fill(dev, 0, max_screen_x, 0, max_screen_y, 0x00F0);
                if (score >= 9999) draw_string(dev, "You won!", max_screen_x / 2 - 44, 12, 2, background);
                draw_string(dev, "Game Over", max_screen_x / 2 - 49, 12, 2, background);
                draw_string(dev, "Press button", max_screen_x / 2 - 66, 26, 2, background);
                draw_string(dev, "to play again", max_screen_x / 2 - 71, 40, 2, background);
                if (pot_highscore > highscore_3) draw_string(dev, "New Highscore:", max_screen_x / 2 - 77, 60, 2, background);
                else draw_string(dev, "Your score:", max_screen_x / 2 - 60, 60, 2, background);
                draw_score(dev, score, max_screen_x / 2 - 22, 74, background);
                puts("Game Over - Press Button to play again");
                
                if (pot_highscore > highscore_1) {
                    highscore_3 = highscore_2;
                    highscore_2 = highscore_1;
                    highscore_1 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_2) {
                    highscore_3 = highscore_2;
                    highscore_2 = pot_highscore;
                    pot_highscore = 0;
                }
                else if (pot_highscore > highscore_3){
                    highscore_3 = pot_highscore;
                    pot_highscore = 0;
                }

                char output_str[40];

                sprintf(output_str, "%d-%d-%d", highscore_1, highscore_2, highscore_3);

                char *input[3] = {"tee", "/nvm0/highscore_runner.txt", output_str};
                _tee(3, input);

                while(1) {
                    if(gpio_read(BTN_PIN) == 0){
                        dead = false;
                        score = 0;
                        spawn_timer = 3500;
                        move_timer = 200;
                        begin_game = true;
                        speed = 4;
                        for (int i = 0; i < MAX_OBSTACLES; i++) obstacles[i].active = false;
                        puts("New Game");
                        start_score = ztimer_now(ZTIMER_MSEC);
                        spawn_start = ztimer_now(ZTIMER_MSEC);
                        move_start = ztimer_now(ZTIMER_MSEC);
                        break;
                    }
                    else continue;
                }
            }
        }
    }
}

int Setting_menu(int background, lcd_t *dev){
    int action = 0;
    int last_action = 0;
    int colour = 0;
    bool last_button = true;
    int start = ztimer_now(ZTIMER_MSEC);

    lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
    draw_string(dev, "Choose your", (*dev).params->lines / 2 - 60, 5, 2, background);
    draw_string(dev, "background", (*dev).params->lines / 2 - 55, 20, 2, background);
    draw_string(dev, "colour", (*dev).params->lines / 2 - 33, 35, 2, background);

    while(true){
        bool current_button = gpio_read(BTN_PIN);

        if(last_button == true && current_button == false) {
            last_action = action;
            action = (action + 1) % 4;
        }
        last_button = current_button;

        if (action == 1){
            draw_string(dev, "Black", (*dev).params->lines / 2 - 27, 60, 2, 3);
            draw_string(dev, "Red", (*dev).params->lines / 2 - 16, 80, 2, background);
            draw_string(dev, "Pink", (*dev).params->lines / 2 - 22, 100, 2, background);
            colour = 0;
        }
        else if (action == 2){
            draw_string(dev, "Red", (*dev).params->lines / 2 - 16, 80, 2, 3);
            draw_string(dev, "Black", (*dev).params->lines / 2 - 27, 60, 2, background);
            draw_string(dev, "Pink", (*dev).params->lines / 2 - 22, 100, 2, background);
            colour = 1;
        }
        else if (action == 3){
            draw_string(dev, "Pink", (*dev).params->lines / 2 - 22, 100, 2, 3);
            draw_string(dev, "Black", (*dev).params->lines / 2 - 27, 60, 2, background);
            draw_string(dev, "Red", (*dev).params->lines / 2 - 16, 80, 2, background);
            colour = 2;
        }
        else {
            draw_string(dev, "Pink", (*dev).params->lines / 2 - 22, 100, 2, background);
            draw_string(dev, "Black", (*dev).params->lines / 2 - 27, 60, 2, background);
            draw_string(dev, "Red", (*dev).params->lines / 2 - 16, 80, 2, background);
        }

        if (action != last_action) {
            start = ztimer_now(ZTIMER_MSEC);
            last_action = action;
        }

        if (ztimer_now(ZTIMER_MSEC) - start >= 1500 && (action == 1 || action == 2 || action == 3)) {
            lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[colour]);
            return colour;
        }
    }
}

void Start_menu(int background, lcd_t *dev, lsm6dsxx_t *devv, lsm6dsxx_3d_data_t *acc_val){
    lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
    draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, background);
    int action = 0;
    int last_action = 0;
    bool last_button = true;
    int start = ztimer_now(ZTIMER_MSEC);

    while(true){
        bool current_button = gpio_read(BTN_PIN);

        if(last_button == true && current_button == false) {
            last_action = action;
            action = (action + 1) % 7;
        }
        last_button = current_button;

        if (action == 1){
            draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, 3);
            draw_string(dev, "Sortgame", (*dev).params->lines / 2 - 44, 25, 2, background);
            draw_string(dev, "Endless runner", (*dev).params->lines / 2 - 77, 45, 2, background);
            draw_string(dev, "True/False", (*dev).params->lines / 2 - 55, 65, 2, background);
            draw_string(dev, "Blackjack", (*dev).params->lines / 2 - 49, 85, 2, background);
            draw_string(dev, "Return", (*dev).params->lines / 2 - 33, 105, 2, background);
        }
        else if (action == 2){
            draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, background);
            draw_string(dev, "Sortgame", (*dev).params->lines / 2 - 44, 25, 2, 3);
            draw_string(dev, "Endless runner", (*dev).params->lines / 2 - 77, 45, 2, background);
            draw_string(dev, "True/False", (*dev).params->lines / 2 - 55, 65, 2, background);
            draw_string(dev, "Blackjack", (*dev).params->lines / 2 - 49, 85, 2, background);
            draw_string(dev, "Return", (*dev).params->lines / 2 - 33, 105, 2, background);
        }
        else if (action == 3){
            draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, background);
            draw_string(dev, "Sortgame", (*dev).params->lines / 2 - 44, 25, 2, background);
            draw_string(dev, "Endless runner", (*dev).params->lines / 2 - 77, 45, 2, 3);
            draw_string(dev, "True/False", (*dev).params->lines / 2 - 55, 65, 2, background);
            draw_string(dev, "Blackjack", (*dev).params->lines / 2 - 49, 85, 2, background);
            draw_string(dev, "Return", (*dev).params->lines / 2 - 33, 105, 2, background);
        }
        else if (action == 4){
            draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, background);
            draw_string(dev, "Sortgame", (*dev).params->lines / 2 - 44, 25, 2, background);
            draw_string(dev, "Endless runner", (*dev).params->lines / 2 - 77, 45, 2, background);
            draw_string(dev, "True/False", (*dev).params->lines / 2 - 55, 65, 2, 3);
            draw_string(dev, "Blackjack", (*dev).params->lines / 2 - 49, 85, 2, background);
            draw_string(dev, "Return", (*dev).params->lines / 2 - 33, 105, 2, background);
        }
        else if (action == 5){
            draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, background);
            draw_string(dev, "Sortgame", (*dev).params->lines / 2 - 44, 25, 2, background);
            draw_string(dev, "Endless runner", (*dev).params->lines / 2 - 77, 45, 2, background);
            draw_string(dev, "True/False", (*dev).params->lines / 2 - 55, 65, 2, background);
            draw_string(dev, "Blackjack", (*dev).params->lines / 2 - 49, 85, 2, 3);
            draw_string(dev, "Return", (*dev).params->lines / 2 - 33, 105, 2, background);
        }
        else if(action == 6){
            draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, background);
            draw_string(dev, "Sortgame", (*dev).params->lines / 2 - 44, 25, 2, background);
            draw_string(dev, "Endless runner", (*dev).params->lines / 2 - 77, 45, 2, background);
            draw_string(dev, "True/False", (*dev).params->lines / 2 - 55, 65, 2, background);
            draw_string(dev, "Blackjack", (*dev).params->lines / 2 - 49, 85, 2, background);
            draw_string(dev, "Return", (*dev).params->lines / 2 - 33, 105, 2, 3);
        }
        else {
            draw_string(dev, "Boxgame", (*dev).params->lines / 2 - 38, 5, 2, background);
            draw_string(dev, "Sortgame", (*dev).params->lines / 2 - 44, 25, 2, background);
            draw_string(dev, "Endless runner", (*dev).params->lines / 2 - 77, 45, 2, background);
            draw_string(dev, "True/False", (*dev).params->lines / 2 - 55, 65, 2, background);
            draw_string(dev, "Blackjack", (*dev).params->lines / 2 - 49, 85, 2, background);
            draw_string(dev, "Return", (*dev).params->lines / 2 - 33, 105, 2, background);
        }

        if (action != last_action) {
            start = ztimer_now(ZTIMER_MSEC);
            last_action = action;
        }

        if (ztimer_now(ZTIMER_MSEC) - start >= 1500 && action > 0) {
            if (action == 1) boxgame(dev, devv, acc_val, background);
            if (action == 2) sortgame(dev, devv, acc_val, background);
            if (action == 3) runner(dev, devv, acc_val, background);
            if (action == 4) true_false_game(dev, background);
            if (action == 5) blackjack(dev, background);
            lcd_fill(dev, 0, (*dev).params->lines, 0, (*dev).params->rgb_channels - 1, colours[background]);
            if (action == 6) return;
            action = 0;
        }
    }
}

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

    lcd_fill(&dev, 0, dev.params->lines, 0, dev.params->rgb_channels - 1, 0x0000);
    int action = 0;
    int last_action = 0;
    bool last_button = true;
    int background_colour = 0;
    int start = ztimer_now(ZTIMER_MSEC);

    while(1){
        draw_string(&dev, "Game", dev.params->lines / 2 - 42, 5, 4, background_colour);
        draw_string(&dev, "Collection", dev.params->lines / 2 - 79, 30, 3, background_colour);

        bool current_button = gpio_read(BTN_PIN);
        if(last_button == true && current_button == false) {
            last_action = action;
            action = (action + 1) % 3;
        }
        last_button = current_button;

        if (action == 1){
            draw_string(&dev, "Start", dev.params->lines / 2 - 27, 60, 2, 3);
            draw_string(&dev, "Settings", dev.params->lines / 2 - 44, 85, 2, background_colour);
        }
        else if (action == 2){
            draw_string(&dev, "Settings", dev.params->lines / 2 - 44, 85, 2, 3);
            draw_string(&dev, "Start", dev.params->lines / 2 - 27, 60, 2, background_colour);
        }
        else {
            draw_string(&dev, "Start", dev.params->lines / 2 - 27, 60, 2, background_colour);
            draw_string(&dev, "Settings", dev.params->lines / 2 - 44, 85, 2, background_colour);
        }

        if (action != last_action) {
            start = ztimer_now(ZTIMER_MSEC);
            last_action = action;
        }

        if (ztimer_now(ZTIMER_MSEC) - start >= 1500){
            if (action == 1) {
                Start_menu(background_colour, &dev, &devv, &acc_val);
                action = 0;
            }
            else if (action == 2) {
                background_colour = Setting_menu(background_colour, &dev);
                action = 0;
            }
            else continue;
        }
    }
    return 0;
}