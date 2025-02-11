// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "rom/ets_sys.h"
#include "rom/gpio.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include "soc/spi_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/periph_ctrl.h"
#include "spi_lcd.h"
#include "psxcontroller.h"
#include "driver/ledc.h"
#include "../../../menu/src/pretty_effect.h"
#include "../nes/nes.h"

#define PIN_NUM_MISO CONFIG_HW_LCD_MISO_GPIO
#define PIN_NUM_MOSI CONFIG_HW_LCD_MOSI_GPIO
#define PIN_NUM_CLK CONFIG_HW_LCD_CLK_GPIO
#define PIN_NUM_CS CONFIG_HW_LCD_CS_GPIO
#define PIN_NUM_DC CONFIG_HW_LCD_DC_GPIO
#define PIN_NUM_RST CONFIG_HW_LCD_RESET_GPIO
#define PIN_NUM_BCKL CONFIG_HW_LCD_BL_GPIO
#define LCD_SEL_CMD() GPIO.out_w1tc = (1 << PIN_NUM_DC)  // Low to send command
#define LCD_SEL_DATA() GPIO.out_w1ts = (1 << PIN_NUM_DC) // High to send data
#define LCD_RST_SET() GPIO.out_w1ts = (1 << PIN_NUM_RST)
#define LCD_RST_CLR() GPIO.out_w1tc = (1 << PIN_NUM_RST)

#define LEDC_LS_TIMER LEDC_TIMER_1
#define LEDC_LS_MODE LEDC_LOW_SPEED_MODE
#define LEDC_LS_CH3_CHANNEL LEDC_CHANNEL_3

#if PIN_NUM_BCKL >= 0
#if CONFIG_HW_INV_BL
#define LCD_BKG_ON() GPIO.out_w1tc = (1 << PIN_NUM_BCKL)  // Backlight ON
#define LCD_BKG_OFF() GPIO.out_w1ts = (1 << PIN_NUM_BCKL) //Backlight OFF
#else
#define LCD_BKG_ON() GPIO.out_w1ts = (1 << PIN_NUM_BCKL)  // Backlight ON
#define LCD_BKG_OFF() GPIO.out_w1tc = (1 << PIN_NUM_BCKL) //Backlight OFF
#endif
#endif

#define SPI_NUM 0x3

#define LCD_TYPE_ILI 0
#define LCD_TYPE_ST 1

#define waitForSPIReady() while (READ_PERI_REG(SPI_CMD_REG(SPI_NUM)) & SPI_USR);

ledc_channel_config_t ledc_channel;

#if PIN_NUM_BCKL >= 0
void initBCKL()
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 500,
        .speed_mode = LEDC_LS_MODE,
        .timer_num = LEDC_LS_TIMER};

    ledc_timer_config(&ledc_timer);

    ledc_channel.channel = LEDC_LS_CH3_CHANNEL;
    ledc_channel.duty = 500;
    ledc_channel.gpio_num = PIN_NUM_BCKL;
    ledc_channel.speed_mode = LEDC_LS_MODE;
    ledc_channel.timer_sel = LEDC_LS_TIMER;

    ledc_channel_config(&ledc_channel);
}
#endif

void setBrightness(int bright)
{
    /*int duty=1000;
	if(bright == -2)duty=0;
	if(bright == 0)duty=1000;
	if(bright == 1)duty=2050;
	if(bright == 2)duty=4100;
	if(bright == 3)duty=6150;
	if(bright == 4)duty=8190;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);*/
    setBright(bright);
}

static void spi_write_byte(const uint8_t data)
{
    SET_PERI_REG_BITS(SPI_MOSI_DLEN_REG(SPI_NUM), SPI_USR_MOSI_DBITLEN, 0x7, SPI_USR_MOSI_DBITLEN_S);
    WRITE_PERI_REG((SPI_W0_REG(SPI_NUM)), data);
    SET_PERI_REG_MASK(SPI_CMD_REG(SPI_NUM), SPI_USR);
    waitForSPIReady();
}

static void LCD_WriteCommand(const uint8_t cmd)
{
    LCD_SEL_CMD();
    spi_write_byte(cmd);
}

static void LCD_WriteData(const uint8_t data)
{
    LCD_SEL_DATA();
    spi_write_byte(data);
}

static void ILI9341_INITIAL()
{
#if PIN_NUM_BCKL >= 0
    LCD_BKG_ON();
#endif
    //------------------------------------Reset Sequence-----------------------------------------//

    LCD_RST_SET();
    ets_delay_us(100000);

    LCD_RST_CLR();
    ets_delay_us(200000);

    LCD_RST_SET();
    ets_delay_us(200000);

#if (CONFIG_HW_LCD_TYPE == LCD_TYPE_ILI)
    //************* Start Initial Sequence **********//
    /* Power contorl B, power control = 0, DC_ENA = 1 */
    LCD_WriteCommand(0xCF);
    LCD_WriteData(0x00);
    LCD_WriteData(0x83);
    LCD_WriteData(0X30);

    /* Power on sequence control,
     * cp1 keeps 1 frame, 1st frame enable
     * vcl = 0, ddvdh=3, vgh=1, vgl=2
     * DDVDH_ENH=1
     */
    LCD_WriteCommand(0xED);
    LCD_WriteData(0x64);
    LCD_WriteData(0x03);
    LCD_WriteData(0X12);
    LCD_WriteData(0X81);

    /* Driver timing control A,
     * non-overlap=default +1
     * EQ=default - 1, CR=default
     * pre-charge=default - 1
     */
    LCD_WriteCommand(0xE8);
    LCD_WriteData(0x85);
    LCD_WriteData(0x01); //i
    LCD_WriteData(0x79); //i

    /* Power control A, Vcore=1.6V, DDVDH=5.6V */
    LCD_WriteCommand(0xCB);
    LCD_WriteData(0x39);
    LCD_WriteData(0x2C);
    LCD_WriteData(0x00);
    LCD_WriteData(0x34);
    LCD_WriteData(0x02);

    /* Pump ratio control, DDVDH=2xVCl */
    LCD_WriteCommand(0xF7);
    LCD_WriteData(0x20);

    /* Driver timing control, all=0 unit */
    LCD_WriteCommand(0xEA);
    LCD_WriteData(0x00);
    LCD_WriteData(0x00);

    /* Power control 1, GVDD=4.75V */
    LCD_WriteCommand(0xC0); //Power control
    LCD_WriteData(0x26);    //i  //VRH[5:0]

    /* Power control 2, DDVDH=VCl*2, VGH=VCl*7, VGL=-VCl*3 */
    LCD_WriteCommand(0xC1); //Power control
    LCD_WriteData(0x11);    //i //SAP[2:0];BT[3:0]

    /* VCOM control 1, VCOMH=4.025V, VCOML=-0.950V */
    LCD_WriteCommand(0xC5); //VCM control
    LCD_WriteData(0x35);    //i
    LCD_WriteData(0x3E);    //i

    /* VCOM control 2, VCOMH=VMH-2, VCOML=VML-2 */
    LCD_WriteCommand(0xC7); //VCM control2
    LCD_WriteData(0xBE);    //i   //»òÕß B1h

    /* Memory access contorl, MX=MY=0, MV=1, ML=0, BGR=1, MH=0 */
    LCD_WriteCommand(0x36); // Memory Access Control
    LCD_WriteData(0x28);    //i //was 0x48

    /* Pixel format, 16bits/pixel for RGB/MCU interface */
    LCD_WriteCommand(0x3A);
    LCD_WriteData(0x55);

    /* Frame rate control, f=fosc, 70Hz fps */
    LCD_WriteCommand(0xB1);
    LCD_WriteData(0x00);
    LCD_WriteData(0x1B); //18

    /* Enable 3G, disabled */
    LCD_WriteCommand(0xF2); // 3Gamma Function Disable
    LCD_WriteData(0x08);

    /* Gamma set, curve 1 */
    LCD_WriteCommand(0x26); //Gamma curve selected
    LCD_WriteData(0x01);

    /* Positive gamma correction */
    LCD_WriteCommand(0xE0); //Set Gamma
    LCD_WriteData(0x1F);
    LCD_WriteData(0x1A);
    LCD_WriteData(0x18);
    LCD_WriteData(0x0A);
    LCD_WriteData(0x0F);
    LCD_WriteData(0x06);
    LCD_WriteData(0x45);
    LCD_WriteData(0X87);
    LCD_WriteData(0x32);
    LCD_WriteData(0x0A);
    LCD_WriteData(0x07);
    LCD_WriteData(0x02);
    LCD_WriteData(0x07);
    LCD_WriteData(0x05);
    LCD_WriteData(0x00);

    /* Negative gamma correction */
    LCD_WriteCommand(0XE1); //Set Gamma
    LCD_WriteData(0x00);
    LCD_WriteData(0x25);
    LCD_WriteData(0x27);
    LCD_WriteData(0x05);
    LCD_WriteData(0x10);
    LCD_WriteData(0x09);
    LCD_WriteData(0x3A);
    LCD_WriteData(0x78);
    LCD_WriteData(0x4D);
    LCD_WriteData(0x05);
    LCD_WriteData(0x18);
    LCD_WriteData(0x0D);
    LCD_WriteData(0x38);
    LCD_WriteData(0x3A);
    LCD_WriteData(0x1F);

    /* Column address set, SC=0, EC=0xEF */
    LCD_WriteCommand(0x2A);
    LCD_WriteData(0x00);
    LCD_WriteData(0x00);
    LCD_WriteData(0x00);
    LCD_WriteData(0xEF);

    /* Page address set, SP=0, EP=0x013F */
    LCD_WriteCommand(0x2B);
    LCD_WriteData(0x00);
    LCD_WriteData(0x00);
    LCD_WriteData(0x01);
    LCD_WriteData(0x3f);
    LCD_WriteCommand(0x2C);

    /* Entry mode set, Low vol detect disabled, normal display */
    LCD_WriteCommand(0xB7);
    LCD_WriteData(0x07);

    /* Display function control */
    LCD_WriteCommand(0xB6); // Display Function Control
    LCD_WriteData(0x0A);    //8 82 27
    LCD_WriteData(0x82);
    LCD_WriteData(0x27);
    LCD_WriteData(0x00);

    //LCD_WriteCommand(0xF6); //not there
    //LCD_WriteData(0x01);
    //LCD_WriteData(0x30);

#endif
#if (CONFIG_HW_LCD_TYPE == LCD_TYPE_ST)

    //212
    //122
    LCD_WriteCommand(0x36);
    LCD_WriteData((1 << 5) | (1 << 6)); //MV 1, MX 1

    LCD_WriteCommand(0x3A);
    LCD_WriteData(0x55);

    LCD_WriteCommand(0xB2);
    LCD_WriteData(0x0c);
    LCD_WriteData(0x0c);
    LCD_WriteData(0x00);
    LCD_WriteData(0x33);
    LCD_WriteData(0x33);

    LCD_WriteCommand(0xB7);
    LCD_WriteData(0x35);

    LCD_WriteCommand(0xBB);
    LCD_WriteData(0x2B);

    LCD_WriteCommand(0xC0);
    LCD_WriteData(0x2C);

    LCD_WriteCommand(0xC2);
    LCD_WriteData(0x01);
    LCD_WriteData(0xFF);

    LCD_WriteCommand(0xC3);
    LCD_WriteData(0x11);

    LCD_WriteCommand(0xC4);
    LCD_WriteData(0x20);

    LCD_WriteCommand(0xC6);
    LCD_WriteData(0x0f);

    LCD_WriteCommand(0xD0);
    LCD_WriteData(0xA4);
    LCD_WriteData(0xA1);

    LCD_WriteCommand(0xE0);
    LCD_WriteData(0xD0);
    LCD_WriteData(0x00);
    LCD_WriteData(0x05);
    LCD_WriteData(0x0E);
    LCD_WriteData(0x15);
    LCD_WriteData(0x0D);
    LCD_WriteData(0x37);
    LCD_WriteData(0x43);
    LCD_WriteData(0x47);
    LCD_WriteData(0x09);
    LCD_WriteData(0x15);
    LCD_WriteData(0x12);
    LCD_WriteData(0x16);
    LCD_WriteData(0x19);

    LCD_WriteCommand(0xE1);
    LCD_WriteData(0xD0);
    LCD_WriteData(0x00);
    LCD_WriteData(0x05);
    LCD_WriteData(0x0D);
    LCD_WriteData(0x0C);
    LCD_WriteData(0x06);
    LCD_WriteData(0x2D);
    LCD_WriteData(0x44);
    LCD_WriteData(0x40);
    LCD_WriteData(0x0E);
    LCD_WriteData(0x1C);
    LCD_WriteData(0x18);
    LCD_WriteData(0x16);
    LCD_WriteData(0x19);

#endif

    /* Sleep out */
    LCD_WriteCommand(0x11); //Exit Sleep
    ets_delay_us(100000);
    /* Display on */
    LCD_WriteCommand(0x29); //Display on
    ets_delay_us(100000);
}
//.............LCD API END----------

static void ili_gpio_init()
{
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_NUM_DC], 2);  //DC PIN
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_NUM_RST], 2); //RESET PIN
#if PIN_NUM_BCKL >= 0
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_NUM_BCKL], 2);  //BKL PIN
    WRITE_PERI_REG(GPIO_ENABLE_W1TS_REG, 1 << PIN_NUM_DC | 1 << PIN_NUM_RST | 1 << PIN_NUM_BCKL);
#else
    WRITE_PERI_REG(GPIO_ENABLE_W1TS_REG, 1 << PIN_NUM_DC | 1 << PIN_NUM_RST);
#endif
}

static void spi_master_init()
{
    periph_module_enable(PERIPH_VSPI_MODULE);
    periph_module_enable(PERIPH_SPI_DMA_MODULE);

    ets_printf("lcd spi pin mux init ...\r\n");
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_NUM_CS], 2);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_NUM_CLK], 2);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_NUM_MOSI], 2);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN_NUM_MISO], 2);
    WRITE_PERI_REG(GPIO_ENABLE_W1TS_REG, 1 << PIN_NUM_CS | 1 << PIN_NUM_CLK | 1 << PIN_NUM_MOSI);

    ets_printf("lcd spi signal init\r\n");
    gpio_matrix_in(PIN_NUM_MISO, VSPIQ_IN_IDX, 0);
    gpio_matrix_out(PIN_NUM_MOSI, VSPID_OUT_IDX, 0, 0);
    gpio_matrix_out(PIN_NUM_CLK, VSPICLK_OUT_IDX, 0, 0);
    gpio_matrix_out(PIN_NUM_CS, VSPICS0_OUT_IDX, 0, 0);
    ets_printf("Vspi config\r\n");

    CLEAR_PERI_REG_MASK(SPI_SLAVE_REG(SPI_NUM), SPI_TRANS_DONE << 5);
    SET_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_CS_SETUP);
    CLEAR_PERI_REG_MASK(SPI_PIN_REG(SPI_NUM), SPI_CK_IDLE_EDGE);
    CLEAR_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_CK_OUT_EDGE);
    CLEAR_PERI_REG_MASK(SPI_CTRL_REG(SPI_NUM), SPI_WR_BIT_ORDER);
    CLEAR_PERI_REG_MASK(SPI_CTRL_REG(SPI_NUM), SPI_RD_BIT_ORDER);
    CLEAR_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_DOUTDIN);
    WRITE_PERI_REG(SPI_USER1_REG(SPI_NUM), 0);
    SET_PERI_REG_BITS(SPI_CTRL2_REG(SPI_NUM), SPI_MISO_DELAY_MODE, 0, SPI_MISO_DELAY_MODE_S);
    CLEAR_PERI_REG_MASK(SPI_SLAVE_REG(SPI_NUM), SPI_SLAVE_MODE);

    // WRITE_PERI_REG(SPI_CLOCK_REG(SPI_NUM), (1 << SPI_CLKCNT_N_S) | (1 << SPI_CLKCNT_L_S));//40MHz
    WRITE_PERI_REG(SPI_CLOCK_REG(SPI_NUM), SPI_CLK_EQU_SYSCLK); // 80Mhz

    SET_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_CS_SETUP | SPI_CS_HOLD | SPI_USR_MOSI);
    SET_PERI_REG_MASK(SPI_CTRL2_REG(SPI_NUM), ((0x4 & SPI_MISO_DELAY_NUM) << SPI_MISO_DELAY_NUM_S));
    CLEAR_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_COMMAND);
    SET_PERI_REG_BITS(SPI_USER2_REG(SPI_NUM), SPI_USR_COMMAND_BITLEN, 0, SPI_USR_COMMAND_BITLEN_S);
    CLEAR_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_ADDR);
    SET_PERI_REG_BITS(SPI_USER1_REG(SPI_NUM), SPI_USR_ADDR_BITLEN, 0, SPI_USR_ADDR_BITLEN_S);
    CLEAR_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_MISO);
    SET_PERI_REG_MASK(SPI_USER_REG(SPI_NUM), SPI_USR_MOSI);
    char i;
    for (i = 0; i < 16; ++i)
    {
        WRITE_PERI_REG((SPI_W0_REG(SPI_NUM) + (i << 2)), 0);
    }
}

#define U16x2toU32(m, l) ((((uint32_t)(l >> 8 | (l & 0xFF) << 8)) << 16) | (m >> 8 | (m & 0xFF) << 8))

extern uint16_t myPalette[];

char *menuText[10] = {
    "Brightness 46 0.",
    "Volume     82 9.",
    " .",
    "Turbo  3 $  1 @.",
    " .",
    "These cause lag.",
    "Horiz Scale 1 5.",
    "Vert Scale  3 7.",
    " .",
    "*"};
bool arrow[9][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0},
                    {0, 0, 0, 0, 1, 0, 0, 0, 0},
                    {0, 0, 0, 1, 1, 1, 0, 0, 0},
                    {0, 0, 1, 1, 1, 1, 1, 0, 0},
                    {0, 1, 1, 1, 1, 1, 1, 1, 0},
                    {0, 0, 0, 1, 1, 1, 0, 0, 0},
                    {0, 0, 0, 1, 1, 1, 0, 0, 0},
                    {0, 0, 0, 1, 1, 1, 0, 0, 0},
                    {0, 0, 0, 0, 0, 0, 0, 0, 0}};
bool buttonA[9][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0},
                      {0, 0, 1, 1, 1, 1, 0, 0, 0},
                      {0, 1, 1, 0, 0, 1, 1, 0, 0},
                      {1, 1, 0, 1, 1, 0, 1, 1, 0},
                      {1, 1, 0, 1, 1, 0, 1, 1, 0},
                      {1, 1, 0, 0, 0, 0, 1, 1, 0},
                      {1, 1, 0, 1, 1, 0, 1, 1, 0},
                      {0, 1, 0, 1, 1, 0, 1, 0, 0},
                      {0, 0, 1, 1, 1, 1, 0, 0, 0}};
bool buttonB[9][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0},
                      {0, 0, 1, 1, 1, 1, 0, 0, 0},
                      {0, 1, 0, 0, 0, 1, 1, 0, 0},
                      {1, 1, 0, 1, 1, 0, 1, 1, 0},
                      {1, 1, 0, 0, 0, 1, 1, 1, 0},
                      {1, 1, 0, 1, 1, 0, 1, 1, 0},
                      {1, 1, 0, 1, 1, 0, 1, 1, 0},
                      {0, 1, 0, 0, 0, 1, 1, 0, 0},
                      {0, 0, 1, 1, 1, 1, 0, 0, 0}};
bool disabled[9][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0},
                       {0, 0, 1, 1, 1, 1, 0, 0, 0},
                       {0, 1, 0, 0, 0, 0, 1, 0, 0},
                       {1, 0, 0, 0, 0, 1, 0, 1, 0},
                       {1, 0, 0, 0, 1, 0, 0, 1, 0},
                       {1, 0, 0, 1, 0, 0, 0, 1, 0},
                       {1, 0, 1, 0, 0, 0, 0, 1, 0},
                       {0, 1, 0, 0, 0, 0, 1, 0, 0},
                       {0, 0, 1, 1, 1, 1, 0, 0, 0}};
bool enabled[9][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 1},
                      {0, 0, 0, 0, 0, 0, 0, 1, 1},
                      {0, 0, 0, 0, 0, 0, 1, 1, 0},
                      {0, 0, 0, 0, 0, 1, 1, 0, 0},
                      {1, 0, 0, 0, 1, 1, 0, 0, 0},
                      {1, 1, 0, 1, 1, 0, 0, 0, 0},
                      {0, 1, 1, 1, 0, 0, 0, 0, 0},
                      {0, 0, 1, 0, 0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0, 0, 0, 0, 0}};
bool scale[9][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0},
                    {0, 0, 0, 0, 0, 0, 1, 1, 0},
                    {0, 0, 0, 0, 0, 0, 1, 1, 0},
                    {0, 0, 0, 0, 1, 1, 1, 1, 0},
                    {0, 0, 0, 0, 1, 1, 1, 1, 0},
                    {0, 0, 1, 1, 1, 1, 1, 1, 0},
                    {0, 0, 1, 1, 1, 1, 1, 1, 0},
                    {1, 1, 1, 1, 1, 1, 1, 1, 0},
                    {1, 1, 1, 1, 1, 1, 1, 1, 0}};
static bool lineEnd;
static bool textEnd;
#define BRIGHTNESS '0'
#define A_BUTTON '1'
#define DOWN_ARROW '2'
#define B_BUTTON '3'
#define LEFT_ARROW '4'
#define HORIZ_SCALE '5'
#define RIGHT_ARROW '6'
#define VERT_SCALE '7'
#define UP_ARROW '8'
#define VOL_METER '9'
#define TURBO_A '@'
#define TURBO_B '$'
#define EOL_MARKER '.'
#define EOF_MARKER '*'

#define spiWrite(register, val)                                                                            \
    SET_PERI_REG_BITS(SPI_MOSI_DLEN_REG(SPI_NUM), SPI_USR_MOSI_DBITLEN, register, SPI_USR_MOSI_DBITLEN_S); \
    WRITE_PERI_REG((SPI_W0_REG(SPI_NUM)), val);                                                            \
    SET_PERI_REG_MASK(SPI_CMD_REG(SPI_NUM), SPI_USR);                                                      \
    waitForSPIReady();

static uint16_t renderInGameMenu(int x, int y, uint16_t x1, uint16_t y1, bool xStr, bool yStr)
{
    if (x < 32 || x > 286 || y < 34 || y > 206)
        return x1;
    else if (x < 40 || x > 280 || y < 38 || y > 202)
        return 0x0F;
    char actChar = ' ';
    if (y == 38)
        textEnd = 0;
    if (x == 40)
        lineEnd = 0;
    int line = (y - 38) / 18;
    int charNo = (x - 40) / 16;
    int xx = ((x - 40) % 16) / 2;
    int yy = ((y - 38) % 18) / 2;

    actChar = menuText[line][charNo];
    if (actChar == EOL_MARKER)
        lineEnd = 1;
    if (actChar == EOF_MARKER)
        textEnd = 1;
    if (lineEnd || textEnd)
        return 0x0F;
    //printf("char %c, x = %d, y = %d{\n",actChar,x,y);
    //color c = [b](0to31)*1 + [g](0to31)*31 + [r] (0to31)*1024 +0x8000 --> x1=y1=c; !?
    switch (actChar)
    {
    case DOWN_ARROW:
        if (arrow[8 - yy][xx])
            return 0xDDDD;
        break;
    case LEFT_ARROW:
        if (arrow[xx][yy])
            return 0xDDDD;
        break;
    case RIGHT_ARROW:
        if (arrow[8 - xx][yy])
            return 0xDDDD;
        break;
    case UP_ARROW:
        if (arrow[yy][xx])
            return 0xDDDD;
        break;
    case A_BUTTON:
        if (buttonA[yy][xx])
            return 0xDDDD;
        break;
    case B_BUTTON:
        if (buttonB[yy][xx])
            return 0xDDDD;
        break;
    case HORIZ_SCALE:
        if (xStr && enabled[yy][xx])
            return 63 << 5;
        else if (!xStr && disabled[yy][xx])
            return 31 << 11;
        break;
    case VERT_SCALE:
        if (yStr && enabled[yy][xx])
            return 63 << 5;
        else if (!yStr && disabled[yy][xx])
            return 31 << 11;
        break;
    case BRIGHTNESS:
        if (scale[yy][xx])
        {
            return xx < getBright() * 2 ? 0xFFFF : 0xDDDD;
            setBrightness(getBright());
        }
        break;
    case VOL_METER:
        if (getVolume() == 0 && disabled[yy][xx])
            return 31 << 11;

        if (getVolume() > 0 && scale[yy][xx])
            return (xx) < getVolume() * 2 ? 0xFFFF : 0xDDDD;
        break;
    case TURBO_A:
        if (!getTurboA() && disabled[yy][xx])
            return 31 << 11;
        if (getTurboA() > 0 && scale[yy][xx])
            return (xx) < (getTurboA() * 2 - 1) ? 0xffff : 0xdddd;
        break;
    case TURBO_B:
        if (!getTurboB() && disabled[yy][xx])
            return 31 << 11;
        if (getTurboB() > 0 && scale[yy][xx])
            return (xx) < (getTurboB() * 2 - 1) ? 0xffff : 0xdddd;
        break;
    default:
        if ((actChar < 47 || actChar > 57) && peGetPixel(actChar, (x - 40) % 16, (y - 38) % 18))
            return 0xFFFF;
        break;
    }
    return 0x0F;
}

uint16_t rowCrc[NES_SCREEN_HEIGHT];
uint16_t scaleX[320];
uint16_t scaleY[240];
// This is the number of pixels on left and right that are ignored for dirty checks
// By ignoring the left and right edges, attribute glitches won't cause extra rendering
#define ignore_border 4
static int calcCrc(const uint8_t *row)
{
    int crc = 0;
    int tmp;
    for (int i = ignore_border; i < 256 - ignore_border; i++)
    {
        tmp = ((crc >> 8) ^ row[i]) & 0xff;
        tmp ^= tmp >> 4;
        crc = (crc << 8) ^ (tmp << 12) ^ (tmp << 5) ^ tmp;
        crc &= 0xffff;
    }
    return crc;
}

static int lastShowMenu = 0;
void ili9341_write_frame(const uint16_t xs, const uint16_t ys, const uint16_t width, const uint16_t height, const uint8_t *data[],
                         bool xStr, bool yStr)
{
    int x, y;
    int xx, yy;
    int i;
    uint16_t x1, y1, evenPixel, oddPixel, backgroundColor;
    uint32_t xv, yv, dc;
    uint32_t temp[16];
    if (data == NULL)
        return;

    dc = (1 << PIN_NUM_DC);

    if (getShowMenu() != lastShowMenu)
    {
        memset(rowCrc, 0, sizeof rowCrc);
    }
    lastShowMenu = getShowMenu();

    int lastY = -1;
    int lastYshown = 0;

    // Black background
    backgroundColor = 0;

    for (y = 0; y < height; y++)
    {
        yy = yStr ? scaleY[y] : y;
        if (lastY == yy)
        {
            if (!lastYshown && !getShowMenu())
                continue;
        }
        else
        {
            lastY = yy;
            uint16_t crc = calcCrc(data[yy]);
            if (crc == rowCrc[yy] && !getShowMenu())
            {
                lastYshown = false;
                continue;
            }
            else
            {
                lastYshown = true;
                rowCrc[yy] = crc;
            }
        }

        //start line
        x1 = xs + (width - 1);
        y1 = ys + y + (height - 1);

        xv = U16x2toU32(xs, x1);
        yv = U16x2toU32((ys + y), y1);

        waitForSPIReady();
        // Set column (command 0x2a - col address) to X start
        GPIO.out_w1tc = dc;
        spiWrite(7, 0x2a);
        GPIO.out_w1ts = dc;
        spiWrite(31, xv);
        // Set row (command 0x2B - page address) to Y start
        GPIO.out_w1tc = dc;
        spiWrite(7, 0x2b);
        GPIO.out_w1ts = dc;
        spiWrite(31, yv);
        // Send memory write command
        GPIO.out_w1tc = dc;
        spiWrite(7, 0x2c);

        x = 0;
        GPIO.out_w1ts = dc;
        SET_PERI_REG_BITS(SPI_MOSI_DLEN_REG(SPI_NUM), SPI_USR_MOSI_DBITLEN, 511, SPI_USR_MOSI_DBITLEN_S);
        while (x < width)
        {
            // Render 32 pixels, grouped as pairs of 16-bit pixels stored in 32-bit values
            for (i = 0; i < 16; i++)
            {
                xx = xStr ? scaleX[x] : x;
                if (xx >= 32 && !xStr)
                    xx -= 32;
                evenPixel = myPalette[(unsigned char)(data[yy][xx])];
                x++;
                xx = xStr ? scaleX[x] : x;
                if (xx >= 32 && !xStr)
                    xx -= 32;
                oddPixel = myPalette[(unsigned char)(data[yy][xx])];
                x++;
                if (!xStr && (x <= 32 || x >= 288))
                    evenPixel = oddPixel = backgroundColor;
                if (!yStr && y >= 224)
                    evenPixel = oddPixel = backgroundColor;
                if (getShowMenu())
                {
                    evenPixel = oddPixel = renderInGameMenu(x, y, evenPixel, oddPixel, xStr, yStr);
                }
                temp[i] = U16x2toU32(evenPixel, oddPixel);
            }
            waitForSPIReady();
            for (i = 0; i < 16; i++)
            {
                WRITE_PERI_REG((SPI_W0_REG(SPI_NUM) + (i << 2)), temp[i]);
            }
            SET_PERI_REG_MASK(SPI_CMD_REG(SPI_NUM), SPI_USR);
        }
    }

    if (getShutdown())
        setBrightness(getBright());
#if PIN_NUM_BCKL >= 0
    if (getBright() == -1)
        LCD_BKG_OFF();
#endif
}

void precalculateLookupTables()
{
    for (int i = 0; i < 320; i++)
    {
        scaleX[i] = i * 0.8;
    }
    for (int i = 0; i < 240; i++)
    {
        scaleY[i] = i * 0.94;
    }
}
void ili9341_init()
{
    lineEnd = textEnd = 0;
    spi_master_init();
    ili_gpio_init();
    ILI9341_INITIAL();
#if PIN_NUM_BCKL >= 0
    LCD_BKG_ON();
    initBCKL();
#endif
    memset(rowCrc, 0x1234, sizeof rowCrc);
    precalculateLookupTables();
}
