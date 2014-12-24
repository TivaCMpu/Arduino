/*
  Copyright (c) 2011 Arduino.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "UARTClass.h"

// Constructors ////////////////////////////////////////////////////////////////

UARTClass::UARTClass( Uart *pUart, IRQn_Type dwIrq, uint32_t dwId, RingBuffer *pRx_buffer, RingBuffer *pTx_buffer )
{
  _rx_buffer = pRx_buffer ;
  _tx_buffer = pTx_buffer;

  _pUart=pUart ;
  _dwIrq=dwIrq ;
  _dwId=dwId ;
}

// Public Methods //////////////////////////////////////////////////////////////



void UARTClass::begin( const uint32_t dwBaudRate )
{
	begin( dwBaudRate, UART_MR_PAR_NO | UART_MR_CHMODE_NORMAL );
}

void UARTClass::begin( const uint32_t dwBaudRate, const uint32_t config )
{
  // Configure PMC
  pmc_enable_periph_clk( _dwId ) ;

  // Disable PDC channel
  _pUart->UART_PTCR = UART_PTCR_RXTDIS | UART_PTCR_TXTDIS ;

  // Reset and disable receiver and transmitter
  _pUart->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS ;

  // Configure mode
  _pUart->UART_MR = config ;

  // Configure baudrate (asynchronous, no oversampling)
  _pUart->UART_BRGR = (SystemCoreClock / dwBaudRate) >> 4 ;

  // Configure interrupts
  _pUart->UART_IDR = 0xFFFFFFFF;
  _pUart->UART_IER = UART_IER_RXRDY | UART_IER_OVRE | UART_IER_FRAME;

  // Enable UART interrupt in NVIC
  NVIC_EnableIRQ(_dwIrq);

  //make sure both ring buffers are initialized back to empty.
  _rx_buffer->_iHead = _rx_buffer->_iTail = 0;
  _tx_buffer->_iHead = _tx_buffer->_iTail = 0;

  // Enable receiver and transmitter
  _pUart->UART_CR = UART_CR_RXEN | UART_CR_TXEN ;
}

void UARTClass::end( void )
{
  // clear any received data
  _rx_buffer->_iHead = _rx_buffer->_iTail ;

  while (_tx_buffer->_iHead != _tx_buffer->_iTail); //wait for transmit data to be sent

  // Disable UART interrupt in NVIC
  NVIC_DisableIRQ( _dwIrq ) ;

  // Wait for any outstanding data to be sent
  flush();

  pmc_disable_periph_clk( _dwId ) ;
}

int UARTClass::available( void )
{
  return (uint32_t)(SERIAL_BUFFER_SIZE + _rx_buffer->_iHead - _rx_buffer->_iTail) % SERIAL_BUFFER_SIZE ;
}

int UARTClass::availableForWrite(void)
{
  int head = _tx_buffer->_iHead;
  int tail = _tx_buffer->_iTail;
  if (head >= tail) return SERIAL_BUFFER_SIZE - 1 - head + tail;
  return tail - head - 1;
}

int UARTClass::peek( void )
{
  if ( _rx_buffer->_iHead == _rx_buffer->_iTail )
    return -1 ;

  return _rx_buffer->_aucBuffer[_rx_buffer->_iTail] ;
}

int UARTClass::read( void )
{
  // if the head isn't ahead of the tail, we don't have any characters
  if ( _rx_buffer->_iHead == _rx_buffer->_iTail )
    return -1 ;

  uint8_t uc = _rx_buffer->_aucBuffer[_rx_buffer->_iTail] ;
  _rx_buffer->_iTail = (unsigned int)(_rx_buffer->_iTail + 1) % SERIAL_BUFFER_SIZE ;
  return uc ;
}

void UARTClass::flush( void )
{
  // Wait for transmission to complete
  while ((_pUart->UART_SR & UART_SR_TXRDY) != UART_SR_TXRDY)
    ;
}

size_t UARTClass::write( const uint8_t uc_data )
{
  if ((_pUart->UART_SR & UART_SR_TXRDY) != UART_SR_TXRDY) //is the hardware currently busy?
  {
	  //if busy we buffer
	  unsigned int l = (_tx_buffer->_iHead + 1) % SERIAL_BUFFER_SIZE;
	  while (_tx_buffer->_iTail == l); //spin locks if we're about to overwrite the buffer. This continues once the data is sent

	  _tx_buffer->_aucBuffer[_tx_buffer->_iHead] = uc_data;
	  _tx_buffer->_iHead = l;
	  _pUart->UART_IER = UART_IER_TXRDY; //make sure TX interrupt is enabled
  }
  else 
  {
     // Send character
     _pUart->UART_THR = uc_data ;
  }
  return 1;
}

void UARTClass::IrqHandler( void )
{
  uint32_t status = _pUart->UART_SR;

  // Did we receive data ?
  if ((status & UART_SR_RXRDY) == UART_SR_RXRDY)
    _rx_buffer->store_char(_pUart->UART_RHR);

  //Do we need to keep sending data?
  if ((status & UART_SR_TXRDY) == UART_SR_TXRDY) 
  {
	  if (_tx_buffer->_iTail != _tx_buffer->_iHead) { //just in case
		_pUart->UART_THR = _tx_buffer->_aucBuffer[_tx_buffer->_iTail];
		_tx_buffer->_iTail = (unsigned int)(_tx_buffer->_iTail + 1) % SERIAL_BUFFER_SIZE;
	  }
	  else
	  {
		_pUart->UART_IDR = UART_IDR_TXRDY; //mask off transmit interrupt so we don't get it anymore
	  }
  }

  // Acknowledge errors
  if ((status & UART_SR_OVRE) == UART_SR_OVRE ||
		  (status & UART_SR_FRAME) == UART_SR_FRAME)
  {
	// TODO: error reporting outside ISR
    _pUart->UART_CR |= UART_CR_RSTSTA;
  }
}

