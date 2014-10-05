#include "console.h"
#include <stm32l1xx.h>
#include <FreeRTOS.h>
#include <FreeRTOS_CLI.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include <string.h>
#include "messages.h"
#include "usart.h"
#include "commands.h"
#include "cir_buf.h"
#include "options.h"

/* -------- defines -------- */
#define MAX_INPUT_LENGTH    100
#define MAX_OUTPUT_LENGTH   100
#define NMEA_SENTENCE_MODE     0
#define CONSOLE_SENTENCE_MODE  1
#define MAX_NMEA_SENTENCE      100

/* -------- variables -------- */
/* Console task queue */
xQueueHandle  console_que;
static const char * const pcWelcomeMessage = "\r\nOGN Tracker Console.\r\n";
/* Console NMEA sentence storage */
char nmea_sentence[MAX_NMEA_SENTENCE];
uint8_t nmea_sent_len;
/* Pointers to data structures assigned in GPS task */
xQueueHandle* gps_task_queue;
cir_buf_str*  gps_task_cir_buf;

/* -------- interrupt handlers -------- */
/* -------- functions -------- */
/**
* @brief  Configures the Console Task Peripherals.
* @param  None
* @retval None
*/
void Console_Config(void)
{
   uint32_t* cons_speed = (uint32_t*)GetOption(OPT_CONS_SPEED);
   if (cons_speed)
   {
      USART2_Config(*cons_speed);
   }
}

/**
* @brief  Sets circular buffer used for NMEA sentences.
* @param  Initialized cir. buf. structure
* @retval None
*/
void Console_SetNMEABuf(cir_buf_str* handle)
{
   gps_task_cir_buf = handle;
}

/**
* @brief  Sets queue for USART3 received data.
* @param  Initialized queue handle
* @retval None
*/
void Console_SetGPSQue(xQueueHandle* handle)
{
   gps_task_queue = handle;
}

/**
* @brief  Sends Console string.
* @param  String address & len.
* @retval None
*/
void Console_Send(const char* str, char block)
{
   USART2_Send((uint8_t*)str, strlen(str));
   if (block)
   {
         USART2_Wait();
   }
}

/**
* @brief  Sends Console char.
* @param  None
* @retval None
*/
void Console_Send_Char(char ch)
{
   static uint8_t data;
   data = ch;
   USART2_Send(&data, 1);
}
/**
* @brief  Handle char entered by console.
* @param  Received character
* @retval None
*/
void handle_console_input(char cRxedChar)
{
   portBASE_TYPE xMoreDataToFollow;
   static char cInputIndex = 0;
   static char pcOutputString[ MAX_OUTPUT_LENGTH ], pcInputString[ MAX_INPUT_LENGTH ];

   Console_Send_Char(cRxedChar);

   if( cRxedChar == '\r' )
   {
      /* A newline character was received, so the input command string is
      complete and can be processed.  Transmit a line separator, just to
      make the output easier to read. */
      Console_Send_Char('\n');

      /* The command interpreter is called repeatedly until it returns
      pdFALSE.  See the "Implementing a command" documentation for an
      explanation of why this is. */
      do
      {
          /* Send the command string to the command interpreter.  Any
          output generated by the command interpreter will be placed in the
          pcOutputString buffer. */
          xMoreDataToFollow = FreeRTOS_CLIProcessCommand
                        (
                            pcInputString,   /* The command string.*/
                            pcOutputString,  /* The output buffer. */
                            MAX_OUTPUT_LENGTH/* The size of the output buffer. */
                        );

          /* Write the output generated by the command interpreter to the
          console.
          Wait until transfer ends, because pcOutputString is altered immediately */
          Console_Send(pcOutputString, 1);

      } while( xMoreDataToFollow != pdFALSE );

      /* All the strings generated by the input command have been sent.
      Processing of the command is complete.  Clear the input string ready
      to receive the next command. */
      cInputIndex = 0;
      memset( pcInputString, 0x00, MAX_INPUT_LENGTH );
   }
   else
   {
      /* The if() clause performs the processing after a newline character
         is received.  This else clause performs the processing if any other
         character is received. */

      if( cRxedChar == '\n' )
      {
          /* Ignore carriage returns. */
      }
      else if(( cRxedChar == '\b' ) || ( cRxedChar == '\177' ))
      {
         /* Backspace or Del were pressed.  Erase the last character in the input
             buffer - if there are any. */
         if( cInputIndex > 0 )
         {
             cInputIndex--;
             pcInputString[(unsigned char)cInputIndex] = '\0';
         }
      }
      else
      {
         /* A character was entered.  It was not a new line, backspace
         or carriage return, so it is accepted as part of the input and
         placed into the input buffer.  When a \n is entered the complete
         string will be passed to the command interpreter. */
         if( cInputIndex < MAX_INPUT_LENGTH )
         {
             pcInputString[(unsigned char)cInputIndex] = cRxedChar;
             cInputIndex++;
         }
      }
   }
}

/**
* @brief  Main Console Task.
* @param  None
* @retval None
*/
void vTaskConsole(void* pvParameters)
{
   task_message msg, gps_msg;
   uint8_t      console_mode;

   console_que = xQueueCreate(10, sizeof(task_message));
   USART2_SetQue(&console_que);

   /* Register all console commands handlers */
   RegisterCommands();

   USART_Cmd(USART2, ENABLE);

   Console_Send(pcWelcomeMessage, 0);

   console_mode = CONSOLE_SENTENCE_MODE;

   for(;;)
   {
      xQueueReceive(console_que, &msg, portMAX_DELAY);
      switch (msg.src_id)
      {
          case CONSOLE_USART_SRC_ID:
          {
             char cons_char = msg.msg_opcode;
             if (cons_char == '$')
             {  /* start of NMEA sentence detected */
                console_mode = NMEA_SENTENCE_MODE;
                /* reset nmea string */
                nmea_sent_len = 0;
             }

             if (console_mode == CONSOLE_SENTENCE_MODE)
             {
                /* normal console mode */
                handle_console_input(cons_char);
             }
             else if (console_mode == NMEA_SENTENCE_MODE)
             {
                /* NMEA sentence mode - update string */
                nmea_sentence[nmea_sent_len++] = cons_char;

                if (cons_char == '\n')
                {
                   /* NMEA Sentence completed */
                   nmea_sentence[nmea_sent_len++] = '\0';
                   if (gps_task_cir_buf && gps_task_queue)
                   {
                      gps_msg.msg_data = (uint32_t)cir_put_data(gps_task_cir_buf, (uint8_t*)nmea_sentence, nmea_sent_len);
                      gps_msg.msg_len  = nmea_sent_len;
                      gps_msg.src_id   = CONSOLE_USART_SRC_ID;
                      /* Send NMEA sentence to GPS task */
                      xQueueSend(*gps_task_queue, &gps_msg, portMAX_DELAY);
                   }
                   /* go back to normal console mode */
                   console_mode = CONSOLE_SENTENCE_MODE;
                }
                if (nmea_sent_len >= MAX_NMEA_SENTENCE) nmea_sent_len = 0;
             }
             break;
          }
          default:
          {
             break;
          }
      }
   }
}
