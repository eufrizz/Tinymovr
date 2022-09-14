
#include <string.h>
#include <src/system/system.h>
#include <src/encoder/hall.h>
#include <src/ssp/ssp_func.h>
#include <src/encoder/ma7xx.h>
#include <src/utils/utils.h>

#define MA330_NPP_REG   0x7
#define MA330_NPP       0b11000000 // Emulate 7 pole pairs
#define MA330_NPP_CMD (MA330_NPP_REG << 8 | MA330_NPP)

static HallConfig config = { 0 };
static HallState state = { 0 };

void hall_init(void)
{
    PAC55XX_GPIOF->MODE.P2 = IO_HIGH_IMPEDENCE_INPUT;
    PAC55XX_GPIOF->MODE.P3 = IO_HIGH_IMPEDENCE_INPUT;
    PAC55XX_GPIOF->MODE.P4 = IO_HIGH_IMPEDENCE_INPUT;

    ssp_init(PRIMARY_ENCODER_SSP_PORT, SSP_MS_MASTER, 0, 0); // Mode 0
    delay_us(16000); // ensure 16ms sensor startup time as per the datasheet
    ma7xx_write_reg(MA330_NPP_REG, MA330_NPP); // set num pole pairs
}

PAC5XXX_RAMFUNC int16_t hall_get_angle(void)
{
    return state.angle;
}

PAC5XXX_RAMFUNC void hall_update_angle(bool check_error)
{
    state.sector = (PAC55XX_GPIOF->IN.w >> 2) & 0b111;
    // Could check sector delta and set an ERROR_ENCODER_READING_UNSTABLE if delta (taking into account wrapping) too large
    state.angle = config.sector_map[state.sector];
}

PAC5XXX_RAMFUNC uint8_t hall_get_sector(void)
{
    return state.sector;
}

void hall_clear_sector_map(void)
{
    (void)memset(config.sector_map, 0, sizeof(config.sector_map));
	config.sector_map_calibrated = false;
}

void hall_set_sector_map_calibrated(void)
{
    config.sector_map_calibrated = true;
}

bool hall_sector_map_is_calibrated(void)
{
    return config.sector_map_calibrated;
}

uint8_t *hall_get_sector_map_ptr(void)
{
    return config.sector_map;
}

HallConfig* hall_get_config(void)
{
    return &config;
}

void hall_restore_config(HallConfig* config_)
{
    config = *config_;
}