/*
 * Nucleo_BS_ACK.c
 *
 * Created: 12-Dec-18 8:41:12 PM
 * Author : Frederic Philips
 */ 
#include "mbed.h"
#include "nrf.h"

#define TRANSMISSON_OK  0
#define MESSAGE_LOST    1

#define NRF_ADDR_LEN    5
#define PAYLOAD_LEN     10

//0 - TX; 1 - RX
volatile uint8_t mode;

//SPI
SPI spi(D11, D12, D13); //MOSI, MISO, SCLK
DigitalOut csn(D4);     //Chip Select
DigitalOut ce(D5);      //Chip Enable

//IRQ
//InterruptIn nrf_irq(D6);

//PC_UART
Serial pc(SERIAL_TX, SERIAL_RX);


uint8_t BS_address_TX[NRF_ADDR_LEN] = {0x11, 0x12, 0x13, 0x14, 0x15};
uint8_t BS_address_RX[NRF_ADDR_LEN] = {0x11, 0x12, 0x13, 0x14, 0x15};


/*
uint8_t BS_address_TX[NRF_ADDR_LEN] = {0x21, 0x22, 0x23, 0x24, 0x25};
uint8_t BS_address_RX[NRF_ADDR_LEN] = {0x21, 0x22, 0x23, 0x24, 0x25};
*/
//uint8_t BS_address_RX[NRF_ADDR_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};


uint8_t BS_payload_TX[PAYLOAD_LEN] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
uint8_t BS_payload_RX[PAYLOAD_LEN];

void nRF_Set_Addr_TX(uint8_t *addrData, uint8_t addrLen);
void nRF_Set_Addr_RX(uint8_t *addrData, uint8_t addrLen);
void nRF_TX_Mode(void);
void nRF_RX_Mode(void);
void nRF_Flush_TX(void);
void nRF_Flush_RX(void);
void nRF_Reset(void);
void nRF_get_Payload(uint8_t *data_out, uint8_t *data_in, uint8_t len);
uint8_t nRF_get_Status(void);
uint8_t nRF_is_Sending(void);
uint8_t nRF_RX_FIFO_Empty(void);

/************************************************************************************
** Nucleo_Init function:
** - Start-up delay
** - Initializes the I/O peripherals
*************************************************************************************/
void Nucleo_Init(void)
{
    wait(0.75);         //750ms mandatory delay for BNO055 POR
}

/************************************************************************************
** PC_UART_Init function:
** - 115200 baud, 8-bit data, 1 stop bit, no parity
*************************************************************************************/
void PC_UART_Init(void)
{
    pc.baud(9600);
}

/************************************************************************************
** SPI_Init function:
** - 1MHz
** - Mode 0: ClockPhase = 0, ClockPolarity = 0
*************************************************************************************/
void SPI_Init(void)
{
    spi.format(8,0);    //8 bit data, Mode 0
    spi.frequency(1000000);
    
    csn = 1;            //CSN high
    ce = 0;             //CE low
}

unsigned char SPI_Read_Byte(unsigned char reg)
{
    wait_us(10);
    csn = 0;            //CSN low
    wait_us(10);
    spi.write(R_REGISTER + reg);
    wait_us(10);
    reg = spi.write(NOP);
    wait_us(10);
    csn = 1;            //CSN high
//    pc.printf("%x", reg);
    return reg;
}

void SPI_Write_Byte(unsigned char reg, unsigned char data)
{
    wait_us(10);
    csn = 0;            //CSN low
    wait_us(10);
    spi.write(W_REGISTER + reg);
    wait_us(10);
    spi.write(data);
    wait_us(10);
    csn = 1;            //CSN high
}

void nRF_Init(void)
{
    //Enable auto-acknowledgment for data pipe 0
    SPI_Write_Byte(EN_AA, 0x01);
    
    //Enable data pipe 0
    SPI_Write_Byte(EN_RXADDR, 0x01);

    //Set address width to 5 bytes
    SPI_Write_Byte(SETUP_AW, 0x03);
    
    //Set channel frequency to 2.505GHz
    SPI_Write_Byte(RF_CH, 0x69);
    
    //Set data rate to 2Mbps and 0dB gain
//    SPI_Write_Byte(RF_SETUP, 0x0E);

    //Set data rate to 250kbps and 0dB gain
    SPI_Write_Byte(RF_SETUP, 0x26);
    
    //Enable W_TX_PAYLOAD_NOACK command
//  SPI_Write_Byte(FEATURE, 0x01);
    
    //Set the 5-bytes receiver address as 0x21 0x22 0x23 0x24 0x25
    nRF_Set_Addr_RX(BS_address_RX, NRF_ADDR_LEN);

    //Set the 5-bytes transmitter address as 0xAA 0xBB 0xCC 0xDD 0xEE
    nRF_Set_Addr_TX(BS_address_TX, NRF_ADDR_LEN);
    
    //Set the payload width as 8-bytes
    SPI_Write_Byte(RX_PW_P0, 0x08);
    
    //Set the retransmission delay to 750us with 15 retries
    SPI_Write_Byte(SETUP_RETR, 0xFF);
    
    //Boot the nrf as TX and mask the maximum retransmission interrupt(disable)
    //Enable CRC and set the length to 2-bytes
    nRF_TX_Mode();
    
    wait_ms(10);        //10ms delay after power-up
}

void nRF_Set_Addr_RX(uint8_t *addrData, uint8_t addrLen)
{
    uint8_t i;

    wait_us(10);
    csn = 0;            //CSN low
    wait_us(10);
    //Setup p0 pipe address for receiving
    spi.write(W_REGISTER + RX_ADDR_P0);

    for(i = 0; i < addrLen; i++)
    {
        wait_us(10);
        spi.write(BS_address_RX[i]);
    }
    wait_us(10);
    csn = 1;            //CSN high
}

void nRF_Set_Addr_TX(uint8_t *addrData, uint8_t addrLen)
{
    uint8_t i;

    wait_us(10);
    csn = 0;            //CSN low
    wait_us(10);
    //Setup p0 pipe address for receiving
    spi.write(W_REGISTER + TX_ADDR);

    for(i = 0; i < addrLen; i++)
    {
        wait_us(10);
        spi.write(BS_address_TX[i]);
    }
    wait_us(10);
    csn = 1;            //CSN high
}

void nRF_TX_Mode(void)
{
    ce = 0;                             //CE low - Standby-I                         
    //Power-up and set as TX
    SPI_Write_Byte(CONFIG, SPI_Read_Byte(CONFIG) & ~(1 << PRIM_RX));
    SPI_Write_Byte(CONFIG, SPI_Read_Byte(CONFIG) | (1 << PWR_UP));
    nRF_Flush_TX();                     //Flush TX FIFO
    SPI_Write_Byte(STATUS, (1 << RX_DR) | (1 << TX_DS) | (1 << MAX_RT)); //Reset status
    //Mask TX_DR and MAX_RT interrupts
    SPI_Write_Byte(CONFIG, SPI_Read_Byte(CONFIG) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT));
    wait_us(150);
}

void nRF_RX_Mode(void)
{
    ce = 0;                             //CE low - Standby-I
    //Power-up as set as RX
    SPI_Write_Byte(CONFIG, SPI_Read_Byte(CONFIG) | (1 << PWR_UP) | (1 << PRIM_RX));
    nRF_Flush_RX();                     //Flush RX FIFO
    SPI_Write_Byte(STATUS, (1 << RX_DR) | (1 << TX_DS) | (1 << MAX_RT)); //Reset status
    //Mask TX_DR and MAX_RT interrupts
    SPI_Write_Byte(CONFIG, SPI_Read_Byte(CONFIG) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT));
    ce = 1;                             //CE high
    wait_us(150);
}

void nRF_Flush_TX(void)
{
    wait_us(10);
    csn = 0;            //CSN low
    wait_us(10);
    spi.write(FLUSH_TX);
    wait_us(10);
    csn = 1;            //CSN high
    wait_us(10);
}

void nRF_Flush_RX(void)
{
    wait_us(10);
    csn = 0;            //CSN low
    wait_us(10);
    spi.write(FLUSH_RX);
    wait_us(10);
    csn = 1;            //CSN high
    wait_us(10);
}

void nRF_send_Payload(uint8_t* data, uint8_t len)
{
    uint8_t i;
    
    for(i = 0; i < len; i++)
    {
        spi.write(BS_payload_TX[i]);
 //       pc.printf("%x", BS_payload_TX[i]);
    }
}

void nRF_TX_Data(unsigned char *tdata)
{
    nRF_Flush_TX();
    csn = 0;            //CSN low
    wait_us(10);
    //Transmit payload with ACK enabled
    spi.write(W_TX_PAYLOAD);
    wait_us(10);
    nRF_send_Payload(BS_payload_TX, PAYLOAD_LEN);
    wait_us(10);
    csn = 1;            //CSN high
    wait_us(10);        //Need at least 10us before sending
    ce = 1;             //CE high
    wait_us(10);        //Hold CE high for at least 10us and not longer than 4ms
    ce = 0;             //CE low
}

uint8_t nRF_get_Status()
{
    uint8_t rv;
    csn = 0;            //CSN low
    rv = spi.write(NOP);
    csn = 1;            //CSN high
    return rv;
}

uint8_t nRF_is_Sending()
{
    uint8_t status;

    /* read the current status */
    status = nRF_get_Status();
//    pc.printf("Status:%x\n", status);
    
    /* if sending successful (TX_DS) or max retries exceeded (MAX_RT). */
    if((status & ((1 << TX_DS)  | (1 << MAX_RT))))
    {
        return 0; /* false */
    }
    return 1; /* true */
}

uint8_t nRF_send_Status()
{
    uint8_t rv;

    rv = nRF_get_Status();

    /* Transmission went OK */
    if((rv & ((1 << TX_DS))))
    {
        return TRANSMISSON_OK;
    }
    
    /* Maximum retransmission count is reached */
    /* Last message probably went missing ... */
    else if((rv & ((1 << MAX_RT))))
    {
        return MESSAGE_LOST;
    }
    
    /* Probably still sending ... */
    else
    {
        return 0xFF;
    }
}

/* Returns the number of retransmissions occurred for the last message */
uint8_t nRF_RT_Count(void)
{
    uint8_t rv;
    rv = SPI_Read_Byte(OBSERVE_TX);
    rv = rv & 0x0F;
    return rv;
}

void nRF_RX_Data(void)
{
 //   pc.printf("BP4\n");
    ce = 0;                     //Stop listening
    // Pull down chip select 
    csn = 0;                    //CSN low
    wait_us(10);
    // Send command to read RX payload 
    spi.write(R_RX_PAYLOAD);
    wait_us(10);
    // Read payload 
    nRF_get_Payload(BS_payload_RX, BS_payload_RX, PAYLOAD_LEN);
    wait_us(10);
    // Pull up chip select
    csn = 1;                    //CSN high
    wait_us(10);
    // Reset status register 
    SPI_Write_Byte(STATUS, (1 << RX_DR));
    mode = 0;                   //Set as TX 
}

void nRF_get_Payload(uint8_t *data_out, uint8_t *data_in, uint8_t len)
{
    uint8_t i;

    for(i = 0; i < len; i++)
    {
        data_in[i] = spi.write(data_out[i]);
        if (pc.writeable()) 
        {
            pc.printf("%x", data_in[i]);    //Send the received data to UART
        }
    }
    if (pc.writeable()) 
    {
        pc.printf("\n");
    }
}

/* Checks if data is available for reading */
/* Returns 1 if data is ready ... */
uint8_t nRF_data_Ready()
{
    // See note in getData() function - just checking RX_DR isn't good enough
    uint8_t status = nRF_get_Status();
    uint8_t FIFO_Status;

    // We can short circuit on RX_DR, but if it's not set, we still need
    // to check the FIFO for any pending packets
    if (status & (1 << RX_DR))
    {
        return 1;
    }
    
    FIFO_Status = nRF_RX_FIFO_Empty();

    return !FIFO_Status;
}

/* Checks if receive FIFO is empty or not */
uint8_t nRF_RX_FIFO_Empty()
{
    uint8_t FIFO_Status;

    FIFO_Status = SPI_Read_Byte(FIFO_STATUS);

    return (FIFO_Status & (1 << RX_EMPTY));
}

void nRF_Reset(void)
{
    wait_us(10);
    //Reset IRQ-flags in status register
    SPI_Write_Byte(STATUS, 0x70);   
    wait_us(10);
}

int main() 
{
    Nucleo_Init();
    SPI_Init();
    nRF_Init();
    PC_UART_Init();
    
    //0 - TX; 1 - RX
    mode = 0;  
    
    //Loop forever
    while(1)
    {
        if(mode == 1)
        {
            if(nRF_data_Ready())
            {
                nRF_RX_Data();            
            }
        }

        else if(mode == 0)               //Check if TX
        {
//            if (pc.writeable()) 
//            {
//                pc.printf("BP1\n");
//            }
            
            //Configure as Transmitter
            nRF_TX_Mode();
//            wait_ms(10);
        
            nRF_TX_Data(BS_payload_TX);
            while(nRF_is_Sending());

            /* Make analysis on last transmission attempt */
            uint8_t TX_Status = nRF_send_Status();

            if(TX_Status == TRANSMISSON_OK)
            {
                if (pc.writeable()) 
                {
                    pc.printf("Transmission went OK\n");
                }
            }
        
            else if(TX_Status == MESSAGE_LOST)
            {
                if (pc.writeable()) 
                {
                    pc.printf("Message is lost!\n");
                }
            }
        
            /* Retransmission count indicates the transmission quality */
            TX_Status = nRF_RT_Count();
            if (pc.writeable()) 
            {            
                pc.printf("Retransmission count: %d\n", TX_Status);
            }
//            nRF_Reset();
            
//            pc.printf("BP2\n");
            
            //Configure as Receiver
            mode = 1;               //Set as RX
            nRF_RX_Mode();
            nRF_Flush_RX();
            nRF_Reset();
            ce = 1;                 //Start listening again 
//            nrf_irq.enable_irq();   //Enable nRF IRQ    

//            pc.printf("BP3\n");
        }
    }
}