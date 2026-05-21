#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define MPU6050_ADDR_LOW  0x68  
#define MPU6050_ADDR_HIGH 0x69  


#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_TEMP_OUT_H   0x41
#define MPU6050_REG_GYRO_XOUT_H  0x43
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_WHO_AM_I     0x75


typedef enum {
    MPU6050_ACCEL_2G  = 0,
    MPU6050_ACCEL_4G  = 1,
    MPU6050_ACCEL_8G  = 2,
    MPU6050_ACCEL_16G = 3,
} mpu6050_accel_range_t;

typedef enum {
    MPU6050_GYRO_250DPS  = 0,
    MPU6050_GYRO_500DPS  = 1,
    MPU6050_GYRO_1000DPS = 2,
    MPU6050_GYRO_2000DPS = 3,
} mpu6050_gyro_range_t;

typedef struct {
    float ax, ay, az;  
    float gx, gy, gz;  
    float temp_c;       
} mpu6050_data_t;

typedef struct {
    float ax_bias, ay_bias, az_bias;
    float gx_bias, gy_bias, gz_bias;
    bool  calibrated;
} mpu6050_cal_t;


bool mpu6050_init(i2c_inst_t* bus, uint8_t addr);


bool mpu6050_read(i2c_inst_t* bus, uint8_t addr,
                  mpu6050_accel_range_t ar, mpu6050_gyro_range_t gr,
                  mpu6050_data_t* out);


bool mpu6050_calibrate(i2c_inst_t* bus, uint8_t addr,
                       mpu6050_accel_range_t ar, mpu6050_gyro_range_t gr,
                       int samples, mpu6050_cal_t* cal);


void mpu6050_apply_cal(mpu6050_data_t* d, const mpu6050_cal_t* cal);


void mpu6050_attitude(const mpu6050_data_t* d, float* roll, float* pitch);


uint8_t mpu6050_whoami(i2c_inst_t* bus, uint8_t addr);