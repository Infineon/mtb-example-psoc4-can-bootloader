/***************************************************************************//**
* \file dfu_user.c
* \version 5.2
*
* This file provides the custom API for a firmware application with
* DFU SDK.
* - Cy_DFU_ReadData (address, length, ctl, params) - to read  the NVM block
* - Cy_Bootalod_WriteData(address, length, ctl, params) - to write the NVM block
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

#include <string.h>
#include "cy_syslib.h"
#include "cy_dfu.h"
#include "transport_can.h"
/*
* The DFU SDK metadata initial value is placed here
* Note: the number of elements equal to the number of the app multiplies by 2
*       because of the two fields per app plus one element for the CRC-32C field.
*/
CY_MISRA_DEVIATE_BLOCK_START('MISRA C-2012 Rule 9.3',1,'The rest of the array initialization is not required.');
CY_SECTION(".cy_boot_metadata") __USED
static const uint32_t cy_dfu_metadata[CY_FLASH_SIZEOF_ROW / sizeof(uint32_t)] =

{
    CY_DFU_APP0_VERIFY_START, CY_DFU_APP0_VERIFY_LENGTH, /* The App0 base address and length */
    CY_DFU_APP1_VERIFY_START, CY_DFU_APP1_VERIFY_LENGTH, /* The App1 base address and length */
    0U                                                             /* The rest does not matter     */
};
CY_MISRA_BLOCK_END('MISRA C-2012 Rule 9.3');


static uint32_t IsMultipleOf(uint32_t value, uint32_t multiple);
static void GetStartEndAddress(uint32_t appId, uint32_t *startAddress, uint32_t *endAddress);

/*******************************************************************************
* Function Name: IsMultipleOf
****************************************************************************//**
*
* This internal function check if value parameter is a multiple of parameter
* multiple
*
* \param value      value that will be checked
* \param multiple   value with which value is checked
*
* \return 1 - value is multiple of parameter multiple, else 0
*
*******************************************************************************/
static uint32_t IsMultipleOf(uint32_t value, uint32_t multiple)
{
    return ( ((value % multiple) == 0U)? 1UL : 0UL);
}


/*******************************************************************************
* Function Name: GetStartEndAddress
****************************************************************************//**
*
* This internal function returns start and end address of application
*
* \param appId          The application number
* \param startAddress   The pointer to a variable where an application start
*                       address is stored
* \param endAddress     The pointer to a variable where a size of application
*                       area is stored.
*
*******************************************************************************/
static void GetStartEndAddress(uint32_t appId, uint32_t *startAddress, uint32_t *endAddress)
{
    uint32_t verifyStart;
    uint32_t verifySize;

    (void)Cy_DFU_GetAppMetadata(appId, &verifyStart, &verifySize);

#if (CY_DFU_APP_FORMAT == CY_DFU_SIMPLIFIED_APP)
    *startAddress = verifyStart - CY_DFU_SIGNATURE_SIZE;
    *endAddress = verifyStart + verifySize;
#else
    *startAddress = verifyStart;
    *endAddress = verifyStart + verifySize + CY_DFU_SIGNATURE_SIZE;
#endif
}


/*******************************************************************************
* Function Name: Cy_DFU_WriteData
****************************************************************************//**
*
* This function documentation is part of the DFU SDK API, see the
* cy_dfu.h file or DFU SDK API Reference Manual for details.
*
*******************************************************************************/
cy_en_dfu_status_t Cy_DFU_WriteData (uint32_t address, uint32_t length, uint32_t ctl,
                                               cy_stc_dfu_params_t *params)
{
    /* application flash limits */
    /* Note that App0 is out of range */
    const uint32_t minUFlashAddress = CY_FLASH_BASE + CY_DFU_APP0_VERIFY_LENGTH;
    const uint32_t maxUFlashAddress = CY_FLASH_BASE + CY_FLASH_SIZE;

    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    uint32_t app = Cy_DFU_GetRunningApp();
    uint32_t startAddress;
    uint32_t endAddress;

    GetStartEndAddress(app, &startAddress, &endAddress);

    /* Check if the address  and length are valid
     * Note Length = 0 is valid for erase command */
    if ( (IsMultipleOf(address, CY_FLASH_SIZEOF_ROW) == 0U) ||
         ( (length != CY_FLASH_SIZEOF_ROW) && ( (ctl & CY_DFU_IOCTL_ERASE) == 0U) ) )
    {
        status = CY_DFU_ERROR_LENGTH;
    }

    /* Refuse to write to a row within a range of the current application */
    if ( (startAddress <= address) && (address < endAddress) )
    {   /* It is forbidden to overwrite the currently running application */
        status = CY_DFU_ERROR_ADDRESS;
    }
#if CY_DFU_OPT_GOLDEN_IMAGE
    if (status == CY_DFU_SUCCESS)
    {
        uint8_t goldenImages[] = { CY_DFU_GOLDEN_IMAGE_IDS() };
        uint32_t count = sizeof(goldenImages) / sizeof(goldenImages[0]);
        uint32_t idx;
        for (idx = 0U; idx < count; ++idx)
        {
            app = goldenImages[idx];
            GetStartEndAddress(app, &startAddress, &endAddress);

            if ( (startAddress <= address) && (address < endAddress) )
            {
                status = Cy_DFU_ValidateApp(app, params);
                status = (status == CY_DFU_SUCCESS) ? CY_DFU_ERROR_ADDRESS : CY_DFU_SUCCESS;
                break;
            }
        }
    }
#endif /* #if CY_DFU_OPT_GOLDEN_IMAGE != 0 */

    /* Check if the address is inside the valid range */
    if ((minUFlashAddress <= address) && (address < maxUFlashAddress))
    {   /* Do nothing, this is an allowed memory range to update to */
    }
    else
    {
        status = CY_DFU_ERROR_ADDRESS;
    }

    if (status == CY_DFU_SUCCESS)
    {
        if ((ctl & CY_DFU_IOCTL_ERASE) != 0U)
        {
            (void) memset(params->dataBuffer, 0, CY_FLASH_SIZEOF_ROW);
        }
CY_MISRA_DEVIATE_LINE('MISRA C-2012 Rule 11.3','Casting uint8_t* to uint32_t* is safe as input address is always valid and aligned.');
        cy_en_flashdrv_status_t fstatus =  Cy_Flash_WriteRow(address, (uint32_t*)params->dataBuffer);
        status = (fstatus == CY_FLASH_DRV_SUCCESS) ? CY_DFU_SUCCESS : CY_DFU_ERROR_DATA;
    }
    return (status);
}


/*******************************************************************************
* Function Name: Cy_DFU_ReadData
****************************************************************************//**
*
* This function documentation is part of the DFU SDK API, see the
* cy_dfu.h file or DFU SDK API Reference Manual for details.
*
*******************************************************************************/
cy_en_dfu_status_t Cy_DFU_ReadData (uint32_t address, uint32_t length, uint32_t ctl,
                                              cy_stc_dfu_params_t *params)
{
    /* application flash limits */
    /* Note that App0 is out of range */
    const uint32_t minUFlashAddress = CY_FLASH_BASE + CY_DFU_APP0_VERIFY_LENGTH;
    const uint32_t maxUFlashAddress = CY_FLASH_BASE + CY_FLASH_SIZE;

    cy_en_dfu_status_t status = CY_DFU_SUCCESS;

    /* Check if the length is valid */
    if (IsMultipleOf(length, CY_FLASH_SIZEOF_ROW) == 0U)
    {
        status = CY_DFU_ERROR_LENGTH;
    }

    /* Check if the address is inside the valid range */
    if ((minUFlashAddress <= address) && (address < maxUFlashAddress))
    {   /* Do nothing, this is an allowed memory range to update to */
    }
    else
    {
        status = CY_DFU_ERROR_ADDRESS;
    }

    /* Read or Compare */
    if (status == CY_DFU_SUCCESS)
    {
        if ((ctl & CY_DFU_IOCTL_COMPARE) == 0U)
        {
CY_MISRA_DEVIATE_BLOCK_START('MISRA C-2012 Rule 11.6',2,'Cast of uint32_t value\
to void* is safe as variable always has valid address value');
            (void) memcpy((void *)params->dataBuffer, (const void *)address, length);
            status = CY_DFU_SUCCESS;
        }
        else
        {
            status = ( memcmp((const void *)params->dataBuffer, (const void *)address, length) == 0 )
                     ? CY_DFU_SUCCESS : CY_DFU_ERROR_VERIFY;
CY_MISRA_BLOCK_END('MISRA C-2012 Rule 11.6');
        }
    }
    return (status);
}


/*******************************************************************************
* Function Name: Cy_DFU_TransportStart
****************************************************************************//**
*
* This function documentation is part of the DFU SDK API, see the
* cy_dfu.h file 
*
*******************************************************************************/
void Cy_DFU_TransportStart(cy_en_dfu_transport_t transport)
{
    CAN_canCyBtldrCommStart();

    CY_UNUSED_PARAMETER(transport);
}


/*******************************************************************************
* Function Name: Cy_DFU_TransportStop
****************************************************************************//**
*
* This function documentation is part of the DFU SDK API, see the
* cy_dfu.h file 
*
*******************************************************************************/
void Cy_DFU_TransportStop(void)
{
    CAN_canCyBtldrCommStop();
}


/*******************************************************************************
* Function Name: Cy_DFU_TransportReset
****************************************************************************//**
*
* This function documentation is part of the DFU SDK API, see the
* cy_dfu.h file 
*
*******************************************************************************/
void Cy_DFU_TransportReset(void)
{
    CAN_canCyBtldrCommReset();
}


/*******************************************************************************
* Function Name: Cy_DFU_TransportRead
****************************************************************************//**
*
* This function documentation is part of the DFU SDK API, see the
* cy_dfu.h file 
*
*******************************************************************************/
cy_en_dfu_status_t Cy_DFU_TransportRead(uint8_t buffer[], uint32_t size, uint32_t *count, uint32_t timeout)
{
    cy_en_dfu_status_t status = CY_DFU_ERROR_UNKNOWN;
    status = CAN_canCyBtldrCommRead(buffer, size, count, timeout);
    return status;
}


/*******************************************************************************
* Function Name: Cy_DFU_TransportWrite
****************************************************************************//**
*
* This function documentation is part of the DFU SDK API, see the
* cy_dfu.h file 
*
*******************************************************************************/
cy_en_dfu_status_t Cy_DFU_TransportWrite(uint8_t buffer[], uint32_t size, uint32_t *count, uint32_t timeout)
{
    cy_en_dfu_status_t status = CY_DFU_ERROR_UNKNOWN;
    status = CAN_canCyBtldrCommWrite(buffer, size, count, timeout);
    return status;
}


/* [] END OF FILE */
