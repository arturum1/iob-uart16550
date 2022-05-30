#include <stdint.h>
#include "iob-uart.h"

//TX FUNCTIONS
void uart_txwait() {
    uint8_t status = 0;
    status = *((volatile uint8_t *)(base + 5));
    while(!(status & (1<<6))){
      status = *((volatile uint8_t *)(base + 5));
    }
}

void uart_putc(char c) {
    uart_txwait();
    *((volatile uint8_t *)(base)) = c;
}

//RX FUNCTIONS
void uart_rxwait() {
    uint8_t status = 0;
    status = *((volatile uint8_t *)(base + 5));
    while(!(status & (1))){
      status = *((volatile uint8_t *)(base + 5));
    }
}

char uart_getc() {
    uart_rxwait();
    uint8_t rvalue;
    rvalue = *((volatile uint8_t *)(base));
    return rvalue;
}

//UART basic functions
void uart_init(int base_address, uint16_t div) {
  //capture base address for good
	base = base_address;

  // Set the Line Control Register to the desired line control parameters.
  // Set bit 7 to ‘1’ to allow access to the Divisor Latches.
  uint8_t lcr = 0;
  lcr = *((volatile uint8_t *)(base + 3));
  lcr = (lcr|128);
  *((volatile uint8_t *)(base + 3)) = lcr;

  // Set the Divisor Latches, MSB first, LSB next.
  uint8_t *dl = (uint8_t *)&div;
  *((volatile uint8_t *)(base + 1)) = *(dl+1);
  *((volatile uint8_t *)(base)) = *(dl);

  // Set bit 7 of LCR to ‘0’ to disable access to Divisor Latches.
  // At this time the transmission engine starts working and data can be sent and received.
  lcr = (lcr&127);
  *((volatile uint8_t *)(base + 3)) = lcr;

  // Set the FIFO trigger level. Generally, higher trigger level values produce less
  // interrupt to the system, so setting it to 14 bytes is recommended if the system
  // responds fast enough.
  *((volatile uint8_t *)(base + 2)) = 192;

  // Enable desired interrupts by setting appropriate bits in the Interrupt Enable
  // register.
  *((volatile uint8_t *)(base + 1)) = 3;

}

void uart_finish() {
  uart_putc(EOT);
  uart_txwait();
}

//Print string, excluding end of string (0)
void uart_puts(const char *s) {
  while (*s) uart_putc(*s++);
}

//Sends the name of the file to use, including end of string (0)
void uart_sendstr (char* name) {
  int i=0;
  do
    uart_putc(name[i]);
  while (name[i++]);
}

//Receives file into mem
int uart_recvfile(char* file_name, char **mem) {

  uart_puts(UART_PROGNAME);
  uart_puts (": requesting to receive file\n");

  //send file receive request
  uart_putc (FRX);

  //send file name
  uart_sendstr(file_name);


  //receive file size
  int file_size = (unsigned int) uart_getc();
  file_size |= ((unsigned int) uart_getc()) << 8;
  file_size |= ((unsigned int) uart_getc()) << 16;
  file_size |= ((unsigned int) uart_getc()) << 24;

  //allocate space for file if file pointer not initialized
  if((*mem) == NULL) {
    (*mem) = (char *) malloc(file_size);
    if ((*mem) == NULL) {
      uart_puts(UART_PROGNAME);
      uart_puts("Error: malloc failed");
    }
  }

  //send ACK before receiving file
  uart_putc(ACK);

  //write file to memory
  for (int i = 0; i < file_size; i++) {
    (*mem)[i] = uart_getc();
  }

  uart_puts(UART_PROGNAME);
  uart_puts(": file received\n");

  return file_size;
}

//Sends mem contents to a file
void uart_sendfile(char *file_name, int file_size, char *mem) {

  uart_puts(UART_PROGNAME);
  uart_puts(": requesting to send file\n");

  //send file transmit command
  uart_putc(FTX);

  //send file name
  uart_sendstr(file_name);

  // send file size
  uart_putc((char)(file_size & 0x0ff));
  uart_putc((char)((file_size & 0x0ff00) >> 8));
  uart_putc((char)((file_size & 0x0ff0000) >> 16));
  uart_putc((char)((file_size & 0x0ff000000) >> 24));

  // send file contents
  for (int i = 0; i < file_size; i++)
    uart_putc(mem[i]);

  uart_puts(UART_PROGNAME);
  uart_puts(": file sent\n");
}
