/*******************************************************************************
* \file dfu_elf_symbols.c
* \version 4.20
*
* This file provides inline assembly to add symbols in the an ELF file required
* by CyMCUElfTool to generate correct CYACD2 image.
*
* \note
* This file requires modifications of DFU specific symbols for the
* application #1.
*
********************************************************************************
* \copyright
* (c) (2016-YEAR), Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
********************************************************************************
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

#include "dfu_common.h"


/* Symbols that are added to the ELF file */
__asm
(
    /* DFU specific symbols */
    ".global __cy_app_id \n"
    ".global __cy_product_id \n"
    ".global __cy_boot_metadata_addr \n"
    ".global __cy_boot_metadata_length \n"

    ".global  __cy_app_core1_start_addr \n"
    ".global  __cy_checksum_type \n"
    ".global  __cy_app_verify_start \n"
    ".global  __cy_app_verify_length \n"

    ".equ __cy_boot_metadata_addr,   " CY_STR(CY_BOOT_METADATA_ADDR) "\n"
    ".equ __cy_boot_metadata_length, " CY_STR(CY_BOOT_METADATA_LENGTH) "\n"

    ".equ __cy_app_id,               " CY_STR(CY_APP_ID) "\n"
    ".equ __cy_product_id,           " CY_STR(CY_PRODUCT_ID) "\n"
    ".equ __cy_checksum_type,        " CY_STR(CY_CHECKSUM_TYPE) "\n"
    ".equ __cy_app_core1_start_addr, " CY_STR(CY_APP1_FLASH_ADDR) " \n"
    ".equ __cy_app_verify_start,     " CY_STR(CY_APP1_FLASH_ADDR) " \n"
    ".equ __cy_app_verify_length,    " CY_STR(CY_APP1_FLASH_LENGTH) " - " CY_STR(CY_BOOT_SIGNATURE_SIZE) "\n"

    /* flash */
    ".global __cy_memory_0_start    \n"
    ".global __cy_memory_0_length   \n"
    ".global __cy_memory_0_row_size \n"

    /* flash */
    ".equ __cy_memory_0_start,    0x00000000 \n"
    ".equ __cy_memory_0_length,   0x00020000 \n"
    ".equ __cy_memory_0_row_size, 0x100 \n"
);


/* [] END OF FILE */
