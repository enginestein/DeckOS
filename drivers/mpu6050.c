#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "mpu6050.h"



static bool reg_write(i2c_inst_t* bus, uint8_t addr,
                      uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_write_timeout_us(bus, addr, buf, 2, false, 3000) == 2;
}

static bool reg_read(i2c_inst_t* bus, uint8_t addr,
                     uint8_t reg, uint8_t* dst, int len) {
    if (i2c_write_timeout_us(bus, addr, &reg, 1, true, 3000) != 1)
        return false;
    return i2c_read_timeout_us(bus, addr, dst, len, false, 5000) == len;
}

static int16_t be16(const uint8_t* p) {
    return (int16_t)((p[0] << 8) | p[1]);
}



uint8_t mpu6050_whoami(i2c_inst_t* bus, uint8_t addr) {
    uint8_t val = 0;
    reg_read(bus, addr, MPU6050_REG_WHO_AM_I, &val, 1);
    return val;
}

bool mpu6050_init(i2c_inst_t* bus, uint8_t addr) {
   
    if (!reg_write(bus, addr, MPU6050_REG_PWR_MGMT_1, 0x00))
        return false;
    sleep_ms(100);
   
    reg_write(bus, addr, MPU6050_REG_SMPLRT_DIV, 0x00);
   
    reg_write(bus, addr, MPU6050_REG_CONFIG, 0x03);
   
    reg_write(bus, addr, MPU6050_REG_ACCEL_CONFIG, 0x00);
    reg_write(bus, addr, MPU6050_REG_GYRO_CONFIG,  0x00);
    return true;
}

bool mpu6050_read(i2c_inst_t* bus, uint8_t addr,
                  mpu6050_accel_range_t ar, mpu6050_gyro_range_t gr,
                  mpu6050_data_t* out) {
    uint8_t raw[14];
    if (!reg_read(bus, addr, MPU6050_REG_ACCEL_XOUT_H, raw, 14))
        return false;

   
    static const float ascale[] = {16384.f, 8192.f, 4096.f, 2048.f};
    static const float gscale[] = {131.f,   65.5f,  32.8f,  16.4f};

    out->ax    = be16(raw + 0)  / ascale[ar];
    out->ay    = be16(raw + 2)  / ascale[ar];
    out->az    = be16(raw + 4)  / ascale[ar];
   
    out->temp_c = be16(raw + 6) / 340.f + 36.53f;
    out->gx    = be16(raw + 8)  / gscale[gr];
    out->gy    = be16(raw + 10) / gscale[gr];
    out->gz    = be16(raw + 12) / gscale[gr];
    return true;
}

void mpu6050_apply_cal(mpu6050_data_t* d, const mpu6050_cal_t* cal) {
    if (!cal->calibrated) return;
    d->ax -= cal->ax_bias;
    d->ay -= cal->ay_bias;
    d->az -= cal->az_bias;
    d->gx -= cal->gx_bias;
    d->gy -= cal->gy_bias;
    d->gz -= cal->gz_bias;
}

bool mpu6050_calibrate(i2c_inst_t* bus, uint8_t addr,
                       mpu6050_accel_range_t ar, mpu6050_gyro_range_t gr,
                       int samples, mpu6050_cal_t* cal) {
    memset(cal, 0, sizeof(*cal));
    double sax=0,say=0,saz=0,sgx=0,sgy=0,sgz=0;
    mpu6050_data_t d;
    for (int i = 0; i < samples; i++) {
        if (!mpu6050_read(bus, addr, ar, gr, &d)) return false;
        sax += d.ax; say += d.ay; saz += d.az;
        sgx += d.gx; sgy += d.gy; sgz += d.gz;
        sleep_ms(5);
    }
    cal->ax_bias = (float)(sax / samples);
    cal->ay_bias = (float)(say / samples);
   
    cal->az_bias = (float)(saz / samples) - 1.0f;
    cal->gx_bias = (float)(sgx / samples);
    cal->gy_bias = (float)(sgy / samples);
    cal->gz_bias = (float)(sgz / samples);
    cal->calibrated = true;
    return true;
}

void mpu6050_attitude(const mpu6050_data_t* d, float* roll, float* pitch) {
   
    *roll  = atan2f(d->ay, d->az) * (180.f / (float)M_PI);
    *pitch = atan2f(-d->ax,
                    sqrtf(d->ay * d->ay + d->az * d->az))
             * (180.f / (float)M_PI);
}