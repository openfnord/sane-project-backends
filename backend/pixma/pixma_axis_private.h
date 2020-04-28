#include "../include/sane/sanei_debug.h"
#define LOG_CRIT 0
#define LOG_NOTICE 1
#define LOG_INFO 2
#define LOG_DEBUG 3
#define LOG_DEBUG2 4
#define LOG_DEBUG3 5

#define AXIS_NO_DEVICES 16

#define AXIS_WIMP_PORT 		10260	/* UDP port for discovery */

#define WIMP_SERVER_INFO	0x24
#define WIMP_SERVER_STATUS	0x30
#define WIMP_REPLY		(1 << 0)
struct axis_wimp_header {
	uint8_t type;			/* 0x24, 0x30 = request, 0x25, 0x31 = reply */
	uint8_t magic;			/* 0x03 */
	uint8_t zero;
} __attribute__((__packed__));

#define WIMP_GET_NAME		0x02
#define WIMP_GET_STATUS		0x03
struct axis_wimp_get {
	uint16_t port;
	uint8_t magic;			/* 0x02 */
	uint8_t zero;
	uint8_t cmd;			/* 0x02 = device name, 0x03 = device status */
	uint8_t idx;			/* 0x01 = first, 0x02 = second */
} __attribute__((__packed__));

#define ETHER_ADDR_LEN 6
struct axis_wimp_server_info {
	uint8_t mac[ETHER_ADDR_LEN];	/* print server MAC address */
	char name[16];			/* print server name (max. 15 chars), zero-terminated */
	uint8_t unknown2[67];
} __attribute__((__packed__));

struct axis_wimp_get_reply {
	uint16_t len;
	uint16_t unknown;
} __attribute__((__packed__));

#define AXIS_SERIAL_LEN		32	/* arbitrary limit */
#define AXIS_USERNAME_LEN	32	/* arbitrary limit */

#define AXIS_SCAN_PORT		49152	/* TCP port for scan data */

#define AXIS_HDR_REQUEST	0x27
#define AXIS_HDR_REPLY		0x28

struct axis_header {
	uint8_t type;
	uint32_t len;	/* little endian */
} __attribute__((__packed__));

#define AXIS_CMD_READ		0x01
#define AXIS_CMD_WRITE		0x02
#define AXIS_CMD_UNKNOWN	0x03	/* ??? */
#define AXIS_CMD_UNKNOWN2	0x04	/* interrupt ??? only seen as reply */
#define AXIS_CMD_CONNECT	0x10
#define AXIS_CMD_DISCONNECT	0x11
#define AXIS_CMD_UNKNOWN3	0x12	/* ??? */

struct axis_cmd {
	uint8_t cmd;
	uint32_t len;	/* little endian */
} __attribute__((__packed__));

struct axis_reply {
	uint8_t cmd;
	uint32_t status;/* little endian */
	uint32_t len;	/* little endian */
}__attribute__((__packed__));

/*
 * Device information for opened devices
 */

typedef struct device_s
{
  int open;			/* connection to scanner is opened */
  int tcp_socket;		/* open tcp socket for communcation to scannner */
  /* device information */
  struct in_addr addr;		/* IP address of the scanner */
  int int_size;			/* size of interrupt data */
  uint8_t int_data[16];		/* interrupt data */
} axis_device_t;
