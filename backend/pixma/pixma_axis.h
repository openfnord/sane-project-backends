#ifndef sanei_axis_h
#define sanei_axis_h

#include "../include/sane/config.h"
#include "../include/sane/sane.h"
#include "pixma.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>		/* for size_t */
#endif

/** Initialize sanei_axis.
 *
 * Call this before any other sanei_axis function.
 */
extern void sanei_axis_init (void);

/** Find scanners responding to a AXIS broadcast.
 *
 * The function attach is called for every device which has been found.
 * Serial is the address of the scanner in human readable form of max
 * SHORT_HOSTNAME_MAX characters
 * @param conf_devices list of pre-configures device URI's to attach
 * @param attach attach function
 * @param pixma_devices device informatio needed by attach function
 *
 * @return SANE_STATUS_GOOD - on success (even if no scanner was found)
 */

#define SHORT_HOSTNAME_MAX 16

extern SANE_Status
sanei_axis_find_devices (const char **conf_devices,
                         SANE_Status (*attach_axis)
			 (SANE_String_Const devname,
			  SANE_String_Const makemodel,
			  SANE_String_Const serial,
			  const struct pixma_config_t *
			  const pixma_devices[]),
			 const struct pixma_config_t *const pixma_devices[]);

/** Open a AXIS device.
 *
 * The device is opened by its name devname and the device number is
 * returned in dn on success.
 *
 * Device names consist of an URI
 * Where:
 * method = axis
 * hostname = resolvable name or IP-address
 * port = 8612 for a scanner
 * An example could look like this: axis://host.domain:8612
 *
 * @param devname name of the device to open
 * @param dn device number
 *
 * @return
 * - SANE_STATUS_GOOD - on success
 * - SANE_STATUS_ACCESS_DENIED - if the file couldn't be accessed due to
 *   permissions
 * - SANE_STATUS_INVAL - on every other error
 */
extern SANE_Status sanei_axis_open (SANE_String_Const devname, SANE_Int * dn);

/** Close a AXIS device.
 *
 * @param dn device number
 */

extern void sanei_axis_close (SANE_Int dn);

/** Activate a AXIS device connection
 *
 *  @param dn device number
 */

extern SANE_Status sanei_axis_activate (SANE_Int dn);

/** De-activate a AXIS device connection
 *
 * @param dn device number
 */

extern SANE_Status sanei_axis_deactivate (SANE_Int dn);

/** Set the libaxis timeout for bulk and interrupt reads.
 *
 * @param devno device number
 * @param timeout the new timeout in ms
 */
extern void sanei_axis_set_timeout (SANE_Int devno, SANE_Int timeout);

/** Check if sanei_axis_set_timeout() is available.
 */
#define HAVE_SANEI_AXIS_SET_TIMEOUT

/** Initiate a bulk transfer read.
 *
 * Read up to size bytes from the device to buffer. After the read, size
 * contains the number of bytes actually read.
 *
 * @param dn device number
 * @param buffer buffer to store read data in
 * @param size size of the data
 *
 * @return
 * - SANE_STATUS_GOOD - on succes
 * - SANE_STATUS_EOF - if zero bytes have been read
 * - SANE_STATUS_IO_ERROR - if an error occured during the read
 * - SANE_STATUS_INVAL - on every other error
 *
 */
extern SANE_Status
sanei_axis_read_bulk (SANE_Int dn, SANE_Byte * buffer, size_t * size);

/** Initiate a bulk transfer write.
 *
 * Write up to size bytes from buffer to the device. After the write size
 * contains the number of bytes actually written.
 *
 * @param dn device number
 * @param buffer buffer to write to device
 * @param size size of the data
 *
 * @return
 * - SANE_STATUS_GOOD - on succes
 * - SANE_STATUS_IO_ERROR - if an error occured during the write
 * - SANE_STATUS_INVAL - on every other error
 */
extern SANE_Status
sanei_axis_write_bulk (SANE_Int dn, const SANE_Byte * buffer, size_t * size);

/** Initiate a interrupt transfer read.
 *
 * Read up to size bytes from the interrupt endpoint from the device to
 * buffer. After the read, size contains the number of bytes actually read.
 *
 * @param dn device number
 * @param buffer buffer to store read data in
 * @param size size of the data
 *
 * @return
 * - SANE_STATUS_GOOD - on succes
 * - SANE_STATUS_EOF - if zero bytes have been read
 * - SANE_STATUS_IO_ERROR - if an error occured during the read
 * - SANE_STATUS_INVAL - on every other error
 *
 */

extern SANE_Status
sanei_axis_read_int (SANE_Int dn, SANE_Byte * buffer, size_t * size);

/*------------------------------------------------------*/
#endif /* sanei_axis_h */
