/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the PSoC 4100S Plus : CAN Device Firmware 
* Upgrade (CAN Bootloader) Example for ModusToolbox.
* This file implements the simple blinky application that can be bootloaded by the
* Bootloader.
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
#include "cy_dfu.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* App ID of bootloader. */
#define BOOTLOADER_ID                    (0u)

/* Define LED toggle frequency. */
#define LED_TOGGLE_INTERVAL_MS           (100u)

/* User button interrupt priority. */
#define SWITCH_INTR_PRIORITY             (3u)

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void user_button_event_handler(void);

/*******************************************************************************
* Global Variables
********************************************************************************/
/* Application signature. */
CY_SECTION(".cy_app_signature") __USED static const uint32_t cy_dfu_appSignature;

/* Interrupt configuration structure */
const cy_stc_sysint_t switch_intr_config = {
    .intrSrc = CYBSP_USER_BTN_IRQ,                  /* Source of interrupt signal */
    .intrPriority = SWITCH_INTR_PRIORITY            /* Interrupt priority */
};

volatile static bool is_user_event_detected;

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CM0+ CPU.
*   1. It blinks LED at "LED_TOGGLE_INTERVAL_MS" interval.
*   2. If user event is detected, it will transfer control to
*      Bootloader.
*
* Parameters:
*  void
*
* Return:
*  int
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;

    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize and enable GPIO interrupt */
    result = Cy_SysInt_Init(&switch_intr_config, user_button_event_handler);

    /* Enable global interrupts */
    __enable_irq();

    /* Clearing and enabling the GPIO interrupt in NVIC */
    NVIC_ClearPendingIRQ(switch_intr_config.intrSrc);
    NVIC_EnableIRQ(switch_intr_config.intrSrc);

    for (;;)
    {
        /* Invert the USER LED state */
        Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);

        /* Delay between LED toggles */
        Cy_SysLib_Delay(LED_TOGGLE_INTERVAL_MS);

        /* Switch to bootloader, if user button is pressed. */
        if (is_user_event_detected)
        {
            /* Check if ECO is enable */
            if (Cy_SysClk_EcoIsEnabled())
            {
                /* Switch HFCLK source to IMO */
                Cy_SysClk_ClkHfSetSource(CY_SYSCLK_CLKHF_IN_IMO);
                /* Disable ECO */
                Cy_SysClk_EcoDisable();
            }
            is_user_event_detected = false;
            Cy_DFU_ExecuteApp(BOOTLOADER_ID);
        }
    }
}

/*******************************************************************************
* Function Name: user_button_event_handler
********************************************************************************
*   User Button event handler.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
static void user_button_event_handler(void)
{
    /* Clears the triggered pin interrupt */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_NUM);
    NVIC_ClearPendingIRQ(switch_intr_config.intrSrc);

    /* Set the flag */
    is_user_event_detected = true;
}

/* [] END OF FILE */
