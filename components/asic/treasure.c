#include <string.h>
#include "treasure.h"
#include "serial.h"
#include "utils.h"
#include "esp_log.h"
#include "esp_crc.h"

static const char *TAG = "TREASURE";

#define TREASURE_CHIP_ID 0x0000
#define DEFAULT_BAUD 115200

#define MAX_MESSAGE_SIZE 64
#define MAX_RESPONSE_SIZE MAX_MESSAGE_SIZE

// CHIP_ID
#define ID_BIT_MASK 0x00FF
#define SPARE_BIT_MASK 0x0300

// Commands
#define CMD_NOTHING 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_READWRITE 0x03
#define CMD_LOAD0 0x04
#define CMD_LOAD1 0x05
#define CMD_LOAD2 0x06
#define CMD_LOAD3 0x07
#define CMD_RETURNHIT 0x40
#define CMD_BROADCAST 0x80

// WTF?
#define CMD_UNIQUE 0x12345678
#define RSP_UNIQUE 0xdac07654
#define HIT_UNIQUE RSP_UNIQUE 

// Registers
#define CHIP_UNIQUE 0x00
#define CHIP_REVISION 0x01
#define ASICID 0x02

int _serial_send_data(const uint8_t *data, size_t data_length)
{
	uint8_t message[MAX_MESSAGE_SIZE] = {0};
	size_t message_length = data_length;
	uint32_t message_crc = 0;

	if (data_length > MAX_MESSAGE_SIZE) {
		ESP_LOGE(TAG, "Message length exceeded maximum expected size");
		return -1;
	}
	if (message_length + sizeof(message_crc) > MAX_MESSAGE_SIZE) {
		ESP_LOGE(TAG, "Message length exceeded maximum expected size after adding CRC");
		return -2;
	}
	memcpy(message, data, data_length);

	message_crc = esp_crc32_le(0, message, message_length);
	message_crc = message_crc ^ 0xFFFFFFFF;
	memcpy(&message[message_length], &message_crc, sizeof(message_crc));
	message_length = message_length + sizeof(message_crc);

	return SERIAL_send(message, message_length, TREASURE_SERIALTX_DEBUG);
}

void _read_register(uint16_t asic_id, uint8_t register_name, bool broadcast)
{
	uint8_t command = CMD_READ;
	uint8_t spare = (uint8_t)((asic_id & SPARE_BIT_MASK) >> 8);
	uint8_t base_asic_id = (uint8_t)(asic_id & ID_BIT_MASK);

	if(broadcast){
		command |= CMD_BROADCAST;
		asic_id = 0;
		spare = 0;
	}
	uint8_t data[] = {0x78, 0x56, 0x34, 0x12, base_asic_id, command, spare, register_name, 0x00, 0x00, 0x00, 0x00};	
	_serial_send_data(data, sizeof(data));

	uint8_t response[MAX_RESPONSE_SIZE] = {0};
	int amount_read = SERIAL_rx(response, sizeof(response), 100);
	if (amount_read <= 0) {
		ESP_LOGE(TAG, "An error occured getting message from asic");
	}

	#if TREASURE_SERIALRX_DEBUG
	ESP_LOGI(TAG,"Asic RX:");
	printf("rx: ");
	prettyHex(response, MAX_RESPONSE_SIZE);
        printf("\n");
	#endif

}

uint8_t TREASURE_init(float frequency, uint16_t asic_count, uint16_t difficulty)
{
	uint8_t chip_counter = 0; 

	// Setup Baudrate
	SERIAL_set_baud(DEFAULT_BAUD);
	//SERIAL_set_baud(921600);

	// Check chips
	_read_register(0x0302, ASICID, false);
	//for(uint16_t x = 0x0300; x <= 0x03FF; x++) {
	//	ESP_LOGI(TAG, "Starting transmit: %x", x);
	//	_read_register(x, ASICID, false);
	//}

	//for(uint8_t x = 0; x < 100; x++) {
	//	ESP_LOGI(TAG, "Starting transmit: %x", x);
	//	_read_register(0, ASICID, false);
	//}

	return chip_counter;
}

void TREASURE_read_registers(void)
{
}
