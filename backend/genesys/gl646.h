/* sane - Scanner Access Now Easy.

   Copyright (C) 2003-2004 Henning Meier-Geinitz <henning@meier-geinitz.de>
   Copyright (C) 2004-2005 Gerhard Jaeger <gerhard@gjaeger.de>
   Copyright (C) 2004-2013 Stéphane Voltz <stef.dev@free.fr>
   Copyright (C) 2005-2009 Pierre Willenbrock <pierre@pirsoft.dnsalias.org>

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.
*/

#ifndef BACKEND_GENESYS_GL646_H
#define BACKEND_GENESYS_GL646_H

#include "genesys.h"
#include "command_set.h"

namespace genesys {
namespace gl646 {

static void gl646_set_fe(Genesys_Device* dev, const Genesys_Sensor& sensor, uint8_t set, int dpi);

/**
 * sets up the scanner for a scan, registers, gamma tables, shading tables
 * and slope tables, based on the parameter struct.
 * @param dev         device to set up
 * @param regs        registers to set up
 * @param settings    settings of the scan
 * @param split       true if move before scan has to be done
 * @param xcorrection true if scanner's X geometry must be taken into account to
 * 		     compute X, ie add left margins
 * @param ycorrection true if scanner's Y geometry must be taken into account to
 * 		     compute Y, ie add top margins
 */
static void setup_for_scan(Genesys_Device* device,
                           const Genesys_Sensor& sensor,
                           Genesys_Register_Set*regs,
                           Genesys_Settings settings,
                           bool split,
                           bool xcorrection,
                           bool ycorrection,
                           bool reverse);

/**
 * Does a simple move of the given distance by doing a scan at lowest resolution
 * shading correction. Memory for data is allocated in this function
 * and must be freed by caller.
 * @param dev device of the scanner
 * @param distance distance to move in MM
 */
static void simple_move(Genesys_Device* dev, SANE_Int distance);

/**
 * Does a simple scan of the area given by the settings. Scanned data
 * it put in an allocated area which must be freed by the caller.
 * and slope tables, based on the parameter struct. There is no shading
 * correction while gamma correction is active.
 * @param dev      device to set up
 * @param settings settings of the scan
 * @param move     flag to enable scanhead to move
 * @param forward  flag to tell movement direction
 * @param shading  flag to tell if shading correction should be done
 * @param data     pointer that will point to the scanned data
 */
static void simple_scan(Genesys_Device* dev, const Genesys_Sensor& sensor,
                        Genesys_Settings settings, bool move, bool forward,
                        bool shading, std::vector<uint8_t>& data, const char* test_identifier);

/**
 * Send the stop scan command
 * */
static void end_scan_impl(Genesys_Device* dev, Genesys_Register_Set* reg, bool check_stop,
                          bool eject);
/**
 * writes control data to an area behind the last motor table.
 */
static void write_control(Genesys_Device* dev, const Genesys_Sensor& sensor, int resolution);


/**
 * initialize scanner's registers at SANE init time
 */
static void gl646_init_regs (Genesys_Device * dev);

/**
 * master motor settings table entry
 */
typedef struct
{
  /* key */
    MotorId motor_id;
    unsigned dpi;
  unsigned channels;

  /* settings */
    StepType steptype;
    bool fastmod; // fast scanning
    bool fastfed; // fast fed slope tables
  SANE_Int mtrpwm;
  SANE_Int steps1;		/* table 1 informations */
  SANE_Int vstart1;
  SANE_Int vend1;
  SANE_Int steps2;		/* table 2 informations */
  SANE_Int vstart2;
  SANE_Int vend2;
  float g1;
  float g2;
  SANE_Int fwdbwd;		/* forward/backward steps */
} Motor_Master;

/**
 * master motor settings, for a given motor and dpi,
 * it gives steps and speed informations
 */
static Motor_Master motor_master[] = {
    /* HP3670 motor settings */
    {MotorId::HP3670,  75, 3, StepType::FULL, false, true , 1, 200,  3429,  305, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 100, 3, StepType::HALF, false, true , 1, 143,  2905,  187, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 150, 3, StepType::HALF, false, true , 1,  73,  3429,  305, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 300, 3, StepType::HALF, false, true , 1,  11,  1055,  563, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 600, 3, StepType::FULL, false, true , 0,   3, 10687, 5126, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670,1200, 3, StepType::HALF, false, true , 0,   3, 15937, 6375, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670,2400, 3, StepType::HALF, false, true , 0,   3, 15937, 12750,192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670,  75, 1, StepType::FULL, false, true , 1, 200,  3429,  305, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 100, 1, StepType::HALF, false, true , 1, 143,  2905,  187, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 150, 1, StepType::HALF, false, true , 1,  73,  3429,  305, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 300, 1, StepType::HALF, false, true , 1,  11,  1055,  563, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670, 600, 1, StepType::FULL, false, true , 0,   3, 10687, 5126, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670,1200, 1, StepType::HALF, false, true , 0,   3, 15937, 6375, 192, 3399, 337, 0.3f, 0.4f, 192},
    {MotorId::HP3670,2400, 3, StepType::HALF, false, true , 0,   3, 15937, 12750,192, 3399, 337, 0.3f, 0.4f, 192},

    /* HP2400/G2410 motor settings base motor dpi = 600 */
    {MotorId::HP2400,  50, 3, StepType::FULL, false, true , 63, 120, 8736,   601, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 100, 3, StepType::HALF, false, true,  63, 120, 8736,   601, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 150, 3, StepType::HALF, false, true , 63, 67, 15902,   902, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 300, 3, StepType::HALF, false, true , 63, 32, 16703,  2188, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 600, 3, StepType::FULL, false, true , 63,  3, 18761, 18761, 192, 4905,  627, 0.3f, 0.4f, 192},
    {MotorId::HP2400,1200, 3, StepType::HALF, false, true , 63,  3, 43501, 43501, 192, 4905,  627, 0.3f, 0.4f, 192},
    {MotorId::HP2400,  50, 1, StepType::FULL, false, true , 63, 120, 8736,   601, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 100, 1, StepType::HALF, false, true,  63, 120, 8736,   601, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 150, 1, StepType::HALF, false, true , 63, 67, 15902,   902, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 300, 1, StepType::HALF, false, true , 63, 32, 16703,  2188, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400, 600, 1, StepType::FULL, false, true , 63,  3, 18761, 18761, 192, 4905,  337, 0.3f, 0.4f, 192},
    {MotorId::HP2400,1200, 1, StepType::HALF, false, true , 63,  3, 43501, 43501, 192, 4905,  337, 0.3f, 0.4f, 192},

    /* XP 200 motor settings */
    {MotorId::XP200,  75, 3, StepType::HALF, true , false, 0, 4,  6000,  2136, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 100, 3, StepType::HALF, true , false, 0, 4,  6000,  2850, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 200, 3, StepType::HALF, true , false, 0, 4,  6999,  5700, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 250, 3, StepType::HALF, true , false, 0, 4,  6999,  6999, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 300, 3, StepType::HALF, true , false, 0, 4, 13500, 13500, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 600, 3, StepType::HALF, true ,  true, 0, 4, 31998, 31998, 2, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200,  75, 1, StepType::HALF, true , false, 0, 4,  6000,  2000, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 100, 1, StepType::HALF, true , false, 0, 4,  6000,  1300, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 200, 1, StepType::HALF, true ,  true, 0, 4,  6000,  3666, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 300, 1, StepType::HALF, true , false, 0, 4,  6500,  6500, 8, 12000, 1200, 0.3f, 0.5f, 1},
    {MotorId::XP200, 600, 1, StepType::HALF, true ,  true, 0, 4, 24000, 24000, 2, 12000, 1200, 0.3f, 0.5f, 1},

    /* HP scanjet 2300c */
    {MotorId::HP2300,  75, 3, StepType::FULL, false, true , 63, 120,  8139,   560, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300, 150, 3, StepType::HALF, false, true , 63,  67,  7903,   543, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300, 300, 3, StepType::HALF, false, true , 63,   3,  2175,  1087, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300, 600, 3, StepType::HALF, false, true , 63,   3,  8700,  4350, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300,1200, 3, StepType::HALF, false, true , 63,   3, 17400,  8700, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300,  75, 1, StepType::FULL, false, true , 63, 120,  8139,   560, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300, 150, 1, StepType::HALF, false, true , 63,  67,  7903,   543, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300, 300, 1, StepType::HALF, false, true , 63,   3,  2175,  1087, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300, 600, 1, StepType::HALF, false, true , 63,   3,  8700,  4350, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300,1200, 1, StepType::HALF, false, true , 63,   3, 17400,  8700, 120, 4905,  337, 0.3f, 0.4f, 16},
    /* non half ccd settings for 300 dpi
    {MotorId::HP2300, 300, 3, StepType::HALF, false, true , 63,  44,  5386,  2175, 120, 4905,  337, 0.3f, 0.4f, 16},
    {MotorId::HP2300, 300, 1, StepType::HALF, false, true , 63,  44,  5386,  2175, 120, 4905,  337, 0.3f, 0.4f, 16},
    */

    /* MD5345/6471 motor settings */
    /* vfinal=(exposure/(1200/dpi))/step_type */
    {MotorId::MD_5345,    50, 3, StepType::HALF   , false, true , 2, 255,  2500,   250, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,    75, 3, StepType::HALF   , false, true , 2, 255,  2500,   343, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   100, 3, StepType::HALF   , false, true , 2, 255,  2500,   458, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   150, 3, StepType::HALF   , false, true , 2, 255,  2500,   687, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   200, 3, StepType::HALF   , false, true , 2, 255,  2500,   916, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   300, 3, StepType::HALF   , false, true , 2, 255,  2500,  1375, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   400, 3, StepType::HALF   , false, true , 0,  32,  2000,  1833, 255, 2000,  300, 0.3f, 0.4f, 32},
    {MotorId::MD_5345,   500, 3, StepType::HALF   , false, true , 0,  32,  2291,  2291, 255, 2000,  300, 0.3f, 0.4f, 32},
    {MotorId::MD_5345,   600, 3, StepType::HALF   , false, true , 0,  32,  2750,  2750, 255, 2000,  300, 0.3f, 0.4f, 32},
    {MotorId::MD_5345,  1200, 3, StepType::QUARTER, false, true , 0,  16,  2750,  2750, 255, 2000,  300, 0.3f, 0.4f, 146},
    {MotorId::MD_5345,  2400, 3, StepType::QUARTER, false, true , 0,  16,  5500,  5500, 255, 2000,  300, 0.3f, 0.4f, 146},
    {MotorId::MD_5345,    50, 1, StepType::HALF   , false, true , 2, 255,  2500,   250, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,    75, 1, StepType::HALF   , false, true , 2, 255,  2500,   343, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   100, 1, StepType::HALF   , false, true , 2, 255,  2500,   458, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   150, 1, StepType::HALF   , false, true , 2, 255,  2500,   687, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   200, 1, StepType::HALF   , false, true , 2, 255,  2500,   916, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   300, 1, StepType::HALF   , false, true , 2, 255,  2500,  1375, 255, 2000,  300, 0.3f, 0.4f, 64},
    {MotorId::MD_5345,   400, 1, StepType::HALF   , false, true , 0,  32,  2000,  1833, 255, 2000,  300, 0.3f, 0.4f, 32},
    {MotorId::MD_5345,   500, 1, StepType::HALF   , false, true , 0,  32,  2291,  2291, 255, 2000,  300, 0.3f, 0.4f, 32},
    {MotorId::MD_5345,   600, 1, StepType::HALF   , false, true , 0,  32,  2750,  2750, 255, 2000,  300, 0.3f, 0.4f, 32},
    {MotorId::MD_5345,  1200, 1, StepType::QUARTER, false, true , 0,  16,  2750,  2750, 255, 2000,  300, 0.3f, 0.4f, 146},
    {MotorId::MD_5345,  2400, 1, StepType::QUARTER, false, true , 0,  16,  5500,  5500, 255, 2000,  300, 0.3f, 0.4f, 146}, /* 5500 guessed */
};

class CommandSetGl646 : public CommandSet
{
public:
    ~CommandSetGl646() override = default;

    bool needs_home_before_init_regs_for_scan(Genesys_Device* dev) const override;

    void init(Genesys_Device* dev) const override;

    void init_regs_for_warmup(Genesys_Device* dev, const Genesys_Sensor& sensor,
                              Genesys_Register_Set* regs, int* channels,
                              int* total_size) const override;

    void init_regs_for_coarse_calibration(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                          Genesys_Register_Set& regs) const override;

    void init_regs_for_shading(Genesys_Device* dev, const Genesys_Sensor& sensor,
                               Genesys_Register_Set& regs) const override;

    void init_regs_for_scan(Genesys_Device* dev, const Genesys_Sensor& sensor) const override;

    void init_regs_for_scan_session(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                    Genesys_Register_Set* reg,
                                    const ScanSession& session) const override;

    void set_fe(Genesys_Device* dev, const Genesys_Sensor& sensor, uint8_t set) const override;
    void set_powersaving(Genesys_Device* dev, int delay) const override;
    void save_power(Genesys_Device* dev, bool enable) const override;

    void begin_scan(Genesys_Device* dev, const Genesys_Sensor& sensor,
                    Genesys_Register_Set* regs, bool start_motor) const override;

    void end_scan(Genesys_Device* dev, Genesys_Register_Set* regs, bool check_stop) const override;

    void send_gamma_table(Genesys_Device* dev, const Genesys_Sensor& sensor) const override;

    void search_start_position(Genesys_Device* dev) const override;

    void offset_calibration(Genesys_Device* dev, const Genesys_Sensor& sensor,
                            Genesys_Register_Set& regs) const override;

    void coarse_gain_calibration(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                 Genesys_Register_Set& regs, int dpi) const override;

    SensorExposure led_calibration(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                   Genesys_Register_Set& regs) const override;

    void wait_for_motor_stop(Genesys_Device* dev) const override;
    void slow_back_home(Genesys_Device* dev, bool wait_until_home) const override;
    void slow_back_home_ta(Genesys_Device& dev) const override;
    void rewind(Genesys_Device* dev) const override;

    bool has_rewind() const override { return false; }

    void update_hardware_sensors(struct Genesys_Scanner* s) const override;

    void load_document(Genesys_Device* dev) const override;

    void detect_document_end(Genesys_Device* dev) const override;

    void eject_document(Genesys_Device* dev) const override;

    void search_strip(Genesys_Device* dev, const Genesys_Sensor& sensor,
                      bool forward, bool black) const override;

    void move_to_ta(Genesys_Device* dev) const override;

    void send_shading_data(Genesys_Device* dev, const Genesys_Sensor& sensor, uint8_t* data,
                           int size) const override;

    bool has_send_shading_data() const override
    {
        return false;
    }

    ScanSession calculate_scan_session(const Genesys_Device* dev,
                                       const Genesys_Sensor& sensor,
                                       const Genesys_Settings& settings) const override;

    void asic_boot(Genesys_Device* dev, bool cold) const override;
};

} // namespace gl646
} // namespace genesys

#endif // BACKEND_GENESYS_GL646_H