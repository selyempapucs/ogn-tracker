/**
  ******************************************************************************
  * @file    Project/STM32L1xx_StdPeriph_Templates/main.c 
  * @author  MCD Application Team
  * @version V1.2.0
  * @date    16-May-2014
  * @brief   Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2014 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "console.h"
#include "gps.h"
#include "options.h"
#include "spirit1.h"
#include "control.h"
#include "display.h"
#include "background.h"

/** @addtogroup Template_Project
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/


/**
  * @brief  Function finishes shut-down sequence, started at PreShutDownSequence() 
  * @param  None
  * @retval None
  */
void ShutdownFinish(void)
{
    /* Reprogram SHDN_REG_NUM to 0 for correct restart next time */
    RTC_WriteBackupRegister(SHDN_REG_NUM, 0);
    /* Activate Power Button in standby mode */
    PWR_WakeUpPinCmd(PWR_WakeUpPin_2, ENABLE);
    /* Go to the lowest possible power mode */
    PWR_UltraLowPowerCmd(ENABLE);
    PWR_EnterSTANDBYMode();
}
/**
  * @brief  Function responsible for detecting power-up mode:
  * @brief  1. Tracker powered up by connecting battery.
  * @brief  2. Tracker powered up by wake-up button.
  * @brief  3. Tracker during transition to shut-down mode.
  * @param  None
  * @retval None
  */
void HandlePowerUpMode(void)
{
    /* Enable RTC and backup registers */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    /* Allow access to RTC backup registers */
    PWR_RTCAccessCmd(ENABLE);
    /* Clear WakeUp flag */
    PWR_ClearFlag(PWR_FLAG_WU);
    
    /* Check for mode 3: tracker during transition to shut-down */
    if (RTC_ReadBackupRegister(SHDN_REG_NUM) == SHDN_MAGIC_NUM)
    {
        ShutdownFinish();
    }

    /* Check if the StandBy flag is set - tracker powered up by wake-up button.*/
    if (PWR_GetFlagStatus(PWR_FLAG_SB) != RESET)
    {
        volatile int delay;
        /* Clear StandBy flag */
        PWR_ClearFlag(PWR_FLAG_SB);
        /* Enable power button */       
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);  
        /* Wait 1 second */
        for (delay=0; delay<1000000; delay++) { delay++; delay--; }
        
        if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13) != Bit_SET)
        {
            /* power button pressed too short - shutdown tracker */
            ShutdownFinish();
        }
        /* Power button still pressed - proceed to wake-up */
        /* Wait for RTC APB registers synchronisation */
        RTC_WaitForSynchro();
    }  
}

void prvSetupHardware(void)
{
   NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
   srand(*(uint32_t*)0x1FF80050); /* Set CPU id as seed */
   
   Background_Config();
   Console_Config();
   Display_Config();
   GPS_Config();
   Spirit1_Config();
   Control_Config();
}

/**
  * @brief  Main program.
  * @param  None
  * @retval None
  */
  
int main(void)
{
  /*!< At this stage the microcontroller clock setting is already configured, 
       this is done through SystemInit() function which is called from startup
       file (startup_stm32l1xx_xx.s) before to branch to application main.
       To reconfigure the default setting of SystemInit() function, refer to
       system_stm32l1xx.c file
     */
   HandlePowerUpMode();
   InitOptions();
   prvSetupHardware();
   
   xTaskCreate(vTaskBackground,(char *)"Bkgnd",   256,  NULL, tskIDLE_PRIORITY+1, NULL);   
   xTaskCreate(vTaskConsole,   (char *)"Console", 1024, NULL, tskIDLE_PRIORITY+2, NULL);
   xTaskCreate(vTaskDisplay,   (char *)"Display", 256,  NULL, tskIDLE_PRIORITY+2, NULL);
   xTaskCreate(vTaskGPS,       (char *)"GPS",     1024, NULL, tskIDLE_PRIORITY+3, NULL);
   xTaskCreate(vTaskSP1,       (char *)"SP1",     1024, NULL, tskIDLE_PRIORITY+4, NULL);
   xTaskCreate(vTaskControl,   (char *)"Control", 1024, NULL, tskIDLE_PRIORITY+5, NULL);

   vTaskStartScheduler();
   return 0;
}

void vApplicationIdleHook(void) // when RTOS is idle: should call "sleep until an interrupt"
{ }

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif

/**
  * @}
  */


/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
