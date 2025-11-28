#ifndef __TOUCH_H__
#define __TOUCH_H__

#include "main.h" /* For SPI handles and GPIO definitions from CubeMX */
#include "lcd.h"  /* For LCD properties and drawing functions */

/******************************************************************************************/
/* Touchscreen Driver
 * Supports resistive (XPT2046/ADS7843) and capacitive (GT9147/OTT2001A) controllers.
 * This version is rewritten for the STM32 HAL library.
 */

/* Maximum number of touch points for capacitive screens */
#define OTT_MAX_TOUCH   5

/* Touch status flags */
#define TP_PRES_DOWN    0x80  // Touchscreen is pressed
#define TP_CATH_PRES    0x40  // A press event has been captured

/* Touchscreen controller device structure */
typedef struct
{
    uint8_t (*init)(void);      /* Initializes the touch controller */
    uint8_t (*scan)(uint8_t);   /* Scans the touchscreen. 0: screen coordinates, 1: raw physical coordinates */
    void (*adjust)(void);       /* Performs screen calibration */
    uint16_t x[OTT_MAX_TOUCH];  /* Current X coordinates. For resistive, only x[0] is used. */
    uint16_t y[OTT_MAX_TOUCH];  /* Current Y coordinates. For resistive, only y[0] is used. */
    uint8_t  sta;               /* Pen status
                                 * b7: 1 = pressed / 0 = released
                                 * b6: 1 = a press event was captured
                                 * b5: Reserved
                                 * b4..b0: Number of pressed points for capacitive touch
                                 */
    /* Calibration parameters (for resistive touch) */
    float    xfac;
    float    yfac;
    int16_t  xoff;
    int16_t  yoff;
    /* Touchscreen type and orientation
     * b0: 0 = Portrait, 1 = Landscape
     * b7: 0 = Resistive, 1 = Capacitive
     */
    uint8_t touchtype;
} _m_tp_dev;

extern _m_tp_dev tp_dev; /* Defined in touch.c */

/*
 * NOTE: The following GPIOs must be configured in STM32CubeMX:
 * 1. SPI peripheral (e.g., SPI2) for communication.
 * 2. A GPIO Output for the SPI Chip Select (CS).
 * 3. A GPIO Input for the Pen Interrupt (PEN).
 * The names (e.g., T_CS_GPIO_Port, T_PEN_Pin) are defined in main.h by CubeMX.
 */

/* Resistive Touchscreen Functions */
void     TP_Write_Byte(uint8_t num);
uint16_t TP_Read_AD(uint8_t CMD);
uint16_t TP_Read_XOY(uint8_t xy);
uint8_t  TP_Read_XY(uint16_t *x, uint16_t *y);
uint8_t  TP_Read_XY2(uint16_t *x, uint16_t *y);
void     TP_Drow_Touch_Point(uint16_t x, uint16_t y, uint16_t color);
void     TP_Draw_Big_Point(uint16_t x, uint16_t y, uint16_t color);
void     TP_Adjust(void);
void     TP_Adj_Info_Show(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t fac);

/* Common Functions */
uint8_t TP_Scan(uint8_t tp);
uint8_t TP_Init(void);

/* Test Functions */
void load_draw_hint(void);
void ctp_test(void);
void rtp_test(void);

#endif