#include "gps.h"
#include <stdio.h>
#include <string.h>
#include <stm32l1xx.h>
#include <FreeRTOS.h>
#include <FreeRTOS_CLI.h>
#include <task.h>
#include <semphr.h>
#include <timers.h>
#include "usart.h"
#include "messages.h"
#include "cir_buf.h"
#include "console.h"
#include "options.h"
#include "spirit1.h"
#include "ogn_lib.h"
#include "display.h"
#include "timer_const.h"

/* -------- constants -------- */
/* http://support.maestro-wireless.com/knowledgebase.php?article=6 */
static const char * const pcResetNMEA    = "$PSRF101,-2686727,-4304282,3851642,75000,95629,1684,12,4*24\r\n";
static const char * const pcShutDownNMEA = "$PSRF117,16*0B\r\n";

/* -------- defines ---------- */
/* - GPS ON_OFF mappings - */
#define GPS_ON_OFF_PIN   GPIO_Pin_4
#define GPS_ON_OFF_PORT  GPIOB
#define GPS_ON_OFF_CLK   RCC_AHBPeriph_GPIOB
/* -------- variables -------- */
/* circular buffer for NMEA messages */
cir_buf_str* nmea_buffer;
xQueueHandle gps_que;
static TimerHandle_t xGPSValidTimer;
static TimerHandle_t xGPSWdgTimer;
static uint8_t GPS_fix_found;
static uint8_t debug_mode;

/* -------- functions -------- */

/**
* @brief  Send new GPS fix status to display task.
* @param  New fix state
* @retval None
*/
void GPS_Send_Disp_Status(display_opcode_types msg_type)
{
    task_message display_msg;
    xQueueHandle* disp_task_queue = Get_DisplayQue();
    if (disp_task_queue)
    {
        /* Start CW */
        display_msg.msg_data   = 0;
        display_msg.msg_len    = 0;
        display_msg.msg_opcode = msg_type;
        display_msg.src_id     = GPS_USART_SRC_ID;
        xQueueSend(*disp_task_queue, &display_msg, portMAX_DELAY);
    }
}

/**
* @brief  GPS fix timeout callback.
* @brief  Callback is called when there is no valid fix for 2 secs.
* @param  Timer handle
* @retval None
*/
void vGPSValidTimerCallback(TimerHandle_t pxTimer)
{
    GPS_fix_found = 0;
    GPS_Send_Disp_Status(DISP_GPS_NO_FIX);
    Console_Send("GPS fix lost.\r\n",0);
}

/**
* @brief  GPS data timeout callback.
* @brief  Callback is called when 
* @brief  there is no data from GPS for OPT_GPS_WDG_TIME seconds.
* @param  Timer handle
* @retval None
*/
void vGPSWdgTimerCallback(TimerHandle_t pxTimer)
{
    Console_Send("!! GPS watchdog reset (use gps_wdg_time 0 to disable) !!\r\n", 1);
    NVIC_SystemReset();
}


/**
* @brief  Function called whenever valid GPS position is found.
* @param  None
* @retval None
*/
void GPS_Valid_Position(void)
{
    xTimerStart(xGPSValidTimer, portMAX_DELAY);
    
    if (!GPS_fix_found)    /* detect only once */
    {
        GPS_Send_Disp_Status(DISP_GPS_FIX);
        Console_Send("GPS fix found.\r\n",0); 
    }
    GPS_fix_found = 1;    
}

/**
* @brief  Functions parses received NMEA string.
* @param  string address, string length
* @retval None
*/

uint32_t GPS_GetPosition(char *Output) { return OGN_GetPosition(Output); }

void Handle_NMEA_String(const char* str, uint8_t len)
{
    OGN_Parse_res_t ret_value;
    static char DebugStr[120];

    ret_value = OGN_Parse_NMEA(str, len);
    if (debug_mode)
    {
        sprintf(DebugStr, "NMEA:%6.6s[%2d] => %d\r\n", str, len, (int)ret_value);
        Console_Send(DebugStr, 0);
    }

    if (ret_value == OGN_PARSE_POS_VALID_CURRENT)
    {
        GPS_Valid_Position();
    }   
    return; 
}

/**
* @brief  GPS Cold Reset.
* @param  None
* @retval None
*/

void GPS_Reset(void)
{
    USART3_Send((uint8_t*)pcResetNMEA, strlen(pcResetNMEA));
}

void GPS_Off(void)
{
    if (!*(uint8_t *)GetOption(OPT_GPS_ALW_ON))
    {
        USART3_Send((uint8_t*)pcShutDownNMEA, strlen(pcShutDownNMEA));
        vTaskDelay(200);
    }
}

void GPS_On(void)
{
    if (!*(uint8_t *)GetOption(OPT_GPS_ALW_ON))
    {
        GPS_Off();
        GPIO_SetBits(GPS_ON_OFF_PORT, GPS_ON_OFF_PIN);
        vTaskDelay(200);
        GPIO_ResetBits(GPS_ON_OFF_PORT, GPS_ON_OFF_PIN);
    }
}

void GPS_Debug_on(void)
{
    debug_mode = 1;
}

/**
* @brief  Configures the GPS Task Peripherals.
* @param  None
* @retval None
*/

void GPS_Config(void)
{
   GPIO_InitTypeDef GPIO_InitStructure;
   
   debug_mode = 0;
   uint32_t* gps_speed = (uint32_t*)GetOption(OPT_GPS_SPEED);
   uint16_t* wdg_time  = (uint16_t*)GetOption(OPT_GPS_WDG_TIME);
   if (gps_speed)
   {
      USART3_Config(*gps_speed);
   } 
   
   /* GPS validity timer */
   xGPSValidTimer = xTimerCreate(
     "GPSTimer",
     /* The timer period in ticks. */
     2500,
     /* The timer will stop when expire. */
     pdFALSE,
     /* unique id */
     ( void * )GPS_VALID_TIMER,
     /* Each timer calls the same callback when it expires. */
     vGPSValidTimerCallback
   );
   
   /* GPS watchdog timer */
   xGPSWdgTimer = xTimerCreate(
     "GPSWsgTimer",
     /* The timer period in ticks. */
     *wdg_time*1000*portTICK_PERIOD_MS,
     /* The timer will stop when expire. */
     pdFALSE,
     /* unique id */
     ( void * )GPS_WDG_TIMER,
     /* Each timer calls the same callback when it expires. */
     vGPSWdgTimerCallback
   );
         
   /* GPS ON/OFF pin configuration */
   RCC_AHBPeriphClockCmd(GPS_ON_OFF_CLK, ENABLE);

   GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
   GPIO_InitStructure.GPIO_Pin   = GPS_ON_OFF_PIN;
   GPIO_Init(GPS_ON_OFF_PORT, &GPIO_InitStructure);
   
   GPIO_ResetBits(GPS_ON_OFF_PORT, GPS_ON_OFF_PIN);
}

/**
* @brief  GPS Task.
* @param  None
* @retval None
*/
void vTaskGPS(void* pvParameters)
{
   task_message msg;
   
   OGN_Init();
   OGN_SetAcftID(*(uint32_t*)GetOption(OPT_ACFT_ID));
 
   /* Allocate data buffer */
   nmea_buffer = init_cir_buf(CIR_BUF_NMEA);
   /* Send cir. buf. handle for USART3 driver & console */
   USART3_SetBuf(nmea_buffer);
   Console_SetNMEABuf(nmea_buffer);

   /* Create queue for GPS task messages received */
   gps_que = xQueueCreate(10, sizeof(task_message));
   /* Inform USART driver about receiving queue */
   USART3_SetQue(&gps_que);
   /* Inform Console about receiving queue */
   Console_SetGPSQue(&gps_que);
   /* Enable USART when everything is set */
   USART_Cmd(USART3, ENABLE);
   /* Wait 700ms for GPS to stabilize after power on */
   vTaskDelay(700);
   /* Perform GPS on sequence */
   GPS_On();
   /* Check GPS fix status */
   GPS_fix_found = 0;
   xTimerStart(xGPSValidTimer, portMAX_DELAY);
   if (xGPSWdgTimer) xTimerStart(xGPSWdgTimer, portMAX_DELAY);
   
   for(;;)
   {
      xQueueReceive(gps_que, &msg, portMAX_DELAY);
      switch (msg.src_id)
      {
         case GPS_USART_SRC_ID:
         {
            /* Received NMEA sentence from real GPS */
            char* nmea_str= (char*)msg.msg_data;
            if (xGPSWdgTimer) xTimerStart(xGPSWdgTimer, portMAX_DELAY);
            if (*(uint8_t*)GetOption(OPT_GPSDUMP))
            {
                /* Send received NMEA sentence to console (with blocking) */
                Console_Send(nmea_str, 1);
            }
            if (msg.msg_len >= 6) 
            {
                /* A2235-H possible bug: observed (very rarely) lack of CRC (*12) at the end of NMEA sentence */
                /* cold reset triggered to get out of the state */
                if ((nmea_str[0] == '$') && (nmea_str[msg.msg_len-5] != '*'))
                {
                    Console_Send("GPS bug detected - GPS cold reset should fix this.\r\nIf not - reconnect battery.\r\n", 1);
                    GPS_Reset();
                }                
            }
            Handle_NMEA_String(nmea_str, msg.msg_len);
            break;
         }
         case CONSOLE_USART_SRC_ID:
         {
            /* Received NMEA sentence from console */
            Handle_NMEA_String((char*)msg.msg_data, msg.msg_len);
            break;
         }
         default:
         {
            break;
         }
      }
   }
}
