#include "usb.h"

#define USB_MAXALTSETTING		128	/* Hard limit */
#define USB_MAXENDPOINTS		30	/* Hard limit */

#define USB_MAXCONFIG			8	/* Arbitrary limit */

static int find_next_descriptor(unsigned char *buffer, int size,
    int dt1, int dt2, int *num_skipped)
{
	struct usb_descriptor_header *h;
	int n = 0;
	unsigned char *buffer0 = buffer;

	/* Find the next descriptor of type dt1 or dt2 */
	while (size > 0) {
		h = (struct usb_descriptor_header *) buffer;
		if (h->bDescriptorType == dt1 || h->bDescriptorType == dt2)
			break;
		buffer += h->bLength;
		size -= h->bLength;
		++n;
	}

	/* Store the number of descriptors skipped and return the
	 * number of bytes skipped */
	if (num_skipped)
		*num_skipped = n;
	return buffer - buffer0;
}

static int usb_parse_endpoint(struct usb_device *dev, int cfgno, int inum,
    int asnum, struct usb_host_interface *ifp, int num_ep,
    unsigned char *buffer, int size)
{
	unsigned char *buffer0 = buffer;
	struct usb_endpoint_descriptor *d;
	struct usb_host_endpoint *endpoint;
	int n, i, j;

	d = (struct usb_endpoint_descriptor *) buffer;
	buffer += d->bLength;
	size -= d->bLength;

	if (d->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE)
		n = USB_DT_ENDPOINT_AUDIO_SIZE;
	else if (d->bLength >= USB_DT_ENDPOINT_SIZE)
		n = USB_DT_ENDPOINT_SIZE;
	else {
		printf("devnum %d: config %d interface %d altsetting %d has an "
		    "invalid endpoint descriptor of length %d, skipping\n",
		    dev->devnum, cfgno, inum, asnum, d->bLength);
		goto skip_to_next_endpoint_or_interface_descriptor;
	}

	i = d->bEndpointAddress & ~USB_ENDPOINT_DIR_MASK;
	if (i >= 16 || i == 0) {
		printf("devnum %d: config %d interface %d altsetting %d has an "
		    "invalid endpoint with address 0x%X, skipping\n",
		    dev->devnum, cfgno, inum, asnum, d->bEndpointAddress);
		goto skip_to_next_endpoint_or_interface_descriptor;
	}

	/* Only store as many endpoints as we have room for */
	if (ifp->desc.bNumEndpoints >= num_ep)
		goto skip_to_next_endpoint_or_interface_descriptor;

	endpoint = &ifp->endpoint[ifp->desc.bNumEndpoints];
	++ifp->desc.bNumEndpoints;

	memcpy(&endpoint->desc, d, n);
	INIT_LIST_HEAD(&endpoint->urb_list);

	/* Fix up bInterval values outside the legal range. Use 32 ms if no
	 * proper value can be guessed. */
	i = 0;		/* i = min, j = max, n = default */
	j = 255;
	if (usb_endpoint_xfer_int(d)) {
		i = 1;
		switch (dev->speed) {
		case USB_SPEED_HIGH:
			/* Many device manufacturers are using full-speed
			 * bInterval values in high-speed interrupt endpoint
			 * descriptors. Try to fix those and fall back to a
			 * 32 ms default value otherwise. */
			n = fls(d->bInterval*8);
			if (n == 0)
				n = 9;	/* 32 ms = 2^(9-1) uframes */
			j = 16;
			break;
		default:		/* USB_SPEED_FULL or _LOW */
			/* For low-speed, 10 ms is the official minimum.
			 * But some "overclocked" devices might want faster
			 * polling so we'll allow it. */
			n = 32;
			break;
		}
	} else if (usb_endpoint_xfer_isoc(d)) {
		i = 1;
		j = 16;
		switch (dev->speed) {
		case USB_SPEED_HIGH:
			n = 9;		/* 32 ms = 2^(9-1) uframes */
			break;
		default:		/* USB_SPEED_FULL */
			n = 6;		/* 32 ms = 2^(6-1) frames */
			break;
		}
	}
	if (d->bInterval < i || d->bInterval > j) {
		printf("devnum %d: config %d interface %d altsetting %d "
		    "endpoint 0x%X has an invalid bInterval %d, "
		    "changing to %d\n",
		    dev->devnum, cfgno, inum, asnum,
		    d->bEndpointAddress, d->bInterval, n);
		endpoint->desc.bInterval = n;
	}

	/* Some buggy low-speed devices have Bulk endpoints, which is
	 * explicitly forbidden by the USB spec.  In an attempt to make
	 * them usable, we will try treating them as Interrupt endpoints.
	 */
	if (dev->speed == USB_SPEED_LOW &&
			usb_endpoint_xfer_bulk(d)) {
		printf("devnum %d: config %d interface %d altsetting %d "
		    "endpoint 0x%X is Bulk; changing to Interrupt\n",
		    dev->devnum, cfgno, inum, asnum, d->bEndpointAddress);
		endpoint->desc.bmAttributes = USB_ENDPOINT_XFER_INT;
		endpoint->desc.bInterval = 1;
		if (endpoint->desc.wMaxPacketSize > 8)
			endpoint->desc.wMaxPacketSize = 8;
	}

	/*
	 * Some buggy high speed devices have bulk endpoints using
	 * maxpacket sizes other than 512.  High speed HCDs may not
	 * be able to handle that particular bug, so let's warn...
	 */
	if (dev->speed == USB_SPEED_HIGH
			&& usb_endpoint_xfer_bulk(d)) {
		unsigned maxp;

		maxp = endpoint->desc.wMaxPacketSize & 0x07ff;
		if (maxp != 512)
			printf("devnum %d: config %d interface %d altsetting %d "
				"bulk endpoint 0x%X has invalid maxpacket %d\n",
				dev->devnum, cfgno, inum, asnum, d->bEndpointAddress, maxp);
	}

	/* Skip over any Class Specific or Vendor Specific descriptors;
	 * find the next endpoint or interface descriptor */
	endpoint->extra = buffer;
	i = find_next_descriptor(buffer, size, USB_DT_ENDPOINT,
	    USB_DT_INTERFACE, &n);
	endpoint->extralen = i;
	return buffer - buffer0 + i;

skip_to_next_endpoint_or_interface_descriptor:
	i = find_next_descriptor(buffer, size, USB_DT_ENDPOINT,
	    USB_DT_INTERFACE, NULL);
	return buffer - buffer0 + i;
}

void usb_release_interface_cache(struct usb_interface_cache *intfc)
{
	int j;

	for (j = 0; j < intfc->num_altsetting; j++)
		free(intfc->altsetting[j].endpoint);
	free(intfc);
}

static int usb_parse_interface(struct usb_device *dev, int cfgno,
    struct usb_host_config *config, unsigned char *buffer, int size,
    u8 inums[], u8 nalts[])
{
	unsigned char *buffer0 = buffer;
	struct usb_interface_descriptor	*d;
	int inum, asnum;
	struct usb_interface_cache *intfc;
	struct usb_host_interface *alt;
	int i, n;
	int len, retval;
	int num_ep, num_ep_orig;

	d = (struct usb_interface_descriptor *) buffer;
	buffer += d->bLength;
	size -= d->bLength;

	if (d->bLength < USB_DT_INTERFACE_SIZE)
		goto skip_to_next_interface_descriptor;

	/* Which interface entry is this? */
	intfc = NULL;
	inum = d->bInterfaceNumber;
	for (i = 0; i < config->desc.bNumInterfaces; ++i) {
		if (inums[i] == inum) {
			intfc = config->intf_cache[i];
			break;
		}
	}
	if (!intfc || intfc->num_altsetting >= nalts[i])
		goto skip_to_next_interface_descriptor;

	/* Check for duplicate altsetting entries */
	asnum = d->bAlternateSetting;
	for ((i = 0, alt = &intfc->altsetting[0]);
	      i < intfc->num_altsetting;
	     (++i, ++alt)) {
		if (alt->desc.bAlternateSetting == asnum) {
			printf("devnum %d: duplicate descriptor for config %d "
			    "interface %d altsetting %d, skipping\n",
			    dev->devnum, cfgno, inum, asnum);
			goto skip_to_next_interface_descriptor;
		}
	}

	++intfc->num_altsetting;
	memcpy(&alt->desc, d, USB_DT_INTERFACE_SIZE);

	/* Skip over any Class Specific or Vendor Specific descriptors;
	 * find the first endpoint or interface descriptor */
	alt->extra = buffer;
	i = find_next_descriptor(buffer, size, USB_DT_ENDPOINT,
	    USB_DT_INTERFACE, &n);
	alt->extralen = i;
	buffer += i;
	size -= i;

	/* Allocate space for the right(?) number of endpoints */
	num_ep = num_ep_orig = alt->desc.bNumEndpoints;
	alt->desc.bNumEndpoints = 0;		// Use as a counter
	if (num_ep > USB_MAXENDPOINTS) {
		printf("devnum %d: too many endpoints for config %d interface %d "
		    "altsetting %d: %d, using maximum allowed: %d\n",
		    dev->devnum, cfgno, inum, asnum, num_ep, USB_MAXENDPOINTS);
		num_ep = USB_MAXENDPOINTS;
	}

	if (num_ep > 0) {	/* Can't allocate 0 bytes */
		len = sizeof(struct usb_host_endpoint) * num_ep;
		alt->endpoint = malloc(len);
		if (!alt->endpoint)
			return -ENOMEM;
		memset(alt->endpoint, 0, len);
	}

	/* Parse all the endpoint descriptors */
	n = 0;
	while (size > 0) {
		if (((struct usb_descriptor_header *) buffer)->bDescriptorType
		     == USB_DT_INTERFACE)
			break;
		retval = usb_parse_endpoint(dev, cfgno, inum, asnum, alt,
		    num_ep, buffer, size);
		if (retval < 0)
			return retval;
		++n;

		buffer += retval;
		size -= retval;
	}

	if (n != num_ep_orig)
		printf("devnum %d: config %d interface %d altsetting %d has %d "
		    "endpoint descriptor%s, different from the interface "
		    "descriptor's value: %d\n",
		    dev->devnum, cfgno, inum, asnum, n, plural(n), num_ep_orig);
	return buffer - buffer0;

skip_to_next_interface_descriptor:
	i = find_next_descriptor(buffer, size, USB_DT_INTERFACE,
	    USB_DT_INTERFACE, NULL);
	return buffer - buffer0 + i;
}

static int usb_parse_configuration(struct usb_device *dev, int cfgidx,
    struct usb_host_config *config, unsigned char *buffer, int size)
{
	unsigned char *buffer0 = buffer;
	int cfgno;
	int nintf, nintf_orig;
	int i, j, n;
	struct usb_interface_cache *intfc;
	unsigned char *buffer2;
	int size2;
	struct usb_descriptor_header *header;
	int len, retval;
	u8 inums[USB_MAXINTERFACES], nalts[USB_MAXINTERFACES];
	unsigned iad_num = 0;

	memcpy(&config->desc, buffer, USB_DT_CONFIG_SIZE);
	if (config->desc.bDescriptorType != USB_DT_CONFIG ||
	    config->desc.bLength < USB_DT_CONFIG_SIZE) {
		printf("devnum %d: invalid descriptor for config index %d: "
		    "type = 0x%X, length = %d\n", dev->devnum, cfgidx,
		    config->desc.bDescriptorType, config->desc.bLength);
		return -EINVAL;
	}
	cfgno = config->desc.bConfigurationValue;

	buffer += config->desc.bLength;
	size -= config->desc.bLength;

	nintf = nintf_orig = config->desc.bNumInterfaces;
	if (nintf > USB_MAXINTERFACES) {
		printf("devnum %d: config %d has too many interfaces: %d, "
		    "using maximum allowed: %d\n",
		    dev->devnum, cfgno, nintf, USB_MAXINTERFACES);
		nintf = USB_MAXINTERFACES;
	}

	/* Go through the descriptors, checking their length and counting the
	 * number of altsettings for each interface */
	n = 0;
	for ((buffer2 = buffer, size2 = size);
	      size2 > 0;
	     (buffer2 += header->bLength, size2 -= header->bLength)) {

		if (size2 < sizeof(struct usb_descriptor_header)) {
			printf("devnum %d: config %d descriptor has %d excess "
			    "byte%s, ignoring\n",
			    dev->devnum, cfgno, size2, plural(size2));
			break;
		}

		header = (struct usb_descriptor_header *) buffer2;
		if ((header->bLength > size2) || (header->bLength < 2)) {
			printf("devnum %d: config %d has an invalid descriptor "
			    "of length %d, skipping remainder of the config\n",
			    dev->devnum, cfgno, header->bLength);
			break;
		}

		if (header->bDescriptorType == USB_DT_INTERFACE) {
			struct usb_interface_descriptor *d;
			int inum;

			d = (struct usb_interface_descriptor *) header;
			if (d->bLength < USB_DT_INTERFACE_SIZE) {
				printf("devnum %d: config %d has an invalid "
				    "interface descriptor of length %d, "
				    "skipping\n", dev->devnum, cfgno, d->bLength);
				continue;
			}

			inum = d->bInterfaceNumber;
			if (inum >= nintf_orig)
				printf("devnum %d: config %d has an invalid "
				    "interface number: %d but max is %d\n",
				    dev->devnum, cfgno, inum, nintf_orig - 1);

			/* Have we already encountered this interface?
			 * Count its altsettings */
			for (i = 0; i < n; ++i) {
				if (inums[i] == inum)
					break;
			}
			if (i < n) {
				if (nalts[i] < 255)
					++nalts[i];
			} else if (n < USB_MAXINTERFACES) {
				inums[n] = inum;
				nalts[n] = 1;
				++n;
			}

		} else if (header->bDescriptorType ==
				USB_DT_INTERFACE_ASSOCIATION) {
			if (iad_num == USB_MAXIADS) {
				printf("devnum %d: found more Interface "
					       "Association Descriptors "
					       "than allocated for in "
					       "configuration %d\n", dev->devnum, cfgno);
			} else {
				config->intf_assoc[iad_num] =
					(struct usb_interface_assoc_descriptor *)header;
				iad_num++;
			}

		} else if (header->bDescriptorType == USB_DT_DEVICE ||
			    header->bDescriptorType == USB_DT_CONFIG)
			printf("devnum %d: config %d contains an unexpected "
			    "descriptor of type 0x%X, skipping\n",
			    dev->devnum, cfgno, header->bDescriptorType);

	}	/* for ((buffer2 = buffer, size2 = size); ...) */
	size = buffer2 - buffer;
	config->desc.wTotalLength = buffer2 - buffer0;

	if (n != nintf)
		printf("devnum %d: config %d has %d interface%s, different from "
		    "the descriptor's value: %d\n",
		    dev->devnum, cfgno, n, plural(n), nintf_orig);
	else if (n == 0)
		printf("devnum %d: config %d has no interfaces?\n", dev->devnum, cfgno);
	config->desc.bNumInterfaces = nintf = n;

	/* Check for missing interface numbers */
	for (i = 0; i < nintf; ++i) {
		for (j = 0; j < nintf; ++j) {
			if (inums[j] == i)
				break;
		}
		if (j >= nintf)
			printf("devnum %d: config %d has no interface number %d\n", dev->devnum, cfgno, i);
	}

	/* Allocate the usb_interface_caches and altsetting arrays */
	for (i = 0; i < nintf; ++i) {
		j = nalts[i];
		if (j > USB_MAXALTSETTING) {
			printf("devnum %d: too many alternate settings for "
			    "config %d interface %d: %d, "
			    "using maximum allowed: %d\n",
			    dev->devnum, cfgno, inums[i], j, USB_MAXALTSETTING);
			nalts[i] = j = USB_MAXALTSETTING;
		}

		len = sizeof(*intfc) + sizeof(struct usb_host_interface) * j;
		config->intf_cache[i] = intfc = malloc(len);
		if (!intfc)
			return -ENOMEM;
		memset(intfc, 0, len);
	}

	/* Skip over any Class Specific or Vendor Specific descriptors;
	 * find the first interface descriptor */
	config->extra = buffer;
	i = find_next_descriptor(buffer, size, USB_DT_INTERFACE,
	    USB_DT_INTERFACE, &n);
	config->extralen = i;
	buffer += i;
	size -= i;

	/* Parse all the interface/altsetting descriptors */
	while (size > 0) {
		retval = usb_parse_interface(dev, cfgno, config,
		    buffer, size, inums, nalts);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
	}

	/* Check for missing altsettings */
	for (i = 0; i < nintf; ++i) {
		intfc = config->intf_cache[i];
		for (j = 0; j < intfc->num_altsetting; ++j) {
			for (n = 0; n < intfc->num_altsetting; ++n) {
				if (intfc->altsetting[n].desc.
				    bAlternateSetting == j)
					break;
			}
			if (n >= intfc->num_altsetting)
				printf("devnum %d: config %d interface %d has no "
				    "altsetting %d\n", dev->devnum, cfgno, inums[i], j);
		}
	}

	return 0;
}

// hub-only!! ... and only exported for reset/reinit path.
// otherwise used internally on disconnect/destroy path
void usb_destroy_configuration(struct usb_device *dev)
{
	int c, i;

	if (!dev->config)
		return;

	if (dev->rawdescriptors) {
		for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
			free(dev->rawdescriptors[i]);

		free(dev->rawdescriptors);
		dev->rawdescriptors = NULL;
	}

	for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
		struct usb_host_config *cf = &dev->config[c];

		free(cf->string);
		cf->string = NULL;

		for (i = 0; i < cf->desc.bNumInterfaces; i++) {
			if (cf->intf_cache[i]) {
				if (cf->intf_cache[i]->ref)
					panic("ERROR: usb intf_cache reference counter non-zero %d\n", cf->intf_cache[i]->ref);

				usb_release_interface_cache(cf->intf_cache[i]);
			}
		}
	}
	free(dev->config);
	dev->config = NULL;
}


// hub-only!! ... and only in reset path, or usb_new_device()
// (used by real hubs and virtual root hubs)
int usb_get_configuration(struct usb_device *dev)
{
	int ncfg = dev->descriptor.bNumConfigurations;
	int result = -ENOMEM;
	unsigned int cfgno, length;
	unsigned char *buffer;
	unsigned char *bigbuffer;
 	struct usb_config_descriptor *desc;

	if (ncfg > USB_MAXCONFIG) {
		printf("devnum %d: too many configurations: %d, "
		    "using maximum allowed: %d\n", dev->devnum, ncfg, USB_MAXCONFIG);
		dev->descriptor.bNumConfigurations = ncfg = USB_MAXCONFIG;
	}

	if (ncfg < 1) {
		printf("devnum %d: no configurations\n", dev->devnum);
		return -EINVAL;
	}

	length = ncfg * sizeof(struct usb_host_config);
	dev->config = malloc(length);
	if (!dev->config)
		goto err2;
	memset(dev->config, 0, length);

	length = ncfg * sizeof(char *);
	dev->rawdescriptors = malloc(length);
	if (!dev->rawdescriptors)
		goto err2;
	memset(dev->rawdescriptors, 0, length);

	buffer = cache_align_malloc(USB_DT_CONFIG_SIZE);
	if (!buffer)
		goto err2;
	desc = (struct usb_config_descriptor *)buffer;

	for (cfgno = 0; cfgno < ncfg; cfgno++) {
		/* We grab just the first descriptor so we know how long
		 * the whole configuration is */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno,
		    buffer, USB_DT_CONFIG_SIZE);
		if (result < 0) {
			printf("devnum %d: unable to read config index %d "
			    "descriptor/%s\n", dev->devnum, cfgno, "start");
			goto err;
		} else if (result < 4) {
			printf("devnum %d: config index %d descriptor too short (expected %i, got %i)\n", 
				dev->devnum, cfgno, USB_DT_CONFIG_SIZE, result);
			result = -EINVAL;
			goto err;
		}
		length = max_t(u16, desc->wTotalLength, USB_DT_CONFIG_SIZE);

		/* Now that we know the length, get the whole thing */
		bigbuffer = cache_align_malloc(length);
		if (!bigbuffer) {
			result = -ENOMEM;
			goto err;
		}
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno,
		    bigbuffer, length);
		if (result < 0) {
			printf("devnum %d: unable to read config index %d descriptor/%s\n", 
				dev->devnum, cfgno, "all");
			free(bigbuffer);
			goto err;
		}
		if (result < length) {
			printf("devnum %d: config index %d descriptor too short "
			    "(expected %i, got %i)\n", dev->devnum, cfgno, length, result);
			length = result;
		}

		dev->rawdescriptors[cfgno] = (char *)bigbuffer;

		result = usb_parse_configuration(dev, cfgno,
		    &dev->config[cfgno], bigbuffer, length);
		if (result < 0) {
			++cfgno;
			goto err;
		}
	}
	result = 0;

err:
	free(buffer);
	dev->descriptor.bNumConfigurations = cfgno;
err2:
	if (result == -ENOMEM)
		printf("devnum %d: out of memory\n", dev->devnum);
	return result;
}
