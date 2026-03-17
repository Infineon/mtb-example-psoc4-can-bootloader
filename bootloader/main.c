/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the PSoC4100S Plus: CAN Bootloader
* This file implements the DFUSDK based Bootloader.
*
* Related Document: See README.md
*
*
*******************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

#include "cy_pdl.h"
#include "cybsp.h"
#include "cycfg_pins.h"
#include "cy_dfu.h"

/*******************************************************************************
* Macros
********************************************************************************/

/* Interval for LED toggle: 1  blink per second */
#define LED_TOGGLE_INTERVAL_MS                      (500u)

/* User button interrupt priority. */
#define SWITCH_INTR_PRIORITY                        (3u)

/* Timeout for Cy_DFU_Continue(), in milliseconds */
#define CAN_DFU_SESSION_TIMEOUT_MS                  (10u)

/* Application ID */
#define USER_APP_ID                                 (1u)

/* CAN DFU idle wait timeout: 300 seconds*/
#define CAN_DFU_IDLE_TIMEOUT_MS                     (300000u)

/* CAN DFU session timeout: 5 seconds */
#define CAN_DFU_COMMAND_TIMEOUT_MS                  (5000u)

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void user_button_event_handler();
static cy_en_dfu_status_t handle_metadata(cy_stc_dfu_params_t *params);
static cy_en_dfu_status_t copy_row(uint32_t dest, uint32_t src, uint32_t rowSize, cy_stc_dfu_params_t * params);
static cy_en_dfu_status_t restart_dfu(uint32_t *state, cy_stc_dfu_params_t *dfu_params );
static cy_en_dfu_status_t manage_bootable_app(cy_stc_dfu_params_t *dfu_params);
static uint32_t counter_timeout_seconds(uint32_t seconds, uint32_t timeout);
void Cy_OnResetUser(void);

/*******************************************************************************
* Global Variables
********************************************************************************/
volatile static bool is_user_event_detected;

const cy_stc_sysint_t switch_intr_config = {
    .intrSrc = CYBSP_USER_BTN_IRQ,                  /* Source of interrupt signal */
    .intrPriority = SWITCH_INTR_PRIORITY            /* Interrupt priority */
};

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the bootloder main function for CM0+ CPU.
*  1. If application started from Non-Software reset it validates app #1
*        --> If app#1 is valid it switches to app#1, else goto #2.
*  2. Start DFU communication.
*  3. If updated application has been received it validates this app.
*  4. If app#1 is valid it switches to it, else wait for new application.
*  5. If CAN_DFU_IDLE_TIMEOUT_MS seconds has passed and no new application has been received
*     then validate app#1, if it is valid then switch to it, else continue as is.
*     #5 will be repeated for every CAN_DFU_IDLE_TIMEOUT_MS seconds, if the new firmware is not received.
*
* Parameters:
*  void
*
* Return:
*  int
*******************************************************************************/
int main(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    result = cybsp_init() ;
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    /* Used to count seconds. */
    uint32_t count = 0;

    /* Status codes for DFU API. */
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    /* Timeout variable. */
    uint32_t timeout_seconds = 0;

    /*
     * DFU state, one of the:
     * - CY_DFU_STATE_NONE
     * - CY_DFU_STATE_UPDATING
     * - CY_DFU_STATE_FINISHED
     * - CY_DFU_STATE_FAILED
     */
    uint32_t state = CY_DFU_STATE_NONE;

    /* Buffer to store DFU commands. */
    CY_ALIGN(4) static uint8_t buffer[CY_DFU_SIZEOF_DATA_BUFFER];

    /* Buffer for DFU data packets for transport API. */
    CY_ALIGN(4) static uint8_t packet[CY_DFU_SIZEOF_CMD_BUFFER];

    /* DFU params, used to configure DFU. */
    cy_stc_dfu_params_t dfu_params;

    /* Initialize dfu_params structure. */
    dfu_params.timeout          = CAN_DFU_SESSION_TIMEOUT_MS;
    dfu_params.dataBuffer       = &buffer[0];
    dfu_params.packetBuffer     = &packet[0];

    status = Cy_DFU_Init(&state, &dfu_params);
    CY_ASSERT(CY_DFU_SUCCESS == status);

    /* Ensure DFU Metadata is valid. */
    status = handle_metadata(&dfu_params);
    CY_ASSERT(CY_DFU_SUCCESS == status);

    /* Initialize and enable GPIO interrupt */
    Cy_SysInt_Init(&switch_intr_config, user_button_event_handler);

    /* Clearing and enabling the GPIO interrupt in NVIC */
    NVIC_ClearPendingIRQ(switch_intr_config.intrSrc);
    NVIC_EnableIRQ(switch_intr_config.intrSrc);

    /* In the case of non-software reset check if there is a valid app image.
     * If there is - switch to it.
     */
    if( (Cy_SysLib_GetResetReason() != CY_SYSLIB_RESET_SOFT) )
    {
        /* Boot to application image, if valid. */
        status = manage_bootable_app(&dfu_params);

        if (status == CY_DFU_SUCCESS)
        {
            /* Check if ECO is enable */
            if (Cy_SysClk_EcoIsEnabled())
            {
                /* Switch HFCLK source to IMO */
                Cy_SysClk_ClkHfSetSource(CY_SYSCLK_CLKHF_IN_IMO);
                /* Disable ECO */
                Cy_SysClk_EcoDisable();
            }
            /* Never returns from here. */
            Cy_DFU_ExecuteApp(USER_APP_ID);
        }
        /* Continue DFU execution (below), if user application not valid !. */
    }

    CY_ASSERT(result == CY_RSLT_SUCCESS);

    /* Enable interrupts. */
    __enable_irq();
    
    /* Initiates the CAN communication interface to facilitate application 
       updates via it */
    Cy_DFU_TransportStart(CY_DFU_CAN_2_0);

    /* Keep the compiler happy. */
    (void)result;

    for (;;)
    {
        status = Cy_DFU_Continue(&state, &dfu_params);
        ++count;

        if (state == CY_DFU_STATE_FINISHED)
        {
           /* Finished loading the application image.
            * Validate DFU application, if it is valid then switch to it.
            */

            status = manage_bootable_app(&dfu_params);
            if (status == CY_DFU_SUCCESS)
            {
                Cy_DFU_TransportStop();
                /* Check if ECO is enable */
                if (Cy_SysClk_EcoIsEnabled())
                {
                    /* Switch HFCLK source to IMO */
                    Cy_SysClk_ClkHfSetSource(CY_SYSCLK_CLKHF_IN_IMO);
                    /* Disable ECO */
                    Cy_SysClk_EcoDisable();
                }
                Cy_DFU_ExecuteApp(USER_APP_ID);
            }
            else if (status == CY_DFU_ERROR_VERIFY)
            {
                /* Restarts loading, an alternatives are to Halt MCU here
                 * or switch to the other app if it is valid.
                 * Error code may be handled here, i.e. print to debug UART.
                 */
                status = restart_dfu(&state, &dfu_params );
            }
        }
        else if (state == CY_DFU_STATE_FAILED)
        {
            /* An error has happened during the loading process.
             * In this Code Example just restart loading process.
             * Restart DFU.
             */
            status = restart_dfu(&state, &dfu_params );
        }
        else if (state == CY_DFU_STATE_UPDATING)
        {
            timeout_seconds = (count >= counter_timeout_seconds(CAN_DFU_COMMAND_TIMEOUT_MS, CAN_DFU_SESSION_TIMEOUT_MS)) ? USER_APP_ID : 0u;

            /* if no command has been received during 5 seconds 
             * defined in the macro CAN_DFU_COMMAND_TIMEOUT_MS when the loading
             * has started then restart loading.
             */
            if (status == CY_DFU_SUCCESS)
            {
               count = 0u;
            }
            else if (status == CY_DFU_ERROR_TIMEOUT)
            {
                if (timeout_seconds != 0u)
                {
                    count = 0u;

                    /* Restart DFU. */
                    status = restart_dfu(&state, &dfu_params );
                }
            }
            else
            {
                count = 0u;

                /* Delay because Transport still may be sending error response to a host. */
                Cy_SysLib_Delay(CAN_DFU_SESSION_TIMEOUT_MS);

                /* Restart DFU. */
                status = restart_dfu(&state, &dfu_params );
            }
        }

        /* Blink once per seconds */
        if ( ( count % counter_timeout_seconds(LED_TOGGLE_INTERVAL_MS, CAN_DFU_SESSION_TIMEOUT_MS)) == 0u)
        {
            /* Invert the USER LED state */
            Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
        }

        if((count >= counter_timeout_seconds(CAN_DFU_IDLE_TIMEOUT_MS, CAN_DFU_SESSION_TIMEOUT_MS) ) && (state == CY_DFU_STATE_NONE))
        {
            status = manage_bootable_app(&dfu_params);

            if (status == CY_DFU_SUCCESS)
            {
                Cy_DFU_TransportStop();
                /* Check if ECO is enable */
                if (Cy_SysClk_EcoIsEnabled())
                {
                    /* Switch HFCLK source to IMO */
                    Cy_SysClk_ClkHfSetSource(CY_SYSCLK_CLKHF_IN_IMO);
                    /* Disable ECO */
                    Cy_SysClk_EcoDisable();
                }
                Cy_DFU_ExecuteApp(USER_APP_ID);
            }

            /* In case, no valid user application, lets start fresh all over.
             * This is just for demonstration.
             * Final application can change it to either assert, reboot, enter low power mode etc,
             * based on usecase requirements.
             */
            count = 0 ;
        }

        /* Is user_button event detected ?. */
        if(is_user_event_detected == true)
        {
            is_user_event_detected = false;
            /* Boot to application image, if valid. */
            status = manage_bootable_app(&dfu_params);
            if (status == CY_DFU_SUCCESS)
            {
                /* Check if ECO is enable */
                if (Cy_SysClk_EcoIsEnabled())
                {
                    /* Switch HFCLK source to IMO */
                    Cy_SysClk_ClkHfSetSource(CY_SYSCLK_CLKHF_IN_IMO);
                    /* Disable ECO */
                    Cy_SysClk_EcoDisable();
                }
                /* Never returns from here. */
                Cy_DFU_ExecuteApp(USER_APP_ID);
            }
        }
    }
}

/*******************************************************************************
* Function Name: counter_timeout_seconds
********************************************************************************
* Returns number of counts that correspond to number of seconds passed as
* a parameter.
*
* Parameters:
*  seconds    Number of seconds to pass. Must be less that 4_294_967 seconds.
*  timeout    Timeout for Cy_DFU_Continue() function, in milliseconds.
*             Must be greater than zero.
*             It is recommended to be a value that produces no reminder
*             for this function to be precise.
* Return:
*  See description.
*******************************************************************************/
static uint32_t counter_timeout_seconds(uint32_t seconds, uint32_t timeout)
{
    uint32_t count = 1 ;

    if(timeout != 0 )
    {
        count = ( (seconds) / timeout);
    }

    return count;
}

/*******************************************************************************
* Function Name: manage_bootable_app
********************************************************************************
* Performs validation of application, clears the existing reset reason if
* a valid image is found.
*
* Parameters:
*  dfuParams     input DFU parameters.
*
* Returns:
*  CY_DFU_SUCCESS if operation is successful and an error code in case of failure.
*******************************************************************************/
static cy_en_dfu_status_t manage_bootable_app(cy_stc_dfu_params_t *dfu_params)
{
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    if(!dfu_params)
    {
        status = CY_DFU_ERROR_UNKNOWN;
    }

    /* Satisfy the compiler. */
    (void)dfu_params;

    if (status == CY_DFU_SUCCESS)
    {
        /* Validate the APP in SECONDARY slot. */
        status = Cy_DFU_ValidateApp(USER_APP_ID, NULL);
    }

    if (status == CY_DFU_SUCCESS)
    {
        /*
        * Clear reset reason because Cy_DFU_ExecuteApp() performs a
        * software reset. Without clearing, two reset reasons would be present.
        */
        do
        {
            Cy_SysLib_ClearResetReason();
        }while(Cy_SysLib_GetResetReason() != 0 );
    }
    return status;
}

/*******************************************************************************
* Function Name: restart_dfu
********************************************************************************
* This function re-initializes the DFU and resets the DFU transport.
*
* Parameters:
*  state          input current state of the DFU
*  dfu_params     input DFU parameters.
*
* Return:
*  Status of operation.
*******************************************************************************/
static cy_en_dfu_status_t restart_dfu(uint32_t *state, cy_stc_dfu_params_t *dfu_params )
{
    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    if(!state || !dfu_params)
    {
        status = CY_DFU_ERROR_UNKNOWN;
    }

    /* Restart DFU process. */
    if( status == CY_DFU_SUCCESS)
    {
        status = Cy_DFU_Init(state, dfu_params);
    }

    if( status == CY_DFU_SUCCESS)
    {
        Cy_DFU_TransportReset();
    }

    return status;
}

/*******************************************************************************
* Function Name: copy_row
********************************************************************************
* Copies data from a source address to a flash row with the address destination.
* If "src" data is the same as "dest" data then no copy is performed.
*
* Parameters:
*  dest     Destination address. Has to be an address of the start of flash row.
*  src      Source address.
*  rowSize  Size of the flash row.
*  params   input DFU parameters.
*
* Returns:
*  CY_DFU_SUCCESS if operation is successful. Error code in a case of failure.
*******************************************************************************/
static cy_en_dfu_status_t copy_row(uint32_t dest, uint32_t src, uint32_t rowSize, cy_stc_dfu_params_t * params)
{
    cy_en_dfu_status_t status;

    /* Save params->dataBuffer value. */
    uint8_t *buffer = params->dataBuffer;

    /* Compare "dest" and "src" content. */
    params->dataBuffer = (uint8_t *)src;
    status = Cy_DFU_ReadData(dest, rowSize, CY_DFU_IOCTL_COMPARE, params);

    /* Restore params->dataBuffer. */
    params->dataBuffer = buffer;

    /* If "dest" differs from "src" then copy "src" to "dest". */
    if (status != CY_DFU_SUCCESS)
    {
        (void) memcpy((void *) params->dataBuffer, (const void*)src, rowSize);
        status = Cy_DFU_WriteData(dest, rowSize, CY_DFU_IOCTL_WRITE, params);
    }
    /* Restore params->dataBuffer. */
    params->dataBuffer = buffer;

    return (status);
}

/*******************************************************************************
* Function Name: handle_metadata
********************************************************************************
* The goal of this function is to make DFU SDK metadata (MD) valid.
*
* Parameters:
*  params   A pointer to a DFU SDK parameters structure.
*
* Returns:
* - CY_DFU_SUCCESS when finished normally.
* - Any other status code on error.
*******************************************************************************/
static cy_en_dfu_status_t handle_metadata(cy_stc_dfu_params_t *params)
{
    const uint32_t MD     = (uint32_t)(&__cy_boot_metadata_addr   ); /* MD address.  */
    const uint32_t mdSize = (uint32_t)(&__cy_boot_metadata_length ); /* MD size, assumed to be one flash row .*/
    const uint32_t MDC    = MD + mdSize;                             /* MDC address. */

    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    status = Cy_DFU_ValidateMetadata(MD, params);
    if (status == CY_DFU_SUCCESS)
    {
        /* Checks if MDC equals to DC, if no then copies MD to MDC. */
        status = copy_row(MDC, MD, mdSize, params);
    }
    else
    {
        status = Cy_DFU_ValidateMetadata(MDC, params);
        if (status == CY_DFU_SUCCESS)
        {
            /* Copy MDC to MD. */
            status = copy_row(MD, MDC, mdSize, params);
        }

        /* Possibly a clean boot or first boot. */
        if (status != CY_DFU_SUCCESS)
        {
            const uint32_t elfStartAddress = CY_DFU_APP0_VERIFY_START;
            const uint32_t elfAppSize      = CY_DFU_APP0_VERIFY_LENGTH;

            /* Set MD to INITIAL_VALUE. */
            status = Cy_DFU_SetAppMetadata(0u, elfStartAddress, elfAppSize, params);
        }
    }
    return (status);
}

/*******************************************************************************
* Function Name: Cy_OnResetUser
********************************************************************************
* This function is called at the start of Reset_Handler().
* DFU requires it to call Cy_DFU_OnResetApp0() in app#0.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
void Cy_OnResetUser(void)
{
    Cy_DFU_OnResetApp0();
}

/*******************************************************************************
* Function Name: user_button_event_handler
********************************************************************************
* User Button event handler.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
static void user_button_event_handler()
{
    /* Clears the triggered pin interrupt */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_NUM);
    NVIC_ClearPendingIRQ(switch_intr_config.intrSrc);

    is_user_event_detected = true;
}

/* [] END OF FILE */

