# Embedded_Seminar_RIOTOS
This repository includes all exercises as well as an individual project from a previously visited seminar. The code is written in C. Besides the classical "hello world" example, there are 3 additional tasks and a bigger project:
  - a spirit level
  - boxgame v1
  - boxgame v2
  - project: game collection
=> the code was written and used on the Seeedstudio XIAO nRF52840 Sense, with a ST LSM6DS3TR-C IMU (for accelerometer/gyroscope) and a 128x160px LCD Display

=> a docker container was used for building, flashing was done outside of the container with: BUILD_IN_DOCKER=1 BOARD=xiaobella-unix make flash term
