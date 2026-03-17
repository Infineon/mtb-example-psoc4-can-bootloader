/***************************************************************************//**
* \file transport_can.c
* \version 5.2
*
* This file provides the source code of the DFU communication APIs
* for the CAN driver.
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
#include "transport_can.h"
#include "cy_can.h"
#include "cycfg_peripherals.h"
#include "cy_sysint.h"
#include <string.h>


#if !defined DFU_CAN_HW
#error "The CAN personality alias must be DFU_CAN to support DFU communication API."

/* Dummy configuration to generate only error above during a build */
#define CY_DFU_CAN_HW                   NULL
#define CY_DFU_CAN_CH_NUM               (0U)
#define CY_DFU_CAN_CFG_PTR              NULL
#define CY_DFU_CAN_IRQ_SOURCE           (0U)

#else

/* USER CONFIGURABLE: the pointer to the base address of the hardware */
#define CY_DFU_CAN_HW                   (DFU_CAN_HW)

/* USER CONFIGURABLE: the number of the hardware channel */
#define CY_DFU_CAN_CH_NUM               (DFU_CAN_CHANNEL_NUM)

/* USER CONFIGURABLE: the pointer to the configuration */
#define CY_DFU_CAN_CFG_PTR              (&DFU_CAN_config)

/* USER CONFIGURABLE: Interrupt configuration for the CAN block */
#define CY_DFU_CAN_IRQ_SOURCE           (can_interrupt_can_IRQn)
#endif /* !defined DFU_CAN_HW */

/* Interrupt priority. Valid range: 0 to 7. */
#ifndef DFU_CAN_IRQ_PRIORITY
#define DFU_CAN_IRQ_PRIORITY            (0U)
#endif /* DFU_CAN_IRQ_PRIORITY */

/* Returns a number of bytes to copy into a DFU buffer */
#define CAN_BYTES_TO_COPY(actBufSize, bufSize) \
                         (((uint32_t)(actBufSize) < (uint32_t)(bufSize)) ? \
                          ((uint32_t) (actBufSize)) : ((uint32_t) (bufSize)))

/* merge byte array into one 32-bit word */
#define BYTEARRAYTOWORD(array,index)    ((uint32_t)((array[index] << 24u)| \
                                                    (array[index + 1u] << 16u)| \
                                                    (array[index + 2u] << 8u)| \
                                                     array[index + 3u]))

/**
* Size of word in bytes
*/
#define WORD_SIZE_IN_BYTES              (4U)

/**
* Size of word in bytes
*/
#define MS_TO_US                        (1000U)


/**
 * Size of the Read buffer for CAN DFU
 */
#define CAN_BTLDR_SIZEOF_RX_BUFFER      (64u)

/**
 * Maxiumum data byte to be send in one CAN frame
 */
#define MAX_DATABYTE_IN_CAN_FRAME       (8U)

/**
 * End of DFU packet value
*/
#define EOF_DFU_PACKET_VALUE            (0x17U)

/**
 * Transport layer buffer index containing details of data length of DFU Packet 
 */
#define TL_DFU_PACKET_SIZE_IDX          (2u)    

/**
 * Transport Layer minimum length of DFU packet 
 */
#define TL_DFU_PACKET_MIN_SIZE          (7u)

/**
 *  CAN reads to this buffer
 */
static uint8_t CAN_RxBuf[CAN_BTLDR_SIZEOF_RX_BUFFER];

/**
 *  Number items in the buffer
 */
static uint32_t CAN_RxBufIdx;

/** The instance-specific context structure.
* It is used while the driver operation for internal configuration and
* data keeping for the CAN. The user should not modify anything in this
* structure.
*/
static  cy_stc_can_context_t            CY_CAN_context;
#define CY_DFU_CAN_CONTEXT              CY_CAN_context

/**
* CAN_initVar indicates whether the CAN driver has been initialized. The
* variable is initialized to false and set to true the first time
* \ref CAN_canCyBtldrCommStart is called. This allows the driver to restart
* without re-initialization after the first call to the
* \ref CAN_canCyBtldrCommStart routine.
* For re-initialization set \ref CAN_initVar to false and call
* \ref CAN_CanCyBtldrCommStart.
*/
bool CAN_initVar = false;

/**
* Maximum interframe space between the consecutive frames comprising DFU 
* packet in usec.

*/
#define CAN_WAIT_US                     (1000000U)

/* Indexes of the Read/Write buffers for CAN DFU */
#define CAN_TX_BUFFER_INDEX_0           (0U)
#define CAN_TX_BUFFER_INDEX_1           (1U)
#define CAN_RX_BUFFER_INDEX             (0U)

/* CAN Message identifier 0x20 */
#define  CY_CAN_MSG_ID0                 (0x20u)

/* RX buffer element for CAN DFU */
static cy_stc_can_message_frame_t CAN_rxBuffer;    /* Rx buffer */

/* TX buffer element for CAN DFU */
static cy_stc_can_message_frame_t CAN_txBuffer =
 {
     .id = CY_CAN_MSG_ID0,
     .data = {0},
     .length = 0u,
     .rtr = false,
     .ide = false
 };   /* Tx buffer */

/*******************************************************************************
* Function Name: CAN_Interrupt
****************************************************************************//**
*
* The CAN driver interrupt handler.
*
*******************************************************************************/
void CAN_Interrupt(void)
{
    uint32_t intrStatus;
    intrStatus = Cy_CAN_GetInterruptStatus(CY_DFU_CAN_HW);
    uint32_t swap_endian;
    uint32_t size_currentReceivedframe = 0;
    /* Store data in received CAN frame in Bootloader RX buffer */
    if (CY_CAN_RX_BUFFER_NEW_MESSAGE == (intrStatus & CY_CAN_RX_BUFFER_NEW_MESSAGE))
    {
        Cy_CAN_ClearInterrupt(CY_DFU_CAN_HW,(intrStatus & CY_CAN_RX_BUFFER_NEW_MESSAGE));
        cy_en_can_status_t canStatus = Cy_CAN_GetRxBuffer(CY_DFU_CAN_HW, CAN_RX_BUFFER_INDEX, &CAN_rxBuffer);
        if (CY_CAN_BAD_PARAM == canStatus)
        {
            CY_ASSERT(0);
        }
        size_currentReceivedframe = CAN_rxBuffer.length;
        if(size_currentReceivedframe <= WORD_SIZE_IN_BYTES)
        {
            swap_endian = CY_SWAP_ENDIAN32(CAN_rxBuffer.data[0]);
            memcpy((void *)&CAN_RxBuf[CAN_RxBufIdx],(const void  *)&swap_endian, size_currentReceivedframe);
            CAN_RxBufIdx += size_currentReceivedframe;
        }
        else
        {
            swap_endian = CY_SWAP_ENDIAN32(CAN_rxBuffer.data[0]);
            memcpy((void *)&CAN_RxBuf[CAN_RxBufIdx],(const void  *)&swap_endian, WORD_SIZE_IN_BYTES);
            CAN_RxBufIdx += WORD_SIZE_IN_BYTES;
            swap_endian = CY_SWAP_ENDIAN32(CAN_rxBuffer.data[1]);
            memcpy((void *)&CAN_RxBuf[CAN_RxBufIdx],(const void  *)&swap_endian, size_currentReceivedframe - WORD_SIZE_IN_BYTES);
            CAN_RxBufIdx += (size_currentReceivedframe - WORD_SIZE_IN_BYTES);
        }
        Cy_CAN_AckRxBuffer(CY_DFU_CAN_HW, CAN_RX_BUFFER_INDEX);
    }
}


/*******************************************************************************
* Function Name: CAN_canCyBtldrCommStart
****************************************************************************//**
*
* Starts the CAN transport.
*
*******************************************************************************/
void CAN_canCyBtldrCommStart(void)
{
    if (!CAN_initVar)
    {
        cy_en_can_status_t status;

        status = Cy_CAN_Init(CY_DFU_CAN_HW, CY_DFU_CAN_CFG_PTR, &CY_DFU_CAN_CONTEXT);
        if (CY_CAN_SUCCESS != status)
        {
            CY_ASSERT(0);
        }

        Cy_CAN_SetInterruptMask(CY_DFU_CAN_HW, CAN_INT_EBL_GLOBAL_INT_ENBL_Msk | CY_CAN_RX_BUFFER_NEW_MESSAGE);

        static const cy_stc_sysint_t CAN_IRQ_cfg =
        {
            .intrSrc = CY_DFU_CAN_IRQ_SOURCE,
            .intrPriority = DFU_CAN_IRQ_PRIORITY,
        };

        (void)Cy_SysInt_Init(&CAN_IRQ_cfg, CAN_Interrupt);
        NVIC_EnableIRQ(CAN_IRQ_cfg.intrSrc);

        CAN_initVar = true;
    }
}


/*******************************************************************************
* Function Name: CAN_canCyBtldrCommStop
****************************************************************************//**
*
*  Stops the CAN transport.
*
*******************************************************************************/
void CAN_canCyBtldrCommStop(void)
{
    if (CAN_initVar)
    {
        cy_en_can_status_t status;

        status = Cy_CAN_DeInit(CY_DFU_CAN_HW, &CY_DFU_CAN_CONTEXT);
        if (CY_CAN_SUCCESS != status)
        {
            CY_ASSERT(0);
        }

        CAN_initVar = false;
    }
}


/*******************************************************************************
* Function Name: CAN_canCyBtldrCommReset
****************************************************************************//**
*
*  Resets the receive and transmit communication buffers.
*
*******************************************************************************/
void CAN_canCyBtldrCommReset(void)
{
    CAN_RxBufIdx = 0U;
    Cy_CAN_AckRxBuffer(CY_DFU_CAN_HW, CAN_RX_BUFFER_INDEX);
}


/*******************************************************************************
* Function Name: CAN_canCyBtldrCommRead
****************************************************************************//**
*
*  Allows the caller to read data from the DFU host (the host writes the
*  data). The function handles polling to allow a block of data to be completely
*  received from the host device.
*
*  \param pData: Pointer to storage for the block of data to be read from the
*   DFU host
*  \param size: Number of bytes to be read.
*  \param count: Pointer to the variable to write the number of bytes actually
*   read.
*  \param timeOut The amount of time (in milliseconds) for which the
*                function should wait before indicating communication
*                time out.
*
*  \return
*   Returns CY_DFU_SUCCESS if no problem was encountered or returns the value
*   that best describes the problem. For more information refer to the
*   \ref cy_en_dfu_status_t description in the API Reference Guide.
*
*******************************************************************************/
cy_en_dfu_status_t CAN_canCyBtldrCommRead(uint8_t pData[], uint32_t size, uint32_t* count, uint32_t timeout)
{
    cy_en_dfu_status_t status = CY_DFU_ERROR_BAD_PARAM;
    uint32_t byteCount = 0u;

    /* Maximum interframe space between the consecutive frames comprising DFU 
       packet */
    uint32_t interframe_space_us = CAN_WAIT_US;
    
    /* Timeout in microseconds */
    uint32_t timeout_us = (timeout << 10u) - (timeout << 4u) - (timeout << 3u);

    if ((pData != NULL) && (count != NULL) && (size > 0U))
    {
        status = CY_DFU_ERROR_TIMEOUT;
        do
        {
            /* Check packet start */
            if (CAN_RxBufIdx != 0U)
            {
                /* Number of the data byte in DFU packet */
                uint16_t datalength = (uint16_t)((CAN_RxBuf[TL_DFU_PACKET_SIZE_IDX + 1u] << 8u)
                                                        |CAN_RxBuf[TL_DFU_PACKET_SIZE_IDX]);
                /* Total size of DFU packet */
                uint16_t dfuPacketSize = datalength + TL_DFU_PACKET_MIN_SIZE;
                
                /* Wait for end of packet */
                while (interframe_space_us != 0u)
                {   
                    if(byteCount != CAN_RxBufIdx)
                    {
                        byteCount = CAN_RxBufIdx;
                        interframe_space_us = CAN_WAIT_US;
                        
                        /* If all the frame Comprising DFU packet are received successfully or 
                           CAN boot-loader buffer is full*/
                        if(((byteCount == dfuPacketSize) && (CAN_RxBuf[byteCount - 1u] == EOF_DFU_PACKET_VALUE))
                            || (byteCount > CAN_BTLDR_SIZEOF_RX_BUFFER - 1u))
                        {
                            break;
                        }
                    }
                    Cy_SysLib_DelayUs(1U);
                    --interframe_space_us;
                }
                
                /* Reset CAN buffer index */
                CAN_RxBufIdx = 0U;
                byteCount = CAN_BYTES_TO_COPY(byteCount, size);
                *count = byteCount;
                
                /* Get data from RX buffer into DFU buffer */
                (void) memcpy((void *) pData, (const void *) CAN_RxBuf, (uint32_t) byteCount);
                
                /* End of the DFU packet(frame) */
                status = CY_DFU_SUCCESS;
                
                break;
            }
            Cy_SysLib_DelayUs(1U);
            --timeout_us;
        } while(timeout_us != 0u);
    }
    return (status);
}


/*******************************************************************************
* Function Name: CAN_canCyBtldrCommWrite
****************************************************************************//**
*
*  Allows the caller to write data to the DFU host (the host reads the
*  data). The function does not use timeout and returns after data has been
*  copied into the transmit buffer. The data transmission starts immediately
*  after the first data element is written into the buffer and lasts until all
*  data elements from the buffer are sent.
*
*  \param pData: Pointer to the block of data to be written to the DFU
*   host.
*  \param size: Number of bytes to be written.
*  \param count: Pointer to the variable to write the number of bytes actually
*   written.
*  \param timeout: The timeout is not used by this function.
*   The function returns as soon as data is copied into the transmit buffer.
*
*  \return
*   Returns CY_DFU_SUCCESS if no problem was encountered or returns the value
*   that best describes the problem. For more information refer to the
*   \ref cy_en_dfu_status_t description in the API Reference Guide.
*
*******************************************************************************/
cy_en_dfu_status_t CAN_canCyBtldrCommWrite(const uint8_t pData[], uint32_t size, uint32_t* count, uint32_t timeout)
{
    cy_en_dfu_status_t dfuStatus  = CY_DFU_ERROR_BAD_PARAM;
    (void)timeout;

    uint8_t tx_buffer_num = CAN_TX_BUFFER_INDEX_0;
    if ((pData != NULL) && (count != NULL) && (size > 0U))
    {
        *count = 0u;
        /* Transmit data. This function does not wait until data is sent. */
        uint8_t index = 0;
        while(size > 0)
        {
            if(size > MAX_DATABYTE_IN_CAN_FRAME)
            {

                CAN_txBuffer.length  = MAX_DATABYTE_IN_CAN_FRAME;
                CAN_txBuffer.data[0] = BYTEARRAYTOWORD(pData,index);
                CAN_txBuffer.data[1] = BYTEARRAYTOWORD(pData,index + WORD_SIZE_IN_BYTES);
            }
            else
            {
                CAN_txBuffer.length = size;
                if((size) <= WORD_SIZE_IN_BYTES)
                {
                    CAN_txBuffer.data[0] = BYTEARRAYTOWORD(pData,index);
                }
                else
                {
                    CAN_txBuffer.data[0] = BYTEARRAYTOWORD(pData,index);
                    CAN_txBuffer.data[1] = BYTEARRAYTOWORD(pData,index + WORD_SIZE_IN_BYTES);
                }
            }

            if (CY_CAN_SUCCESS == Cy_CAN_Transmit(CY_DFU_CAN_HW,
                                                  tx_buffer_num,
                                                  &CAN_txBuffer,
                                                  false,true,
                                                  &CY_DFU_CAN_CONTEXT))
            {
                index  += CAN_txBuffer.length;
                *count =  index;
                size   -= CAN_txBuffer.length;
                tx_buffer_num++;
                dfuStatus = CY_DFU_SUCCESS;
            }
            else
            {
                dfuStatus = CY_DFU_ERROR_UNKNOWN;
                break;
            }
        }
    }
    return (dfuStatus);
}

/* [] END OF FILE */
